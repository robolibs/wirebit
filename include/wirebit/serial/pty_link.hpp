#pragma once

#ifdef HAS_HARDWARE

#include <wirebit/common/types.hpp>
#include <wirebit/link.hpp>

namespace wirebit {

    /// Configuration for PTY link
    struct PtyConfig {
        bool auto_destroy = true; ///< Automatically close PTY on destructor
    };

    /// Pseudo-terminal link for serial communication
    /// Bridges wirebit to real PTY devices (/dev/pts/X)
    /// External tools like minicom, picocom can connect to the slave PTY
    ///
    /// @note Requires HAS_HARDWARE compile flag
    /// @note NO sudo required - PTY is completely userspace
    class PtyLink : public Link {
      public:
        // TODO: Implementation in wirebit-rfl.2
    };

} // namespace wirebit

#endif // HAS_HARDWARE
