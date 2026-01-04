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

// Protocol endpoints
#include <wirebit/can/can_endpoint.hpp>
#include <wirebit/eth/eth_endpoint.hpp>
#include <wirebit/serial/serial_endpoint.hpp>

namespace wirebit {

    // Library version
    constexpr const char *VERSION = "0.0.1";

} // namespace wirebit
