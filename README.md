# wirebit

Header-only C++ library for simulating hardware communication interfaces over shared memory, with bridges to real Linux interfaces (PTY, SocketCAN, TAP, TUN).

## Development Status

See [TODO.md](./TODO.md) for the complete development plan and current progress.

## Overview

Wirebit provides a unified framework for simulating serial, CAN, and Ethernet communication interfaces in software. It enables multi-process testing of embedded systems without requiring physical hardware. Communication happens over shared memory with configurable network impairments (latency, jitter, packet loss, corruption) for realistic simulation.

The library also supports bridging to real Linux interfaces: PTY for serial, SocketCAN for CAN bus, TAP for L2 Ethernet, and TUN for L3 IP. This allows seamless transition from simulation to hardware testing.

Key design principles:
- **Header-only**: No compilation required, just include and use
- **Zero-copy**: Shared memory ring buffers with atomic operations
- **Type-safe**: Uses datapod Result types for error handling
- **Deterministic**: Seeded PRNG for reproducible simulations
- **Hardware-ready**: Bridge to real interfaces with `HARDWARE=1` build flag

### Architecture

```
┌─────────────────────────────────────────────────────────────────────────────────┐
│                              WIREBIT LIBRARY                                     │
├──────────────────┬──────────────────┬──────────────────┬────────────────────────┤
│  SerialEndpoint  │   CanEndpoint    │  EthEndpoint     │      LinkModel         │
│  (Baud pacing)   │  (SocketCAN API) │  (L2 frames)     │     (Impairments)      │
├──────────────────┴──────────────────┴──────────────────┴────────────────────────┤
│                                 Frame Layer                                      │
├──────────────────┬──────────────────┬──────────────────┬───────────┬────────────┤
│    ShmLink       │    PtyLink       │  SocketCanLink   │  TapLink  │  TunLink   │
│  (Simulation)    │  (Serial PTY)    │  (CAN vcan/can)  │ (L2 Eth)  │  (L3 IP)   │
├──────────────────┼──────────────────┼──────────────────┼───────────┼────────────┤
│  Shared Memory   │   /dev/pts/X     │  vcan/can iface  │    TAP    │    TUN     │
└──────────────────┴──────────────────┴──────────────────┴───────────┴────────────┘
```

**Link Types:**
- **ShmLink** - Shared memory for simulation (multi-process IPC)
- **PtyLink** - Linux pseudo-terminal for real serial tools
- **SocketCanLink** - Linux SocketCAN for real CAN bus (requires `HARDWARE=1`)
- **TapLink** - Linux TAP device for L2 Ethernet (requires `HARDWARE=1`)
- **TunLink** - Linux TUN device for L3 IP packets (requires `HARDWARE=1`)

## Installation

### Quick Start (CMake FetchContent)

```cmake
include(FetchContent)
FetchContent_Declare(wirebit
  GIT_REPOSITORY https://github.com/robolibs/wirebit
  GIT_TAG main)
FetchContent_MakeAvailable(wirebit)
target_link_libraries(your_target PRIVATE wirebit)
```

### XMake (Recommended)

```lua
add_requires("wirebit")
target("your_target")
    set_kind("binary")
    add_packages("wirebit")
    add_files("src/*.cpp")
```

## Usage

### Serial Communication

```cpp
#include <wirebit/wirebit.hpp>
using namespace wirebit;

auto link = ShmLink::create(String("serial"), 64 * 1024).value();
SerialConfig config{.baud = 115200, .data_bits = 8, .stop_bits = 1, .parity = 'N'};
SerialEndpoint serial(std::make_shared<ShmLink>(std::move(link)), config, 1);

Bytes data = {0x48, 0x65, 0x6C, 0x6C, 0x6F};  // "Hello"
serial.send(data);
```

### CAN Bus

```cpp
auto link = ShmLink::create(String("can"), 64 * 1024).value();
CanConfig config{.bitrate = 500000};
CanEndpoint can(std::make_shared<ShmLink>(std::move(link)), config, 1);

can_frame frame;
frame.can_id = 0x123;
frame.can_dlc = 4;
std::memcpy(frame.data, "\xDE\xAD\xBE\xEF", 4);
can.send_can(frame);
```

### Ethernet L2

```cpp
auto link = ShmLink::create(String("eth"), 1024 * 1024).value();
MacAddr mac = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};
EthConfig config{.bandwidth_bps = 1000000000};
EthEndpoint eth(std::make_shared<ShmLink>(std::move(link)), config, 1, mac);

Bytes frame = make_eth_frame(MAC_BROADCAST, mac, ETH_P_IP, payload);
eth.send_eth(frame);
```

### Network Impairment

```cpp
LinkModel model{
    .base_latency_ns = 1000000,  // 1 ms
    .jitter_ns = 200000,         // ±200 µs
    .drop_prob = 0.05,           // 5% loss
    .corrupt_prob = 0.01,        // 1% corruption
    .seed = 12345                // Deterministic
};
link->set_model(model);
```

## Hardware Links (Linux)

Build with `HARDWARE=1` to enable real interface bridging:

```bash
HARDWARE=1 make reconfig && HARDWARE=1 make build
```

### PtyLink - Serial Bridge

```cpp
PtyConfig config{.create_symlink = true, .symlink_path = "/tmp/serial"};
auto pty = PtyLink::create(config).value();
// Connect with: screen /tmp/serial 115200
```

### SocketCanLink - CAN Bridge

```cpp
SocketCanConfig config{.interface_name = "vcan0", .create_if_missing = true};
auto can = SocketCanLink::create(config).value();
// Monitor with: candump vcan0
```

### TapLink - L2 Ethernet Bridge

```cpp
TapConfig config{.interface_name = "tap0", .create_if_missing = true, .destroy_on_close = true};
auto tap = TapLink::create(config).value();
// Monitor with: sudo tcpdump -i tap0
```

### TunLink - L3 IP Bridge

```cpp
TunConfig config{.interface_name = "tun0", .ip_address = "10.100.0.1/24", .destroy_on_close = true};
auto tun = TunLink::create(config).value();
// Test with: ping 10.100.0.2 (your app responds to ICMP)
// Monitor with: sudo tcpdump -i tun0
```

**Sudoers setup (optional, for passwordless interface creation):**
```bash
# /etc/sudoers.d/wirebit
%wheel ALL=(ALL) NOPASSWD: /usr/bin/ip link add dev vcan* type vcan
%wheel ALL=(ALL) NOPASSWD: /usr/bin/ip link set vcan* up
%wheel ALL=(ALL) NOPASSWD: /usr/bin/ip link delete vcan*
%wheel ALL=(ALL) NOPASSWD: /usr/bin/ip tuntap add dev tap* mode tap user *
%wheel ALL=(ALL) NOPASSWD: /usr/bin/ip tuntap add dev tun* mode tun user *
%wheel ALL=(ALL) NOPASSWD: /usr/bin/ip link set tap* up
%wheel ALL=(ALL) NOPASSWD: /usr/bin/ip link set tun* up
%wheel ALL=(ALL) NOPASSWD: /usr/bin/ip addr add * dev tun*
%wheel ALL=(ALL) NOPASSWD: /usr/bin/ip link delete tap*
%wheel ALL=(ALL) NOPASSWD: /usr/bin/ip link delete tun*
```

## Features

- **Serial Endpoint** - Baud rate pacing (9600-921600 bps), configurable data/stop/parity bits
- **CAN Endpoint** - SocketCAN-compatible, standard/extended IDs, RTR frames, bit timing
- **Ethernet Endpoint** - L2 frames, MAC filtering, bandwidth shaping, promiscuous mode
- **Hardware Links** - PTY, SocketCAN, TAP (L2), TUN (L3) bridges to real Linux interfaces
- **Shared Memory** - Lock-free SPSC ring buffers, <1µs latency, zero syscalls in hot path
- **Network Simulation** - Latency, jitter, drops, corruption with deterministic PRNG
- **Type-Safe Errors** - datapod Result types, no exceptions in hot path

## Performance

- **Hot path**: Zero syscalls (atomic operations only)
- **Latency**: <1µs for ShmLink push/pop
- **Memory**: Configurable ring buffers (64KB - 1MB typical)
- **Threading**: SPSC per direction, no locks

## Examples

```bash
./serial_demo      # Serial communication scenarios
./can_demo         # CAN bus examples
./ethernet_demo    # Ethernet L2 examples
./can_bus_hub      # Multi-node CAN hub
./pty_demo         # PTY bridge demo
./socketcan_demo   # SocketCAN bridge (HARDWARE=1)
./tap_demo         # TAP L2 bridge (HARDWARE=1)
./tun_demo         # TUN L3 bridge with ICMP responder (HARDWARE=1)
```

## Testing

```bash
make build                    # Build (simulation only)
HARDWARE=1 make build         # Build with hardware support
make test                     # Run all tests
HARDWARE=1 make test          # Include hardware link tests
make test TEST=test_tap_link  # Run specific test
```

**Coverage:** 17 test suites, 200+ assertions, hardware interface verification via `ip link`.

## Roadmap

- **v0.1** - SHM backend with Serial/CAN/Ethernet endpoints ✅
- **v0.2** - PTY backend for serial bridging ✅
- **v0.3** - SocketCAN backend for CAN bridging ✅
- **v0.6** - TAP backend for L2 Ethernet bridging ✅
- **v0.7** - TUN backend for L3 IP bridging ✅
- **v0.8** - Performance optimizations and benchmarking

## Dependencies

- **datapod** - POD-friendly data structures
- **echo** - Logging with color support
- **C++20** - Header-only

## License

MIT License - see [LICENSE](./LICENSE) for details.

## Acknowledgments

Made possible thanks to [these amazing projects](./ACKNOWLEDGMENTS.md).
