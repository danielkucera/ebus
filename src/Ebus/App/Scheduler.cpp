/*
 * Copyright (C) 2026 Roland Jax
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "App/Scheduler.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <vector>

#include "Utils/Common.hpp"

ebus::Scheduler::Scheduler(Handler* handler)
    : handler_(handler),
      stopFlag_(true),
      nextId_(1),
      maxSendAttempts_(3),
      maxArbAttempts_(3),
      baseBackoff_(std::chrono::milliseconds(100)) {
  attachHandlerCallbacks();
}

ebus::Scheduler::~Scheduler() { stop(); }

void ebus::Scheduler::start() {
  bool expected = true;
  if (stopFlag_.compare_exchange_strong(expected, false)) {
    worker_ = std::make_unique<ServiceThread>(
        "ebusScheduler", [this] { run(); }, 4096, 5, 1);
    worker_->start();
  }
}

void ebus::Scheduler::stop() {
  bool expected = false;
  if (stopFlag_.compare_exchange_strong(expected, true)) {
    {
      std::lock_guard<std::mutex> lock(dataMutex_);
      dataReadyCv_.notify_all();
    }

    {
      std::lock_guard<std::mutex> lock(transferMutex_);
      transferFinishedCv_.notify_all();
    }

    detachHandlerCallbacks();

    if (worker_) worker_->join();
  }
}

void ebus::Scheduler::enqueue(uint8_t priority,
                              const std::vector<uint8_t>& message,
                              ResultCallback callback) {
  Item it;
  it.priority = priority;
  it.due = Clock::now();
  it.message = message;
  it.resultCallback = std::move(callback);
  it.id = nextId_++;
  pushItem(std::move(it));
}

void ebus::Scheduler::enqueueAt(uint8_t priority,
                                const std::vector<uint8_t>& message,
                                TimePoint when, ResultCallback callback) {
  Item it;
  it.priority = priority;
  it.due = when;
  it.message = message;
  it.resultCallback = std::move(callback);
  it.id = nextId_++;
  pushItem(std::move(it));
}

void ebus::Scheduler::setMaxSendAttempts(int sendAttempts) {
  maxSendAttempts_ = std::max(1, sendAttempts);
}

void ebus::Scheduler::setMaxArbAttempts(int arbAttempts) {
  maxArbAttempts_ = std::max(1, arbAttempts);
}

void ebus::Scheduler::setBaseBackoff(Duration duration) {
  baseBackoff_ = duration;
}

void ebus::Scheduler::setTelegramCallback(TelegramCallback callback) {
  externTelegramCallback_ = std::move(callback);
}

void ebus::Scheduler::setErrorCallback(ErrorCallback callback) {
  externErrorCallback_ = std::move(callback);
}

size_t ebus::Scheduler::queueSize() {
  std::lock_guard<std::mutex> lock(dataMutex_);
  return queue_.size();
}

void ebus::Scheduler::clear() {
  std::lock_guard<std::mutex> lock(dataMutex_);
  queue_.clear();
  std::make_heap(queue_.begin(), queue_.end(), Compare());
}

void ebus::Scheduler::pushItem(Item&& it) {
  std::lock_guard<std::mutex> lock(dataMutex_);
  queue_.push_back(std::move(it));
  std::push_heap(queue_.begin(), queue_.end(), Compare());
  dataReadyCv_.notify_one();
}

void ebus::Scheduler::run() {
  std::unique_lock<std::mutex> lock(dataMutex_);

  // Main loop: wait for next due item, attempt to send, and handle retries if
  // needed
  while (!stopFlag_.load()) {
    if (queue_.empty()) {
      dataReadyCv_.wait(lock,
                        [this] { return stopFlag_.load() || !queue_.empty(); });
      if (stopFlag_.load()) break;
    }

    // copy next due while holding lock
    auto next_due = queue_.front().due;

    dataReadyCv_.wait_until(lock, next_due, [this, next_due] {
      return stopFlag_.load() || queue_.empty() ||
             queue_.front().due <= Clock::now() ||
             queue_.front().due < next_due;
    });

    if (stopFlag_.load()) break;
    if (queue_.empty()) continue;
    if (queue_.front().due > Clock::now()) continue;

    std::pop_heap(queue_.begin(), queue_.end(), Compare());
    Item currentItem = std::move(queue_.back());
    queue_.pop_back();

    lock.unlock();

    bool sent = false;
    std::string lastError = "unknown";

    // Arbitration loop: attempt to send message, and if it fails due to
    // arbitration loss or similar, retry up to maxArbAttempts_ times before
    // giving up and counting as a send attempt failure.
    for (int arbAttempt = 0; arbAttempt < maxArbAttempts_; ++arbAttempt) {
      uint32_t attemptId = currentItem.id;  // use item id as token
      currentAttemptId_.store(attemptId, std::memory_order_relaxed);

      {
        std::lock_guard<std::mutex> callbackLock(transferMutex_);
        pendingState_ = PendingState::WaitingForStart;
        busWon_.store(false, std::memory_order_relaxed);
      }

      if (stopFlag_.load()) break;

      if (handler_->sendActiveMessage(currentItem.message)) {
        std::unique_lock<std::mutex> callbackLock(transferMutex_);

        // waiting for FSM (500ms for 2400 Baud)
        bool signaled = transferFinishedCv_.wait_for(
            callbackLock, std::chrono::milliseconds(500), [this, attemptId] {
              // only return true for signals that correspond to current attempt
              return (currentAttemptId_.load(std::memory_order_relaxed) ==
                      attemptId) &&
                     (pendingState_ == PendingState::Done || stopFlag_.load());
            });

        if (stopFlag_.load()) {
          pendingState_ = PendingState::Idle;
          break;
        }

        if (!signaled) {
          lastError = "FSM timeout";
        } else if (currentAttemptId_.load(std::memory_order_relaxed) ==
                       attemptId &&
                   busWon_.load(std::memory_order_relaxed)) {
          // Capture slave response if available for the callback
          // This assumes the Handler's telegram callback updated Scheduler's
          // internal tracking or we can query it.
          sent = true;
          break;
        } else {
          lastError = "Bus arbitration lost";
        }
      } else {
        lastError = "Handler rejected message";
        break;
      }

      pendingState_ = PendingState::Idle;

      if (!sent && arbAttempt + 1 < maxArbAttempts_) {
        sleep_ms(20);
      }
    }

    // clear attempt id so stray callbacks are ignored
    currentAttemptId_.store(0, std::memory_order_relaxed);

    if (sent) {
      if (currentItem.resultCallback) {
        // We don't have the slave bytes easily here without more plumbing,
        // but we can pass an indicator of success.
        currentItem.resultCallback(true, currentItem.message, {});
      }
      lock.lock();
    } else {
      ++currentItem.sendAttempts;
      if (currentItem.sendAttempts < maxSendAttempts_) {
        currentItem.due =
            Clock::now() + backoffDuration(currentItem.sendAttempts);

        lock.lock();
        queue_.push_back(std::move(currentItem));
        std::push_heap(queue_.begin(), queue_.end(), Compare());
        dataReadyCv_.notify_one();
      } else {
        if (externErrorCallback_) {
          externErrorCallback_(lastError, currentItem.message, {});
        }
        if (currentItem.resultCallback) {
          currentItem.resultCallback(false, currentItem.message, {});
        }
        lock.lock();
      }
    }

    {
      std::lock_guard<std::mutex> callbackLock(transferMutex_);
      pendingState_ = PendingState::Idle;
    }
  }
}

ebus::Scheduler::Duration ebus::Scheduler::backoffDuration(int attempt) const {
  // exponential backoff: base * 2^(attempt-1)
  int shift = std::max(0, attempt - 1);
  // cap shift to avoid undefined/overflowed shifts
  constexpr int kMaxShift = 30;
  shift = std::min(shift, kMaxShift);

  // multiply baseBackoff_ by (1 << shift) using integer multiplication on
  // count() to avoid accidental scaling issues with some duration types.
  using Rep = typename Duration::rep;

  // Cap shift to prevent overflow if Rep is small or base is large
  if (shift >= static_cast<int>(sizeof(Rep) * 8 - 2)) return Duration::max();

  Rep factor = static_cast<Rep>(1) << shift;
  return Duration(static_cast<Rep>(baseBackoff_.count() * factor));
}

void ebus::Scheduler::attachHandlerCallbacks() {
  if (!handler_) return;

  handler_->setBusRequestWonCallback([this]() {
    uint32_t id = currentAttemptId_.load(std::memory_order_relaxed);
    if (id == 0) return;

    std::lock_guard<std::mutex> lock(transferMutex_);
    busWon_.store(true, std::memory_order_relaxed);
    // wake waiter to re-evaluate predicate (telegram may have arrived earlier)
    if (pendingState_ == PendingState::WaitingForStart) {
      transferFinishedCv_.notify_one();
    }
  });

  handler_->setBusRequestLostCallback([this]() {
    uint32_t id = currentAttemptId_.load(std::memory_order_relaxed);
    if (id == 0) return;

    std::lock_guard<std::mutex> lock(transferMutex_);
    if (pendingState_ == PendingState::WaitingForStart) {
      pendingState_ = PendingState::Done;
      transferFinishedCv_.notify_one();
    }
    busWon_.store(false, std::memory_order_relaxed);
  });

  handler_->setTelegramCallback([this](const MessageType& messageType,
                                       const TelegramType& telegramType,
                                       const std::vector<uint8_t>& master,
                                       const std::vector<uint8_t>& slave) {
    if (externTelegramCallback_)
      externTelegramCallback_(messageType, telegramType, master, slave);

    uint32_t id = currentAttemptId_.load(std::memory_order_relaxed);
    if (id == 0) return;

    std::lock_guard<std::mutex> lock(transferMutex_);
    if (pendingState_ == PendingState::WaitingForStart) {
      // mark done when telegram arrives; capture busWon_ state for later check
      pendingState_ = PendingState::Done;
      transferFinishedCv_.notify_one();
    }
  });

  handler_->setErrorCallback([this](const std::string& error,
                                    const std::vector<uint8_t>& master,
                                    const std::vector<uint8_t>& slave) {
    if (externErrorCallback_) externErrorCallback_(error, master, slave);

    auto state = handler_->getState();

    // Reset on certain error conditions that indicate the bus is now free again
    if (state == ebus::HandlerState::releaseBus ||
        state == ebus::HandlerState::passiveReceiveMaster) {
      uint32_t id = currentAttemptId_.load(std::memory_order_relaxed);
      if (id == 0) return;

      std::lock_guard<std::mutex> lock(transferMutex_);
      if (pendingState_ == PendingState::WaitingForStart) {
        busWon_.store(false, std::memory_order_relaxed);
        pendingState_ = PendingState::Done;
        transferFinishedCv_.notify_one();
      }
    }
  });
}

void ebus::Scheduler::detachHandlerCallbacks() {
  if (!handler_) return;
  handler_->setBusRequestWonCallback(nullptr);
  handler_->setBusRequestLostCallback(nullptr);
  handler_->setReactiveMasterSlaveCallback(nullptr);
  handler_->setTelegramCallback(nullptr);
  handler_->setErrorCallback(nullptr);
}
