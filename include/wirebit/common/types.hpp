#pragma once

#include <cstdint>
#include <datapod/adapters/error.hpp>
#include <datapod/adapters/result.hpp>
#include <datapod/sequential/string.hpp>
#include <datapod/sequential/vector.hpp>

namespace wirebit {

    // Unit type for Result<Unit, Error> (equivalent to Result<void, Error>)
    struct Unit {
        auto members() noexcept { return std::tie(); }
        auto members() const noexcept { return std::tie(); }
    };

    // Datapod type aliases for convenience
    template <typename T, typename E = datapod::Error> using Result = datapod::Result<T, E>;

    using Error = datapod::Error;

    template <typename T> using Vector = datapod::Vector<T>;

    using String = datapod::String;

    // Common types
    using Byte = uint8_t;
    using Bytes = Vector<Byte>;

    // Time type (nanoseconds since Unix epoch) - compatible with datapod::Stamp
    using TimeNs = int64_t;

    // Frame ID type
    using FrameId = uint64_t;

} // namespace wirebit
