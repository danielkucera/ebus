/*
 * Copyright (C) 2026 Roland Jax
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <unistd.h>

#include <cstdint>
#include <ebus/Datatypes.hpp>
#include <string>
#include <vector>

#include "Core/Handler.hpp"

/**
 * Robust read helper to handle partial TCP/Socket reads.
 */
inline bool read_exact(int fd, uint8_t* buffer, size_t length) {
  size_t total = 0;
  while (total < length) {
    ssize_t n = read(fd, buffer + total, length - total);
    if (n <= 0) return false;
    total += n;
  }
  return true;
}

/**
 * Encodes logical cmd/val into wire-format 2-byte sequence.
 * Used for testing the Enhanced ebusd protocol.
 */
inline void encode_enhanced(uint8_t cmd, uint8_t val, uint8_t out[2]) {
  out[0] = 0xc0 | (cmd << 2) | (val >> 6);
  out[1] = 0x80 | (val & 0x3f);
}

const std::string GREETING_STR = "ebus-service 1.0\n";

// Common TestCase struct for eBUS tests
struct TestCase {
  ebus::MessageType messageType;
  uint8_t address;
  std::string description;
  std::string read_string;
  std::string send_string = "";
  struct ExpectedResult {
    int telegram;
    int errors;
  } expected;
};