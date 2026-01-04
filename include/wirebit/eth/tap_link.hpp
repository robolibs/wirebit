#pragma once

#ifdef HAS_HARDWARE

#include <wirebit/common/types.hpp>
#include <wirebit/link.hpp>

namespace wirebit {

    /// Configuration for TAP link
    struct TapConfig {
        String interface_name = "tap0"; ///< TAP interface name
        bool create_if_missing = true;  ///< Create interface if it doesn't exist (requires sudo)
        bool destroy_on_close = false;  ///< Destroy interface when link is closed
        bool set_up_on_create = true;   ///< Bring interface up after creation
    };

    /// TAP link for real network interface communication
    /// Bridges wirebit to Linux TAP devices for L2 Ethernet frames
    ///
    /// @note Requires HAS_HARDWARE compile flag
    /// @note Interface creation/deletion requires sudo (or sudoers config)
    class TapLink : public Link {
      public:
        // TODO: Implementation in wirebit-rfl.4
    };

} // namespace wirebit

#endif // HAS_HARDWARE
