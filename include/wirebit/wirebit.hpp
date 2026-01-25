#pragma once

// Main wirebit header - includes all components

// Common types and utilities
#include <wirebit/common/time.hpp>
#include <wirebit/common/types.hpp>

// Core abstractions
#include <wirebit/endpoint.hpp>
#include <wirebit/frame.hpp>
#include <wirebit/link.hpp>
#include <wirebit/model.hpp>

// Shared memory implementation
#include <wirebit/shm/handshake.hpp>
#include <wirebit/shm/ring.hpp>
#include <wirebit/shm/shm_link.hpp>

// Hardware interface links (enabled by default, use NO_HARDWARE to disable)
// IMPORTANT: Include these BEFORE eth_endpoint.hpp so system headers
// are included first, then eth_endpoint.hpp can #undef conflicting macros
#ifndef NO_HARDWARE
#include <wirebit/can/socketcan_link.hpp>
#include <wirebit/eth/tap_link.hpp>
#include <wirebit/eth/tun_link.hpp>
#include <wirebit/serial/pty_link.hpp>
#include <wirebit/serial/tty_link.hpp>
#endif // NO_HARDWARE

// Protocol endpoints
// eth_endpoint.hpp must come after hardware headers to #undef system macros
#include <wirebit/can/can_endpoint.hpp>
#include <wirebit/eth/eth_endpoint.hpp>
#include <wirebit/serial/serial_endpoint.hpp>

namespace wirebit {

    // Library version
    constexpr const char *VERSION = "0.0.1";

} // namespace wirebit
