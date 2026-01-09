<img align="right" width="26%" src="./misc/logo.png">

# wirebit

Header-only C++ library for simulating hardware communication interfaces over shared memory, with bridges to real Linux interfaces.

## Development Status

Current version: **0.0.9** (January 5, 2026)

See [CHANGELOG.md](./CHANGELOG.md) for version history and recent changes.

## Overview

Wirebit provides a unified framework for simulating serial (UART/RS232), CAN bus, and Ethernet communication interfaces in software. It enables multi-process testing of embedded systems without requiring physical hardware, making it ideal for robotics development, automotive testing, and industrial automation scenarios.

Communication happens over shared memory using lock-free SPSC ring buffers with sub-microsecond latency. The library supports configurable network impairments (latency, jitter, packet loss, corruption, duplication) with deterministic pseudo-random number generation for reproducible simulations.

The library also supports bridging to real Linux interfaces: PTY for serial devices, SocketCAN for CAN bus, TAP for L2 Ethernet, and TUN for L3 IP packets. This allows seamless transition from pure simulation to hardware-in-the-loop testing without changing your application code.

Key design principles:
- **Header-only architecture**: No compilation required, just `#include <wirebit/wirebit.hpp>`
- **Zero-copy communication**: Shared memory ring buffers with atomic operations only
- **Type-safe error handling**: Uses `datapod::Result<T, E>` for errors, no exceptions in hot path
- **Deterministic simulation**: Seeded PRNG ensures reproducible test scenarios
- **Hardware-ready**: Conditional compilation for real interface bridging
- **Multi-process IPC**: Share communication channels between processes without network overhead

### Architecture Diagrams

**Component Architecture:**

```
┌─────────────────────────────────────────────────────────────────────────────────┐
│                              WIREBIT LIBRARY                                     │
├──────────────────┬──────────────────┬──────────────────┬────────────────────────┤
│  SerialEndpoint  │   CanEndpoint    │  EthEndpoint     │      LinkModel         │
│  (Baud pacing)   │  (SocketCAN API) │  (L2 frames)     │     (Impairments)      │
│                  │                  │                  │                        │
│  ┌────────────┐  │  ┌────────────┐  │  ┌────────────┐  │  ┌──────────────────┐  │
│  │ 9600-921k  │  │  │ Std/Ext ID │  │  │ MAC filter │  │  │ Latency/Jitter   │  │
│  │ Data/Stop  │  │  │ RTR frames │  │  │ Bandwidth  │  │  │ Drop/Corrupt     │  │
│  │ Parity     │  │  │ Bit timing │  │  │ Promiscuous│  │  │ Deterministic    │  │
│  └────────────┘  │  └────────────┘  │  └────────────┘  │  └──────────────────┘  │
├──────────────────┴──────────────────┴──────────────────┴────────────────────────┤
│                           Frame Layer (44-byte header)                           │
│                    [Magic|Version|Type|Timestamps|IDs|Payload]                   │
├──────────────────┬──────────────────┬──────────────────┬───────────┬────────────┤
│    ShmLink       │    PtyLink       │  SocketCanLink   │  TapLink  │  TunLink   │
│  (Simulation)    │  (Serial PTY)    │  (CAN vcan/can)  │ (L2 Eth)  │  (L3 IP)   │
│                  │                  │                  │           │            │
│  ┌────────────┐  │  ┌────────────┐  │  ┌────────────┐  │ ┌───────┐ │ ┌────────┐ │
│  │ SPSC Ring  │  │  │ /dev/pts/X │  │  │ vcan/can0  │  │ │ tap0  │ │ │ tun0   │ │
│  │ <1µs lat   │  │  │ Symlink    │  │  │ Auto-setup │  │ │ L2    │ │ │ L3     │ │
│  │ Zero-copy  │  │  │ Non-block  │  │  │ Sudoers    │  │ │ Raw   │ │ │ ICMP   │ │
│  └────────────┘  │  └────────────┘  │  └────────────┘  │ └───────┘ │ └────────┘ │
├──────────────────┼──────────────────┼──────────────────┼───────────┼────────────┤
│  Shared Memory   │   /dev/pts/X     │  vcan/can iface  │    TAP    │    TUN     │
│  (Multi-proc)    │  (Serial tools)  │  (CAN tools)     │ (tcpdump) │  (ping)    │
└──────────────────┴──────────────────┴──────────────────┴───────────┴────────────┘
       │                   │                   │               │            │
       └───────────────────┴───────────────────┴───────────────┴────────────┘
                                        │
                                ┌───────▼────────┐
                                │  Your App/Lib  │
                                │  (Multi-proc)  │
                                └────────────────┘
```

**Data Flow Example (CAN Bus Simulation):**

```
Process A                     Shared Memory                    Process B
┌──────────┐                 ┌──────────────┐                 ┌──────────┐
│CanEndpoint│                │   ShmLink    │                 │CanEndpoint│
│  (Node 1) │                │              │                 │  (Node 2) │
│           │                │  ┌────────┐  │                 │           │
│  send()   ├───Frame────────┼─►│ Ring A │──┼────Frame────────┤  recv()   │
│           │                │  └────────┘  │                 │           │
│           │                │              │                 │           │
│  recv()   │◄───Frame───────┼──│ Ring B │◄─┼────Frame────────┤  send()   │
│           │                │  └────────┘  │                 │           │
└──────────┘                 │              │                 └──────────┘
                             │  LinkModel   │
                             │  ┌────────┐  │
                             │  │Latency │  │ (Applied to both directions)
                             │  │Jitter  │  │
                             │  │Drop    │  │
                             │  └────────┘  │
                             └──────────────┘
```

## Installation

### Quick Start (CMake FetchContent)

```cmake
include(FetchContent)
FetchContent_Declare(
  wirebit
  GIT_REPOSITORY https://github.com/robolibs/wirebit
  GIT_TAG main
)
FetchContent_MakeAvailable(wirebit)

target_link_libraries(your_target PRIVATE wirebit)
```

### Recommended: XMake

[XMake](https://xmake.io/) is a modern, fast, and cross-platform build system.

**Install XMake:**
```bash
curl -fsSL https://xmake.io/shget.text | bash
```

**Add to your xmake.lua:**
```lua
add_requires("wirebit")

target("your_target")
    set_kind("binary")
    add_packages("wirebit")
    add_files("src/*.cpp")
```

**Build:**
```bash
xmake
xmake run
```

### Complete Development Environment (Nix + Direnv + Devbox)

For the ultimate reproducible development environment:

**1. Install Nix (package manager from NixOS):**
```bash
# Determinate Nix Installer (recommended)
curl --proto '=https' --tlsv1.2 -sSf -L https://install.determinate.systems/nix | sh -s -- install
```
[Nix](https://nixos.org/) - Reproducible, declarative package management

**2. Install direnv (automatic environment switching):**
```bash
sudo apt install direnv

# Add to your shell (~/.bashrc or ~/.zshrc):
eval "$(direnv hook bash)"  # or zsh
```
[direnv](https://direnv.net/) - Load environment variables based on directory

**3. Install Devbox (Nix-powered development environments):**
```bash
curl -fsSL https://get.jetpack.io/devbox | bash
```
[Devbox](https://www.jetpack.io/devbox/) - Portable, isolated dev environments

**4. Use the environment:**
```bash
cd wirebit
direnv allow  # Allow .envrc (one-time)
# Environment automatically loaded! All dependencies available.

xmake        # or cmake, make, etc.
```

## Usage

### Basic Usage - Serial Communication

```cpp
#include <wirebit/wirebit.hpp>
using namespace wirebit;

// Create shared memory link (64KB buffer)
auto link = ShmLink::create(String("serial"), 64 * 1024).value();

// Configure serial endpoint (115200 baud, 8N1)
SerialConfig config{
    .baud = 115200,
    .data_bits = 8,
    .stop_bits = 1,
    .parity = 'N'
};

// Create endpoint with ID=1
SerialEndpoint serial(std::make_shared<ShmLink>(std::move(link)), config, 1);

// Send data (realistic byte-by-byte timing)
Bytes data = {0x48, 0x65, 0x6C, 0x6C, 0x6F};  // "Hello"
serial.send(data);

// Receive data
serial.process();  // Process incoming frames
auto result = serial.recv();
if (result.is_ok()) {
    Bytes received = result.value();
    // Process received data
}
```

### Basic Usage - CAN Bus

```cpp
// Create shared memory link (64KB buffer)
auto link = ShmLink::create(String("can"), 64 * 1024).value();

// Configure CAN endpoint (500 kbps)
CanConfig config{.bitrate = 500000};
CanEndpoint can(std::make_shared<ShmLink>(std::move(link)), config, 1);

// Send standard CAN frame
can_frame frame;
frame.can_id = 0x123;
frame.can_dlc = 4;
std::memcpy(frame.data, "\xDE\xAD\xBE\xEF", 4);
can.send_can(frame);

// Receive CAN frames
can.process();
auto result = can.recv_can();
if (result.is_ok()) {
    can_frame received = result.value();
    // Process CAN frame
}
```

### Basic Usage - Ethernet L2

```cpp
// Create shared memory link (1MB buffer)
auto link = ShmLink::create(String("eth"), 1024 * 1024).value();

// Configure Ethernet endpoint (1 Gbps)
MacAddr mac = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};
EthConfig config{.bandwidth_bps = 1000000000};
EthEndpoint eth(std::make_shared<ShmLink>(std::move(link)), config, 1, mac);

// Send Ethernet frame
Bytes payload = {0x01, 0x02, 0x03, 0x04};
Bytes frame = make_eth_frame(MAC_BROADCAST, mac, ETH_P_IP, payload);
eth.send_eth(frame);

// Receive Ethernet frames
eth.process();
auto result = eth.recv_eth();
if (result.is_ok()) {
    Bytes received = result.value();
    // Parse and process Ethernet frame
}
```

### Advanced Usage - Network Impairment Simulation

```cpp
// Configure realistic network conditions
LinkModel model{
    .base_latency_ns = 1000000,   // 1 ms base latency
    .jitter_ns = 200000,          // ±200 µs jitter
    .drop_prob = 0.05,            // 5% packet loss
    .duplicate_prob = 0.01,       // 1% duplication
    .corrupt_prob = 0.01,         // 1% corruption (bit flips)
    .bandwidth_bps = 1000000,     // 1 Mbps bandwidth limit
    .seed = 12345                 // Deterministic seed for reproducibility
};

// Create link with impairment model
auto link = ShmLink::create(String("impaired"), 64 * 1024, &model).value();

// All communication through this link will experience the configured impairments
// Perfect for testing error handling, retransmission logic, and timeout behavior
```

### Advanced Usage - Multi-Process Communication

```cpp
// Process A (Creator)
auto link_a = ShmLink::create(String("ipc_channel"), 64 * 1024).value();
SerialEndpoint serial_a(std::make_shared<ShmLink>(std::move(link_a)), config, 1);

// Process B (Attacher)
auto link_b = ShmLink::attach(String("ipc_channel")).value();
SerialEndpoint serial_b(std::make_shared<ShmLink>(std::move(link_b)), config, 2);

// Now both processes can communicate bidirectionally
// Process A sends to Process B
serial_a.send(data);

// Process B receives from Process A
serial_b.process();
auto result = serial_b.recv();
```

## Hardware Links (Linux)

Build with hardware support enabled (default since v0.0.9):

```bash
make reconfig && make build
```

To disable hardware support (simulation only):

```bash
NO_HARDWARE=1 make reconfig && NO_HARDWARE=1 make build
```

### PtyLink - Serial Bridge

Bridge to Linux pseudo-terminal devices for integration with real serial tools:

```cpp
#include <wirebit/serial/pty_link.hpp>

PtyConfig config{
    .create_symlink = true,
    .symlink_path = "/tmp/serial"
};

auto pty = PtyLink::create(config).value();
SerialEndpoint serial(std::make_shared<PtyLink>(std::move(pty)), serial_config, 1);

// Now connect with: screen /tmp/serial 115200
// Or: minicom -D /tmp/serial
// Or: picocom /tmp/serial -b 115200
```

### SocketCanLink - CAN Bridge

Bridge to Linux SocketCAN interfaces for integration with real CAN tools:

```cpp
#include <wirebit/can/socketcan_link.hpp>

SocketCanConfig config{
    .interface_name = "vcan0",
    .create_if_missing = true,
    .destroy_on_close = true
};

auto can_link = SocketCanLink::create(config).value();
CanEndpoint can(std::make_shared<SocketCanLink>(std::move(can_link)), can_config, 1);

// Monitor with: candump vcan0
// Send with: cansend vcan0 123#DEADBEEF
```

### TapLink - L2 Ethernet Bridge

Bridge to Linux TAP devices for L2 Ethernet frame injection:

```cpp
#include <wirebit/eth/tap_link.hpp>

TapConfig config{
    .interface_name = "tap0",
    .create_if_missing = true,
    .destroy_on_close = true,
    .owner_uid = getuid()
};

auto tap = TapLink::create(config).value();
EthEndpoint eth(std::make_shared<TapLink>(std::move(tap)), eth_config, 1, mac);

// Monitor with: sudo tcpdump -i tap0 -e -n
// Or: sudo wireshark -i tap0
```

### TunLink - L3 IP Bridge

Bridge to Linux TUN devices for L3 IP packet injection with ICMP responder:

```cpp
#include <wirebit/eth/tun_link.hpp>

TunConfig config{
    .interface_name = "tun0",
    .ip_address = "10.100.0.1/24",
    .create_if_missing = true,
    .destroy_on_close = true,
    .owner_uid = getuid()
};

auto tun = TunLink::create(config).value();

// Test with: ping 10.100.0.2 (your app responds to ICMP)
// Monitor with: sudo tcpdump -i tun0 -n
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

- **Serial Endpoint (UART/RS232)** - Configurable baud rates (9600-921600 bps), data bits (5-8), stop bits (1-2), parity (None/Even/Odd). Realistic byte-by-byte transmission timing with buffered reading.
  ```cpp
  SerialConfig config{.baud = 115200, .data_bits = 8, .stop_bits = 1, .parity = 'N'};
  ```

- **CAN Endpoint (SocketCAN-compatible)** - Standard (11-bit) and Extended (29-bit) IDs, RTR frames, configurable bitrates (125 kbps - 1 Mbps). Automatic frame timing with bit stuffing overhead. Broadcast nature simulation.
  ```cpp
  CanConfig config{.bitrate = 500000};
  auto frame = CanEndpoint::make_std_frame(0x123, data, 4);
  ```

- **Ethernet Endpoint (L2 Network)** - Raw L2 frame handling, MAC address filtering, configurable bandwidth (10 Mbps - 1 Gbps+), EtherType support (IPv4/IPv6/ARP/VLAN), promiscuous mode, automatic padding to minimum frame size (60 bytes).
  ```cpp
  EthConfig config{.bandwidth_bps = 1000000000};  // 1 Gbps
  ```

- **Hardware Links (Linux)** - PTY for serial tools (minicom, screen), SocketCAN for CAN tools (candump, cansend), TAP for L2 Ethernet (tcpdump, wireshark), TUN for L3 IP (ping, traceroute). Automatic interface creation and cleanup.

- **Shared Memory Transport** - Lock-free SPSC ring buffers, sub-microsecond latency (<1µs), zero syscalls in hot path (atomic operations only), configurable buffer sizes (64KB - 1MB typical), bidirectional communication.

- **Network Simulation (LinkModel)** - Configurable latency, jitter (uniform random), packet loss, duplication, corruption (bit flips), bandwidth limiting. Deterministic PRNG with seed for reproducible test scenarios.
  ```cpp
  LinkModel model{.base_latency_ns = 1000000, .jitter_ns = 200000, .drop_prob = 0.05, .seed = 42};
  ```

- **Type-Safe Error Handling** - Uses `datapod::Result<T, E>` for all fallible operations. No exceptions in hot path. Clear error types for debugging.

- **Multi-Process IPC** - Share communication channels between processes using shared memory. Creator/attacher pattern with automatic cleanup.

## Performance

Wirebit is designed for high-performance simulation with minimal overhead:

- **Hot path latency**: <1µs for ShmLink push/pop operations
- **Zero syscalls**: Hot path uses only atomic operations (no kernel involvement)
- **Lock-free**: SPSC ring buffers per direction, no mutex contention
- **Memory efficiency**: Configurable ring buffers (64KB - 1MB typical)
- **Threading model**: SPSC per direction, safe for concurrent access
- **Frame overhead**: 44-byte header per frame (magic, version, type, timestamps, IDs, length)

Benchmark results (typical x86_64 system):
- Serial endpoint: Accurate baud rate pacing within 1% of target
- CAN endpoint: Frame timing includes bit stuffing overhead
- Ethernet endpoint: Bandwidth shaping accurate to within 5% of target
- ShmLink: 500K+ frames/sec throughput per direction

## Examples

The `examples/` directory contains comprehensive demonstrations:

```bash
./build/serial_demo         # Serial communication scenarios (baud rates, parity)
./build/can_demo            # CAN bus examples (std/ext frames, RTR)
./build/ethernet_demo       # Ethernet L2 examples (MAC filtering, bandwidth)
./build/can_bus_hub         # Multi-node CAN hub simulation
./build/pty_demo            # PTY bridge demo (connect with screen/minicom)
./build/socketcan_demo      # SocketCAN bridge (requires hardware support)
./build/tap_demo            # TAP L2 bridge (requires hardware support)
./build/tun_demo            # TUN L3 bridge with ICMP responder (requires hardware support)
./build/link_model_demo     # Network impairment demonstration
./build/multi_process_demo  # Multi-process IPC example
```

Run examples:
```bash
make build
./build/serial_demo
```

## Testing

Wirebit includes comprehensive test coverage:

```bash
make build                    # Build (hardware support enabled by default)
NO_HARDWARE=1 make build      # Build simulation-only
make test                     # Run all tests
make test TEST=test_tap_link  # Run specific test
```

**Test Coverage:**
- 17 test suites
- 200+ assertions
- Unit tests: Frame encoding/decoding, link model behavior, ring buffer operations
- Integration tests: Serial timing accuracy, CAN broadcast, Ethernet bandwidth shaping
- Hardware tests: PTY/SocketCAN/TAP/TUN interface verification via `ip link`
- Multi-process tests: IPC communication scenarios

## Roadmap

- **v0.1** - SHM backend with Serial/CAN/Ethernet endpoints ✅
- **v0.2** - PTY backend for serial bridging ✅
- **v0.3** - SocketCAN backend for CAN bridging ✅
- **v0.6** - TAP backend for L2 Ethernet bridging ✅
- **v0.7** - TUN backend for L3 IP bridging ✅
- **v0.8** - Performance optimizations and benchmarking (in progress)
- **v0.9** - Additional protocol support (SPI, I2C)
- **v1.0** - Stable API, production-ready

## Dependencies

**Required:**
- **datapod** (v0.0.29+) - POD-friendly data structures (`Result`, `Error`, `Vector`, `String`, `RingBuffer`, `Stamp`)
- **echo** (main) - Logging with color support
- **C++20** - Modern C++ features (concepts, ranges, modules)

**Optional (for examples):**
- **rerun_sdk** - Visualization (via pkg-config)
- **optinum** (v0.0.16) - Optimization utilities
- **graphix** (v0.0.6) - Graphics utilities

**Testing:**
- **doctest** (v2.4.12) - Unit testing framework

**System Requirements (for hardware links):**
- Linux with `/dev/net/tun` support
- `sudo` or sudoers configuration for interface creation
- SocketCAN kernel module (`vcan`)

## License

MIT License - see [LICENSE](./LICENSE) for details.

## Acknowledgments

Made possible thanks to [these amazing projects](./ACKNOWLEDGMENTS.md).
