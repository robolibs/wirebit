#pragma once

#include <string>

namespace isobus {

    class Isobus {
      public:
        Isobus() = default;
        ~Isobus() = default;

        std::string version() const;
    };

} // namespace isobus
