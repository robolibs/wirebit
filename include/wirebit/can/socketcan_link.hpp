#pragma once

#ifdef HAS_HARDWARE

#include <wirebit/common/types.hpp>
#include <wirebit/link.hpp>

namespace wirebit {

    /// Configuration for SocketCAN link
    struct SocketCanConfig {
        String interface_name = "vcan0"; ///< CAN interface name (vcan0, can0, etc.)
        bool create_if_missing = true;   ///< Create interface if it doesn't exist (requires sudo)
        bool destroy_on_close = false;   ///< Destroy interface when link is closed
    };

    /// SocketCAN link for real/virtual CAN bus communication
    /// Bridges wirebit to Linux SocketCAN interfaces
    ///
    /// @note Requires HAS_HARDWARE compile flag
    /// @note Interface creation/deletion requires sudo (or sudoers config)
    class SocketCanLink : public Link {
      public:
        // TODO: Implementation in wirebit-rfl.3
    };

} // namespace wirebit

#endif // HAS_HARDWARE
