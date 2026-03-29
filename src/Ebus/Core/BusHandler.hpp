/*
 * Copyright (C) 2025-2026 Roland Jax
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <functional>
#include <mutex>
#include <utility>
#include <vector>

#include "Core/Handler.hpp"
#include "Core/Request.hpp"
#include "Platform/Queue.hpp"
#include "Platform/ServiceThread.hpp"

namespace ebus {

/**
 * Background worker that processes raw bytes from the Bus queue and feeds
 * them into the Request and Handler state machines. It also manages a
 * registry of byte listeners.
 */
class BusHandler {
 public:
  using ByteListener = std::function<void(const uint8_t& byte)>;

  BusHandler(Request* request, Handler* handler, Queue<BusEvent>* queue)
      : request_(request), handler_(handler), queue_(queue), running_(false) {}

  ~BusHandler() { stop(); }

  void start() {
    if (running_) return;
    running_ = true;
    worker_ = std::make_unique<ServiceThread>(
        "ebusBusQueueRunner", [this] { this->run(); }, 4096, 1);
    worker_->start();
  }

  void stop() {
    running_ = false;
    if (worker_) worker_->join();
  }

  uint32_t addByteListener(ByteListener listener) {
    std::lock_guard<std::mutex> lock(mutex_);
    uint32_t id = nextListenerId_++;
    listeners_.push_back({id, std::move(listener)});
    return id;
  }

  void removeByteListener(uint32_t id) {
    std::lock_guard<std::mutex> lock(mutex_);
    listeners_.erase(
        std::remove_if(listeners_.begin(), listeners_.end(),
                       [id](const std::pair<uint32_t, ByteListener>& p) {
                         return p.first == id;
                       }),
        listeners_.end());
  }

 private:
  Request* request_;
  Handler* handler_;
  Queue<BusEvent>* queue_;
  std::atomic<bool> running_;

  std::unique_ptr<ServiceThread> worker_;

  uint32_t nextListenerId_ = 0;
  mutable std::mutex mutex_;
  std::vector<std::pair<uint32_t, ByteListener>> listeners_;

  void run() {
    BusEvent event;
    while (running_) {
      if (queue_->pop(event, std::chrono::milliseconds(100))) {
        if (request_) {
          if (event.busRequest) request_->busRequestCompleted();
          if (event.startBit) request_->startBit();
          request_->run(event.byte);
        }
        if (handler_) handler_->run(event.byte);

        std::vector<ByteListener> listenersCopy;
        {
          std::lock_guard<std::mutex> lock(mutex_);
          listenersCopy.reserve(listeners_.size());
          for (const auto& item : listeners_)
            listenersCopy.push_back(item.second);
        }
        for (const ByteListener& listener : listenersCopy) listener(event.byte);
      }
    }
  }
};

}  // namespace ebus
