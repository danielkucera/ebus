# C++ library for eBUS communication

This library enables communication with systems based on the eBUS protocol. eBUS is primarily used in the heating industry.

### eBUS Overview

- The eBUS works on a two-wire bus with a speed of 2400 baud.
- Realisation with Standard UART with 8 bits + start bit + stop bit. 
- A maximum of 25 master and 228 slave participants are possible.
- The eBUS protocol is byte-oriented with byte-oriented arbitration.
- Data protection through 8-bit CRC.

### Class Overview

The library is designed with a clear separation between the public API and internal protocol orchestration.

#### Public API (`include/ebus/`)
- **Controller**: The primary interface for applications. It manages the lifecycle, scheduling, and diagnostic aggregation. Encapsulated using the PIMPL idiom to hide internal complexity.
- **Config**: Platform-independent configuration for the controller and hardware-specific bus settings.
- **Definitions**: Central source of truth for protocol symbols, enums, and callback signatures.
- **Metrics**: Unified data models for bus health monitoring (jitter, utilization, error rates).
- **Datatypes**: Advanced encoding/decoding utilities for eBUS-specific data formats.

#### Internal Implementation (`src/Ebus/`)
- **App**: Orchestration layer containing the **Scheduler** (priority-based transmission), **PollManager** (recurring jobs), and **ClientManager** (network bridging for ebusd).
- **Core**: The protocol engine. **Handler** manages the Finite State Machine (FSM) for telegrams, while **Request** handles byte-oriented arbitration.
- **Platform**: Abstraction layer for **Bus** (POSIX/FreeRTOS) and **ServiceThread** (threading).

### Diagnostics & Bus Health

The library includes a unified telemetry system accessible via `Controller::getMetrics()`. It provides:
- **Bus Utilization**: Physical line low-time calculation.
- **Error Rate**: Percentage-based protocol health.
- **Contention Rate**: Collision monitoring during arbitration.
- **Jitter Analysis**: Timing statistics for SYN symbols and response latencies.

### Tools

**ebusread**: A diagnostic tool that interprets incoming streams as eBUS telegrams. Supports files, devices, pipes, and TCP sockets.

**playground**: A developer sandbox for testing library features and protocol edge cases.

### Build

Compilation requires CMake and a C++ compiler (tested on GCC) with C++14 support. 


For reporting bugs and requesting features, please use the GitHub [Issues](https://github.com/yuhu-/ebus/issues) page.
