/*
 * Copyright (C) 2025 Roland Jax
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

// This file should eventually be split into StringUtils.hpp, BusUtils.hpp, and
// TimeUtils.hpp

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "ebus/Definitions.hpp"

namespace ebus {

// symbols
constexpr uint8_t sym_zero = 0x00;  // zero byte

// sizes
constexpr uint8_t max_bytes = 0x10;  // 16 maximum data bytes

bool isMaster(const uint8_t& byte);
bool isSlave(const uint8_t& byte);

bool isTarget(const uint8_t& byte);

uint8_t masterOf(const uint8_t& byte);
uint8_t slaveOf(const uint8_t& byte);

const std::string to_string(const uint8_t& byte);
const std::string to_string(const std::vector<uint8_t>& vec);

const std::vector<uint8_t> to_vector(const std::string& str);

const std::vector<uint8_t> range(const std::vector<uint8_t>& vec,
                                 const size_t& index, const size_t& len);

bool contains(const std::vector<uint8_t>& vec,
              const std::vector<uint8_t>& search);

bool matches(const std::vector<uint8_t>& vec,
             const std::vector<uint8_t>& search, size_t index);

uint8_t calc_crc(const uint8_t& byte, const uint8_t& init);

/**
 * Returns the number of zero bits in a byte.
 */
uint8_t countZeroBits(uint8_t byte);

void sleep_ms(uint32_t ms);

}  // namespace ebus
