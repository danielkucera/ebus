/*
 * Copyright (C) 2026 Roland Jax
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <string>
#include <vector>

#include "Core/Handler.hpp"
#include "Platform/ServiceThread.hpp"

namespace ebus {

class Scheduler {
 public:
  using Clock = std::chrono::steady_clock;
  using TimePoint = Clock::time_point;
  using Duration = Clock::duration;

  using ResultCallback =
      std::function<void(bool success, const std::vector<uint8_t>& master,
                         const std::vector<uint8_t>& slave)>;

  struct Item {
    uint8_t priority = 0;  // larger = higher priority (e.g. 255 is top)
    TimePoint due = Clock::now();
    uint32_t id = 0;
    int sendAttempts = 0;         // macro retries (with backoff)
    int arbitrationAttempts = 0;  // micro retries (without backoff)
    std::vector<uint8_t> message;
    ResultCallback resultCallback = nullptr;
  };

  Scheduler(Handler* handler);
  ~Scheduler();

  Scheduler(const Scheduler&) = delete;
  Scheduler& operator=(const Scheduler&) = delete;

  void start();
  void stop();

  void enqueue(uint8_t priority, const std::vector<uint8_t>& message,
               ResultCallback callback = nullptr);
  void enqueueAt(uint8_t priority, const std::vector<uint8_t>& message,
                 TimePoint when, ResultCallback callback = nullptr);

  void setMaxSendAttempts(int sendAttempts);
  void setMaxArbAttempts(int arbAttempts);
  void setBaseBackoff(Duration duration);

  void setTelegramCallback(TelegramCallback callback);
  void setErrorCallback(ErrorCallback callback);

  size_t queueSize();

  void clear();

 private:
  struct Compare {
    bool operator()(Item const& lhs, Item const& rhs) const {
      if (lhs.due != rhs.due)
        return lhs.due > rhs.due;          // earlier due time first
      return lhs.priority < rhs.priority;  // larger priority value second
    }
  };

  enum class PendingState { Idle, WaitingForStart, Done };

  Handler* handler_ = nullptr;

  // Queue management
  std::vector<Item> queue_;
  std::mutex dataMutex_;
  std::condition_variable dataReadyCv_;

  // Worker thread
  std::unique_ptr<ServiceThread> worker_;
  std::atomic<bool> stopFlag_;
  std::atomic<uint32_t> nextId_;

  // Active transfer state (protected by transferMutex_)
  std::atomic<uint32_t> currentAttemptId_{0};
  std::mutex transferMutex_;
  std::condition_variable transferFinishedCv_;
  PendingState pendingState_ = PendingState::Idle;
  std::atomic<bool> busWon_{false};

  // Configuration
  int maxSendAttempts_;
  int maxArbAttempts_;
  Duration baseBackoff_;

  // Forwarded callbacks
  TelegramCallback externTelegramCallback_ = nullptr;
  ErrorCallback externErrorCallback_ = nullptr;

  void pushItem(Item&& it);
  void run();
  Duration backoffDuration(int attempt) const;
  void attachHandlerCallbacks();
  void detachHandlerCallbacks();
};

}  // namespace ebus
