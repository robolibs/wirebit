# wirebit

Header-only C++ library for simulating hardware communication interfaces over shared memory.

## Development Status

See [TODO.md](./TODO.md) for the complete development plan and current progress.

## Overview

Wirebit provides a unified framework for simulating serial, CAN, and Ethernet communication interfaces in software. It enables multi-process testing and development of embedded systems without requiring physical hardware. All communication happens over shared memory with configurable network impairments (latency, jitter, packet loss, corruption) for realistic simulation.

The library is designed for robotics and embedded systems development where you need to test communication protocols, timing behavior, and error handling before deploying to hardware. It's particularly useful for CI/CD pipelines, integration testing, and development environments where hardware access is limited.

Key design principles:
- **Header-only**: No compilation required, just include and use
- **Zero-copy**: Shared memory ring buffers with atomic operations
- **Type-safe**: Uses datapod Result types for error handling, no exceptions in hot path
- **Deterministic**: Seeded PRNG for reproducible simulations
- **Multi-process**: Fork-based testing with proper IPC via ShmLink

### Architecture

```
┌─────────────────────────────────────────────────────────────────────────┐
│                            WIREBIT LIBRARY                               │
├──────────────────┬──────────────────┬──────────────────┬────────────────┤
│  SerialEndpoint  │   CanEndpoint    │  EthEndpoint     │   LinkModel    │
│                  │                  │                  │                │
│  ┌────────────┐  │  ┌────────────┐  │  ┌────────────┐  │  ┌──────────┐  │
│  │ Baud Rate  │  │  │ SocketCAN  │  │  │  L2 Frames │  │  │ Latency  │  │
│  │   Pacing   │  │  │ Compatible │  │  │  MAC Addr  │  │  │  Jitter  │  │
│  │  Framing   │  │  │ Arbitration│  │  │  Bandwidth │  │  │  Drops   │  │
│  └────────────┘  │  └────────────┘  │  └────────────┘  │  │ Corrupt  │  │
│        │         │        │         │        │         │  └──────────┘  │
│        └─────────┴────────┴─────────┴────────┘                │         │
│                          │                                     │         │
│                    ┌─────▼──────┐                       ┌──────▼──────┐  │
│                    │   Frame    │◄──────────────────────│  Simulator  │  │
│                    │  (Header)  │                       │   (PRNG)    │  │
│                    └─────┬──────┘                       └─────────────┘  │
│                          │                                               │
│                    ┌─────▼──────┐                                        │
│                    │  ShmLink   │                                        │
│                    │ (Bidir TX) │                                        │
│                    └─────┬──────┘                                        │
│                          │                                               │
│              ┌───────────┴───────────┐                                   │
│              │                       │                                   │
│        ┌─────▼──────┐          ┌────▼──────┐                            │
│        │ RingBuffer │          │ RingBuffer│                            │
│        │  (TX Dir)  │          │  (RX Dir) │                            │
│        │    SPSC    │          │    SPSC   │                            │
│        └─────┬──────┘          └────┬──────┘                            │
│              │                      │                                    │
└──────────────┼──────────────────────┼────────────────────────────────────┘
               │                      │
         ┌─────▼──────┐         ┌────▼──────┐
         │  Process A │         │ Process B │
         │  (Writer)  │         │ (Reader)  │
         └────────────┘         └───────────┘
```

**Data Flow:**
1. Application creates endpoint (Serial/CAN/Ethernet)
2. Endpoint wraps data in Frame with timing metadata
3. LinkModel applies network impairments (optional)
4. Frame pushed to ShmLink's bidirectional ring buffers
5. Receiver process pops frame, honors deliver_at_ns timing
6. Endpoint unwraps frame and returns protocol-specific data

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

// Create shared memory link
auto link_result = ShmLink::create(String("my_serial"), 64 * 1024);
if (!link_result.is_ok()) {
    echo::error("Failed to create link: ", link_result.error().message.c_str());
    return 1;
}
auto link = std::make_shared<ShmLink>(std::move(link_result.value()));

// Configure serial endpoint
SerialConfig config;
config.baud = 115200;
config.data_bits = 8;
config.stop_bits = 1;
config.parity = 'N';

SerialEndpoint serial(link, config, 1);

// Send data
Bytes data = {0x48, 0x65, 0x6C, 0x6C, 0x6F};  // "Hello"
auto send_result = serial.send(data);
if (!send_result.is_ok()) {
    echo::error("Send failed: ", send_result.error().message.c_str());
}

// Receive data (in another process)
auto recv_result = serial.recv();
if (recv_result.is_ok()) {
    Bytes received = recv_result.value();
    echo::info("Received ", received.size(), " bytes");
}
```

### CAN Bus with Hub

```cpp
#include <wirebit/wirebit.hpp>

using namespace wirebit;

// Node process
auto link_result = ShmLink::attach(String("can_node_0"));
auto link = std::make_shared<ShmLink>(std::move(link_result.value()));

CanConfig config;
config.bitrate = 500000;  // 500 kbps
CanEndpoint can(link, config, 0);

// Send CAN frame
can_frame frame;
frame.can_id = 0x123;
frame.can_dlc = 8;
std::memcpy(frame.data, "CANDATA", 7);

auto result = can.send_can(frame);

// Receive CAN frame
can_frame received;
auto recv_result = can.recv_can(received);
if (recv_result.is_ok()) {
    echo::info("CAN ID: 0x", std::hex, received.can_id);
}
```

**Run CAN bus hub:**
```bash
# Terminal 1: Start hub with 3 nodes
./can_bus_hub 3 500000

# Terminal 2: Node 0 sends frame
./can_node 0 send 0x123 "01 02 03 04"

# Terminal 3: Node 1 receives
./can_node 1 recv
```

### Ethernet L2 Frames

```cpp
#include <wirebit/wirebit.hpp>

using namespace wirebit;

auto link_result = ShmLink::create(String("eth_link"), 1024 * 1024);
auto link = std::make_shared<ShmLink>(std::move(link_result.value()));

// Create endpoint with MAC address
MacAddr mac = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};
EthConfig config;
config.bandwidth_bps = 1000000000;  // 1 Gbps

EthEndpoint eth(link, config, 1, mac);

// Create Ethernet frame
MacAddr dst = {0x02, 0x00, 0x00, 0x00, 0x00, 0x02};
Bytes payload = {0x01, 0x02, 0x03, 0x04};
Bytes frame = make_eth_frame(dst, mac, ETH_P_IP, payload);

// Send frame
auto result = eth.send_eth(frame);

// Receive frame
auto recv_result = eth.recv_eth();
if (recv_result.is_ok()) {
    Bytes received = recv_result.value();
    
    MacAddr dst_mac, src_mac;
    uint16_t ethertype;
    Bytes payload;
    parse_eth_frame(received, dst_mac, src_mac, ethertype, payload);
    
    echo::info("Ethernet frame: ", mac_to_string(src_mac).c_str(), 
               " -> ", mac_to_string(dst_mac).c_str());
}
```

### Network Impairment Simulation

```cpp
#include <wirebit/wirebit.hpp>

using namespace wirebit;

// Create link with network impairments
LinkModel model;
model.base_latency_ns = 1000000;    // 1 ms base latency
model.jitter_ns = 200000;           // 200 µs jitter
model.drop_prob = 0.05;             // 5% packet loss
model.corrupt_prob = 0.01;          // 1% corruption
model.bandwidth_bps = 10000000;     // 10 Mbps bandwidth limit
model.seed = 12345;                 // Deterministic PRNG

auto link_result = ShmLink::create(String("lossy_link"), 64 * 1024);
auto link = std::make_shared<ShmLink>(std::move(link_result.value()));
link->set_model(model);

// Now all communication through this link will experience:
// - 1ms ± 200µs latency
// - 5% random drops
// - 1% random corruption
// - 10 Mbps bandwidth shaping
// - Deterministic behavior (same seed = same results)
```

## Features

- **Serial Endpoint** - Byte-stream communication with accurate baud rate pacing
  ```cpp
  SerialConfig config{.baud = 115200, .data_bits = 8, .stop_bits = 1, .parity = 'N'};
  SerialEndpoint serial(link, config, endpoint_id);
  ```
  Supports 9600-921600 bps, configurable data bits (5-8), stop bits (1-2), and parity (N/E/O). Byte-by-byte framing with precise timing simulation.

- **CAN Endpoint** - SocketCAN-compatible CAN bus simulation
  ```cpp
  CanConfig config{.bitrate = 500000};  // 125k, 250k, 500k, 1M bps
  CanEndpoint can(link, config, endpoint_id);
  ```
  Compatible with Linux `can_frame` structure. Supports standard (11-bit) and extended (29-bit) IDs, RTR frames, and proper bit timing with stuffing overhead.

- **Ethernet Endpoint** - L2 Ethernet frame handling with MAC filtering
  ```cpp
  MacAddr mac = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};
  EthConfig config{.bandwidth_bps = 1000000000, .promiscuous = false};
  EthEndpoint eth(link, config, endpoint_id, mac);
  ```
  Full L2 frame support (dst/src MAC, EtherType, payload). Bandwidth shaping (10M/100M/1G bps), promiscuous mode, broadcast/unicast filtering. TAP-ready design.

- **Shared Memory Transport** - Lock-free SPSC ring buffers for zero-copy IPC
  ```cpp
  auto link = ShmLink::create(String("my_link"), 1024 * 1024);  // 1 MB
  ```
  Atomic operations only, no syscalls in hot path. Bidirectional communication with separate TX/RX rings. Typical latency <1µs for push/pop operations.

- **Network Impairment Simulation** - Configurable latency, jitter, drops, corruption
  ```cpp
  LinkModel model{
      .base_latency_ns = 1000000,  // 1 ms
      .jitter_ns = 200000,         // ±200 µs
      .drop_prob = 0.05,           // 5% loss
      .corrupt_prob = 0.01,        // 1% corruption
      .bandwidth_bps = 10000000,   // 10 Mbps
      .seed = 12345                // Deterministic
  };
  ```
  Deterministic PRNG for reproducible simulations. Statistical validation in integration tests. Perfect for CI/CD testing.

- **Type-Safe Error Handling** - datapod Result types, no exceptions
  ```cpp
  auto result = endpoint.send(data);
  if (!result.is_ok()) {
      echo::error("Send failed: ", result.error().message.c_str());
      return 1;
  }
  ```
  All fallible operations return `Result<T, Error>`. No exceptions thrown in hot path. RAII for resource management.

- **Multi-Process Testing** - Fork-based integration tests with proper IPC
  ```bash
  # Serial timing test (writer/reader processes)
  ./test_serial_timing 115200 100
  
  # CAN broadcast test (hub + N nodes)
  ./test_can_broadcast 3 10 0.05
  
  # Ethernet bandwidth test (sender/receiver)
  ./test_eth_bandwidth 100 50 1000
  ```
  Validates timing accuracy, frame integrity, and statistical bounds. All tests use fork() for true multi-process communication.

- **Comprehensive Examples** - 7+ examples demonstrating all features
  ```bash
  ./serial_demo    # 6 serial communication scenarios
  ./can_demo       # 7 CAN bus examples
  ./ethernet_demo  # 7 Ethernet L2 examples
  ./can_bus_hub    # Multi-node CAN hub application
  ./can_node       # CAN node client (send/recv/pingpong)
  ```
  Each example is fully documented with echo logging for visibility.

- **Built-in Logging** - echo library integration for visibility
  ```cpp
  echo::info("Operation started").green();
  echo::debug("Frame details: ", frame.size(), " bytes");
  echo::trace("Verbose internal state");
  echo::warn("Timing tolerance exceeded").yellow();
  echo::error("Critical failure").red().bold();
  ```
  Color-coded output, multiple log levels, zero overhead when disabled.

## Performance Characteristics

- **Hot path**: Zero syscalls (atomic operations only)
- **Latency**: <1µs for ShmLink push/pop operations
- **Throughput**: Limited by memory bandwidth, not library overhead
- **Memory**: Configurable ring buffer sizes (typically 64KB - 1MB)
- **Notification**: eventfd on empty→non-empty transitions only
- **Threading**: SPSC (single producer/consumer) per direction, no locks

## Dependencies

- **datapod** - POD-friendly data structures (Vector, String, Result, Array)
- **echo** - Logging library with color support
- **C++20** - Header-only, no compilation required

All dependencies are header-only and automatically fetched by CMake/XMake.

## Examples Directory

```
examples/
├── serial_demo.cpp      # 6 serial communication scenarios
├── can_demo.cpp         # 7 CAN bus examples
├── ethernet_demo.cpp    # 7 Ethernet L2 examples
├── can_bus_hub.cpp      # Multi-node CAN hub (standalone app)
├── can_node.cpp         # CAN node client (send/recv/pingpong)
├── main.cpp             # Simple hello world
└── ...
```

## Testing

```bash
# Build all tests
make build

# Run unit tests (9 test suites, 100% passing)
make test

# Run specific test
make test TEST=test_can

# Run integration tests
./build/linux/x86_64/release/test_serial_timing 115200 100
./build/linux/x86_64/release/test_can_broadcast 3 10 0.05
./build/linux/x86_64/release/test_eth_bandwidth 100 50 1000
```

**Test Coverage:**
- 9 unit test suites (frame, ring, serial, can, ethernet, model, shmlink, eventfd, shm_simple)
- 3 multi-process integration tests (serial timing, CAN broadcast, Ethernet bandwidth)
- 200+ assertions validating correctness
- Statistical validation of network impairments

## Roadmap

- **v0.1** - SHM backend with Serial/CAN/Ethernet endpoints ✅
- **v0.2** - PTY backend for real serial port bridging
- **v0.3** - SocketCAN backend for real CAN bus bridging
- **v0.4** - TAP backend for real Ethernet bridging
- **v0.5** - Performance optimizations and benchmarking suite

## License

MIT License - see [LICENSE](./LICENSE) for details.

## Acknowledgments

Made possible thanks to [these amazing projects](./ACKNOWLEDGMENTS.md).
