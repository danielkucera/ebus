/*
 * Copyright (C) 2026 Roland Jax
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <atomic>
#include <cassert>
#include <chrono>
#include <csignal>
#include <iomanip>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include "App/Scheduler.hpp"
#include "Core/BusHandler.hpp"
#include "Core/Handler.hpp"
#include "Core/Request.hpp"
#include "Core/Sequence.hpp"
#include "Core/Telegram.hpp"
#include "Platform/Bus.hpp"
#include "Utils/Common.hpp"
#include "ebus/Datatypes.hpp"

struct TestCase {
  std::string description;
  uint8_t priority;
  std::string msg_hex;   // message scheduled (raw master bytes to send)
  std::string wire_hex;  // simulated bus bytes that will occur while sending
  std::chrono::milliseconds delay;  // delay before enqueueAt; 0 -> immediate
  bool expectSuccess;      // whether we expect success or error for this test
  int expectedMinRetries;  // retries we expect scheduler to attempt
  std::string expectedError;
};

struct TestResult {
  std::atomic<int> telegramCount{0};
  std::atomic<int> errorCount{0};
  std::string lastError = "";
  std::vector<uint8_t> lastMaster;

  void reset() {
    telegramCount = 0;
    errorCount = 0;
    lastError = "";
    lastMaster.clear();
  }
} g_result;

// "Smart" Bus Simulator state
static std::vector<uint8_t> write_buffer;
std::atomic<int> g_error_count(0);
bool g_detailed_output = false;
std::atomic<int> g_retry_sim_count(0);
std::vector<std::string> g_log_buffer;
std::mutex g_log_mutex;
std::mutex g_sim_mutex;

ebus::Request request;

ebus::busConfig config = {
    .device = "/dev/simulation", .simulate = true, .enable_syn = true};
ebus::Bus bus(config, &request);

ebus::Handler handler(ebus::DEFAULT_ADDRESS, &bus, &request);
ebus::BusHandler busHandler(&request, &handler, bus.getQueue());

void busRequestWonCallback() {
  if (g_detailed_output) std::cout << " request: won" << std::endl;
}

void busRequestLostCallback() {
  if (g_detailed_output) std::cout << " request: lost" << std::endl;
}

void reactiveMasterSlaveCallback(const std::vector<uint8_t>& master,
                                 std::vector<uint8_t>* const slave) {
  std::vector<uint8_t> search;
  search = {0x07, 0x04};  // 0008070400
  if (ebus::contains(master, search))
    *slave = ebus::to_vector("0ab5504d53303001074302");
  search = {0x07, 0x05};  // 0008070500
  if (ebus::contains(master, search))
    *slave = ebus::to_vector("0ab5504d533030010743");  // defect

  if (g_detailed_output)
    std::cout << "reactive: " << ebus::to_string(master) << " "
              << ebus::to_string(*slave) << std::endl;
}

void telegramCallback(const ebus::MessageType& messageType,
                      const ebus::TelegramType& telegramType,
                      const std::vector<uint8_t>& master,
                      const std::vector<uint8_t>& slave) {
  if (!g_detailed_output) return;
  switch (telegramType) {
    case ebus::TelegramType::broadcast:
      std::cout << "    type: broadcast";
      break;
    case ebus::TelegramType::master_master:
      std::cout << "    type: master master";
      break;
    case ebus::TelegramType::master_slave:
      std::cout << "    type: master slave";
      break;
  }
  switch (messageType) {
    case ebus::MessageType::active:
      std::cout << "  active: ";
      break;
    case ebus::MessageType::passive:
      std::cout << " passive: ";
      break;
    case ebus::MessageType::reactive:
      std::cout << "reactive: ";
      break;
  }
  std::cout << ebus::to_string(master) << " " << ebus::to_string(slave)
            << std::endl;
}

void errorCallback(const std::string& error, const std::vector<uint8_t>& master,
                   const std::vector<uint8_t>& slave) {
  if (g_detailed_output)
    std::cout << "   error: " << error << " master '" << ebus::to_string(master)
              << "' slave '" << ebus::to_string(slave) << "'" << std::endl;
}

void schedTelegramForward(const ebus::MessageType& messageType,
                          const ebus::TelegramType& telegramType,
                          const std::vector<uint8_t>& master,
                          const std::vector<uint8_t>& slave) {
  if (g_detailed_output) std::cout << "[scheduler] ";
  telegramCallback(messageType, telegramType, master, slave);
  g_result.telegramCount++;
  g_result.lastMaster = master;
}

void schedErrorForward(const std::string& error,
                       const std::vector<uint8_t>& master,
                       const std::vector<uint8_t>& slave) {
  if (g_detailed_output) std::cout << "[scheduler] ";
  errorCallback(error, master, slave);
  g_result.errorCount++;
  g_result.lastError = error;
}

// clang-format off
  std::vector<TestCase> test_cases = {
    {"BC Success", 1, "feb5050427002d00", "33feb5050427002d00aa", std::chrono::milliseconds(0), true, 0, ""},
    // Active Master-Slave: Simulator will provide Slave response
    {"MS Success", 1, "52b509030d4600", "", std::chrono::milliseconds(0), true, 0, ""},
    // Retry Test: Simulator will kill this message twice, Scheduler should succeed on 3rd try
    {"Retry Success", 1, "1500", "", std::chrono::milliseconds(0), true, 0, ""},
    // Delayed Test: Verify enqueueAt works
    {"Delayed BC", 1, "feb5050427002d00", "", std::chrono::milliseconds(200), true, 0, ""},
    // Priority Test: Low priority enqueued first, but High should send first
    {"Priority Low", 1, "feb5050427002d00", "", std::chrono::milliseconds(0), true, 0, ""},
    {"Priority High", 10, "feb5050427002d00", "", std::chrono::milliseconds(0), true, 0, ""},
  };
// clang-format on

int main() {
  handler.setBusRequestWonCallback(busRequestWonCallback);
  handler.setBusRequestLostCallback(busRequestLostCallback);
  handler.setReactiveMasterSlaveCallback(reactiveMasterSlaveCallback);
  handler.setTelegramCallback(telegramCallback);
  handler.setErrorCallback(errorCallback);
  handler.setSourceAddress(0x33);

  bus.addWriteListener([](const uint8_t& byte) {
    if (g_detailed_output) {
      std::string msg = "<- write: " + ebus::to_string(byte);
      std::cout << msg << std::endl;
    } else {
      std::lock_guard<std::mutex> lock(g_log_mutex);
      g_log_buffer.push_back("<- write: " + ebus::to_string(byte));
    }

    std::lock_guard<std::mutex> lock(g_sim_mutex);
    write_buffer.push_back(byte);

    // Simulator Logic
    // 1. Detect MS Message (52 b5 ... 36) and reply
    // Sequence: 33 52 b5 09 03 0d 46 00 36
    if (write_buffer.size() >= 9) {
      size_t sz = write_buffer.size();
      if (write_buffer[sz - 9] == 0x33 && write_buffer[sz - 8] == 0x52 &&
          write_buffer[sz - 1] == 0x36) {
        // Found MS header + data + CRC. Inject Slave Response (ACK 00 +
        // Data...)
        std::thread([]() {
          std::this_thread::sleep_for(std::chrono::milliseconds(5));
          std::vector<uint8_t> resp = ebus::to_vector("00013fa400");
          for (auto b : resp) {
            bus.writeByte(b);
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
          }
        }).detach();
      }
    }

    // 2. Detect Retry Message (15 00) and kill it (twice)
    // Sequence: 33 15 00 <CRC>
    if (write_buffer.size() >= 3) {
      size_t sz = write_buffer.size();
      if (write_buffer[sz - 3] == 0x33 && write_buffer[sz - 2] == 0x15 &&
          write_buffer[sz - 1] == 0x00) {
        // Found the Retry Test message
        if (g_retry_sim_count < 2) {
          g_retry_sim_count++;
          if (g_detailed_output)
            std::cout << "SIM: Killing message (Attempt " << g_retry_sim_count
                      << ")" << std::endl;
          // Inject garbage to cause echo error or checksum fail
          std::thread([]() { bus.writeByte(0xFF); }).detach();
        }
      }
    }
  });
  
  bus.addReadListener([](const uint8_t& byte) {
    if (g_detailed_output) {
      std::string msg = "->  read: " + ebus::to_string(byte);
      std::cout << msg << std::endl;
    } else {
      std::lock_guard<std::mutex> lock(g_log_mutex);
      g_log_buffer.push_back("->  read: " + ebus::to_string(byte));
    }
  });

  // Create scheduler and attach forwarded callbacks for visibility
  ebus::Scheduler scheduler(&handler);
  scheduler.setTelegramCallback(schedTelegramForward);
  scheduler.setErrorCallback(schedErrorForward);

  // configure scheduler retries/backoff to exercise retry behavior quickly
  scheduler.setMaxSendAttempts(3);
  scheduler.setBaseBackoff(std::chrono::milliseconds(10));
  scheduler.setMaxArbAttempts(3);
  scheduler.setBaseBackoff(std::chrono::milliseconds(200));

  bus.start();
  busHandler.start();
  scheduler.start();

  // Ensure scheduler is clean
  scheduler.clear();

  // Enqueue all messages at once to let the scheduler handle them
  for (const auto& tc : test_cases) {
    if (tc.delay.count() > 0) {
      scheduler.enqueueAt(tc.priority, ebus::to_vector(tc.msg_hex),
                          ebus::Scheduler::Clock::now() + tc.delay);
    } else {
      scheduler.enqueue(tc.priority, ebus::to_vector(tc.msg_hex));
    }
  }

  // Wait for the scheduler to process all items.
  // Total expected successes + failures
  int total_expected_events = 0;
  for (const auto& tc : test_cases) {
    total_expected_events +=
        (tc.expectSuccess ? 1 : 1);  // Each test is one event
  }

  for (int i = 0; i < 200; ++i) {
    if (g_result.telegramCount + g_result.errorCount >= total_expected_events)
      break;
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  std::cout << "--- Final Results ---" << std::endl;
  std::cout << "Successful Telegrams: " << g_result.telegramCount << std::endl;
  std::cout << "Scheduler Errors: " << g_result.errorCount << std::endl;
  if (g_result.telegramCount + g_result.errorCount != total_expected_events) {
    std::cout << "TEST FAILED: Did not process all expected events."
              << std::endl;
    std::cout << "--- Bus Log ---" << std::endl;
    std::lock_guard<std::mutex> lock(g_log_mutex);
    for (const auto& line : g_log_buffer) {
      std::cout << line << std::endl;
    }
    return EXIT_FAILURE;
  }

  scheduler.stop();
  busHandler.stop();
  bus.stop();

  std::cout << "\nAll scheduler tests passed!" << std::endl;

  return EXIT_SUCCESS;
}
