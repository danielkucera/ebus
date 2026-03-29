/*
 * Copyright (C) 2025-2026 Roland Jax
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#if defined(ESP32)
#include "FreeRTOS/BusFreeRtos.hpp"
namespace ebus {
using Bus = BusFreeRtos;
}
#elif defined(POSIX)
#include "Posix/BusPosix.hpp"
namespace ebus {
using Bus = BusPosix;
}
#endif
