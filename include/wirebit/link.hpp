#pragma once

#include <wirebit/frame.hpp>

namespace wirebit {

    /// Abstract interface for bidirectional communication links
    class Link {
      public:
        virtual ~Link() = default;

        /// Send a frame through the link
        /// @param frame Frame to send
        /// @return Result indicating success or error
        virtual Result<Unit, Error> send(const Frame &frame) = 0;

        /// Receive a frame from the link (non-blocking)
        /// @return Result containing frame if available, or error
        virtual Result<Frame, Error> recv() = 0;

        /// Check if link is ready for sending
        /// @return true if link can accept more frames
        virtual bool can_send() const = 0;

        /// Check if link has frames available for receiving
        /// @return true if frames are available
        virtual bool can_recv() const = 0;

        /// Get link name/identifier
        /// @return Link name
        virtual String name() const = 0;
    };

} // namespace wirebit
