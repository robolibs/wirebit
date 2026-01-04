#pragma once

#include <datapod/temporal/stamp.hpp>
#include <wirebit/common/types.hpp>

namespace wirebit {

    /// Get current time in nanoseconds since Unix epoch (uses datapod::Stamp::now())
    inline TimeNs now_ns() { return datapod::Stamp<int>::now(); }

    /// Convert nanoseconds to microseconds
    inline int64_t ns_to_us(TimeNs ns) { return ns / 1000; }

    /// Convert nanoseconds to milliseconds
    inline int64_t ns_to_ms(TimeNs ns) { return ns / 1000000; }

    /// Convert nanoseconds to seconds
    inline double ns_to_s(TimeNs ns) { return static_cast<double>(ns) / 1e9; }

    /// Convert microseconds to nanoseconds
    inline TimeNs us_to_ns(int64_t us) { return us * 1000; }

    /// Convert milliseconds to nanoseconds
    inline TimeNs ms_to_ns(int64_t ms) { return ms * 1000000; }

    /// Convert seconds to nanoseconds
    inline TimeNs s_to_ns(double s) { return static_cast<TimeNs>(s * 1e9); }

} // namespace wirebit
