#pragma once

#include <wirebit/frame.hpp>
#include <wirebit/link.hpp>

namespace wirebit {

    /// Abstract interface for protocol-specific endpoints
    class Endpoint {
      public:
        virtual ~Endpoint() = default;

        /// Send protocol-specific data through the endpoint
        /// @param data Protocol-specific data to send
        /// @return Result indicating success or error
        virtual Result<Unit, Error> send(const Bytes &data) = 0;

        /// Receive protocol-specific data from the endpoint (non-blocking)
        /// @return Result containing data if available, or error
        virtual Result<Bytes, Error> recv() = 0;

        /// Process incoming frames from the link
        /// This is called internally to convert frames to protocol-specific data
        /// @return Result indicating success or error
        virtual Result<Unit, Error> process() = 0;

        /// Get endpoint name/identifier
        /// @return Endpoint name
        virtual String name() const = 0;

        /// Get the underlying link
        /// @return Pointer to the link
        virtual Link *link() = 0;
    };

} // namespace wirebit
