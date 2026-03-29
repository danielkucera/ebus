/*
 * Copyright (C) 2026 Roland Jax
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <cstdint>
#include <string>

namespace ebus {

/**
 * Platform-dependent bus configuration.
 */
#if defined(ESP32)
struct busConfig {
  uint8_t uart_port;
  uint8_t rx_pin;
  uint8_t tx_pin;
  uint8_t timer_group;
  uint8_t timer_idx;

  bool enable_syn = false;
  uint8_t master_addr = 0x00;
  uint32_t syn_base_ms = 50;
  uint32_t syn_tolerance_ms = 5;
  bool syn_deterministic = true;
};
#elif defined(POSIX)
struct busConfig {
  std::string device = "/dev/ttyUSB0";
  uint32_t baud = 2400;
  bool simulate = false;
  
  bool enable_syn = false;
  uint8_t master_addr = 0x00;
  uint32_t syn_base_ms = 50;
  uint32_t syn_tolerance_ms = 5;
  bool syn_deterministic = true;
};
#endif

/**
 * Global eBUS Controller configuration.
 */
struct ebusConfig {
  uint8_t address = 0xff;
  uint16_t window = 4300;
  uint16_t offset = 80;
  uint32_t clientTimeoutMs = 1000;
  busConfig bus = {};
};

/**
 * Available client types.
 */
enum class ClientType { ReadOnly, Regular, Enhanced };

}  // namespace ebus