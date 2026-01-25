# Changelog

## [0.0.12] - 2026-01-25

### <!-- 0 -->⛰️  Features

- Implement TTY serial link for Linux

## [0.0.11] - 2026-01-18

### <!-- 7 -->⚙️ Miscellaneous Tasks

- Refactor build system to CMake

## [0.0.10] - 2026-01-09

### <!-- 1 -->🐛 Bug Fixes

- Improve PTY write resilience and add flush controls
- Improve hardware flag management in Makefile

### <!-- 3 -->📚 Documentation

- Update README with new features

### <!-- 7 -->⚙️ Miscellaneous Tasks

- Merge main into develop (sync branches)
- Update config by ignoring sudoers files
- Update sudoers configuration for Wirebit

## [0.0.9] - 2026-01-05

### <!-- 1 -->🐛 Bug Fixes

- Remove compiler warnings in hardware links (strncpy, system return)

### <!-- 2 -->🚜 Refactor

- Migrate from HAS_HARDWARE (opt-in) to NO_HARDWARE (opt-out)

## [0.0.8] - 2026-01-05

### <!-- 0 -->⛰️  Features

- Implement TunLink for Linux TUN interface (L3 IP packets)

### <!-- 3 -->📚 Documentation

- Fix roadmap versions (TAP=v0.6, TUN=v0.7)
- Update README with TUN support (v0.5)

## [0.0.7] - 2026-01-05

### <!-- 0 -->⛰️  Features

- Implement TunLink for Linux TUN interface (L3 IP packets)

### <!-- 3 -->📚 Documentation

- Fix roadmap versions (TAP=v0.6, TUN=v0.7)
- Update README with TUN support (v0.5)

## [0.0.6] - 2026-01-05

### <!-- 0 -->⛰️  Features

- Implement TapLink for Linux TAP interface

### <!-- 3 -->📚 Documentation

- Update README with hardware link documentation (PtyLink, SocketCanLink, TapLink)

### <!-- 6 -->🧪 Testing

- Add explicit TAP interface creation/destruction verification tests

## [0.0.5] - 2026-01-05

### <!-- 0 -->⛰️  Features

- Implement SocketCanLink for Linux SocketCAN interface

### <!-- 7 -->⚙️ Miscellaneous Tasks

- Add sudoers configuration and vcan test

## [0.0.4] - 2026-01-05

### <!-- 0 -->⛰️  Features

- Add Pseudo-Terminal (PTY) Link

## [0.0.3] - 2026-01-04

### <!-- 0 -->⛰️  Features

- Add hardware interface links (PTY, SocketCAN, TAP)

## [0.0.2] - 2026-01-04

### <!-- 0 -->⛰️  Features

- Add multi-process integration tests
- Add CAN bus hub and node client applications
- Implement EthEndpoint with L2 frame support
- Add comprehensive CAN endpoint demo
- Implement CanEndpoint with SocketCAN compatibility
- Add comprehensive serial endpoint demo
- Implement SerialEndpoint with baud rate pacing
- Enhance ShmLink with LinkModel integration and statistics
- Add comprehensive tests and examples
- Implement deterministic LinkModel with PRNG
- Implement eventfd notification system with comprehensive tests
- Implement FrameRing wrapper for SPSC ring buffer (wirebit-p3e.2)
- Implement wirebit project skeleton (wirebit-p3e.1)
- Implement a comprehensive C++ serial communication library
- A very detailed example 'main.cpp'
- Add NMEA 0183 and PHTG to CAN and serial bridges
- Add SocketCAN ISOBUS example and fix library link order
- Add AgIsoStack as a dependency
- Init

### <!-- 1 -->🐛 Bug Fixes

- Improve integration tests with unique SHM names and proper cleanup
- Workaround datapod String buffer overflow with long SHM names
- Remove ShmLink workaround after datapod RingBuffer fix

### <!-- 2 -->🚜 Refactor

- Implement proper Frame structure with FrameHeader
- Use datapod::Stamp<FrameData> for Frame type
- Use datapod::Stamp for time utilities
- Unify and simplify build system across CMake, XMake, and Zig
- Introduce event-driven serial communication API
- Rename project from isobus to tractor
- Cherry picking
- Refactor build system for clearer linker control

### <!-- 3 -->📚 Documentation

- Add comprehensive README.md
- Document datapod RingBuffer SHM ownership bug and implement workaround

### <!-- 6 -->🧪 Testing

- Update tests for new Frame structure

### <!-- 7 -->⚙️ Miscellaneous Tasks

- Add empty ACKNOWLEDGMENTS.md and CHANGELOG.md

### Build

- Update build environment variables and aliases

