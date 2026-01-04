#include <chrono>
#include <doctest/doctest.h>
#include <thread>
#include <wirebit/wirebit.hpp>

TEST_CASE("Simple SHM test") {
    const char *shm_name = "/test_simple_shm";

    // Clean up any leftover SHM
    shm_unlink(shm_name);

    SUBCASE("Create and attach in same process") {
        // Create SHM
        auto create_result = wirebit::FrameRing::create_shm(wirebit::String(shm_name), 4096);
        REQUIRE(create_result.is_ok());

        // Attach to SHM
        auto attach_result = wirebit::FrameRing::attach_shm(wirebit::String(shm_name));
        REQUIRE(attach_result.is_ok());

        // Clean up
        shm_unlink(shm_name);
    }

    SUBCASE("Create, move, then attach") {
        // Create SHM
        auto create_result = wirebit::FrameRing::create_shm(wirebit::String(shm_name), 4096);
        REQUIRE(create_result.is_ok());

        // Move it
        auto ring = std::move(create_result.value());

        // Try to attach
        auto attach_result = wirebit::FrameRing::attach_shm(wirebit::String(shm_name));
        REQUIRE(attach_result.is_ok());

        // Clean up
        shm_unlink(shm_name);
    }
}
