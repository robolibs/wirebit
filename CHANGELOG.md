# Changelog

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

