#include <doctest/doctest.h>
#include <isobus/isobus.hpp>

TEST_CASE("Isobus version") {
    isobus::Isobus bus;
    CHECK(bus.version() == "0.0.1");
}
