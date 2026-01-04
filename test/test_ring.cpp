#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include <wirebit/wirebit.hpp>

TEST_CASE("FrameRing basic operations") {
    SUBCASE("Create frame ring") {
        auto result = wirebit::FrameRing::create(4096);
        REQUIRE(result.is_ok());

        auto ring = std::move(result.value());
        CHECK(ring.empty());
        CHECK_FALSE(ring.full());
        CHECK(ring.capacity() == 4096);
        CHECK(ring.size() == 0);
        CHECK(ring.available() == 4096);
        CHECK(ring.usage() == 0.0f);
    }

    SUBCASE("Push and pop single frame") {
        auto ring_result = wirebit::FrameRing::create(4096);
        REQUIRE(ring_result.is_ok());
        auto ring = std::move(ring_result.value());

        // Create a frame
        wirebit::Bytes payload = {1, 2, 3, 4, 5};
        wirebit::Frame frame(wirebit::FrameType::SERIAL, payload, 12345, 100);

        // Push frame
        auto push_result = ring.push_frame(frame);
        REQUIRE(push_result.is_ok());
        CHECK_FALSE(ring.empty());

        // Pop frame
        auto pop_result = ring.pop_frame();
        REQUIRE(pop_result.is_ok());

        auto popped = std::move(pop_result.value());
        CHECK(popped.header.frame_type == wirebit::FrameType::SERIAL);
        CHECK(popped.header.payload_len == 5);
        CHECK(popped.header.timestamp == 12345);
        CHECK(popped.header.frame_id == 100);
        CHECK(popped.payload.size() == 5);
        CHECK(popped.payload[0] == 1);
        CHECK(popped.payload[4] == 5);
        CHECK(ring.empty());
    }

    SUBCASE("Push and pop multiple frames") {
        auto ring_result = wirebit::FrameRing::create(4096);
        REQUIRE(ring_result.is_ok());
        auto ring = std::move(ring_result.value());

        // Push 10 frames
        for (int i = 0; i < 10; ++i) {
            wirebit::Bytes payload = {static_cast<wirebit::Byte>(i), static_cast<wirebit::Byte>(i + 1),
                                      static_cast<wirebit::Byte>(i + 2)};
            wirebit::Frame frame(wirebit::FrameType::CAN, payload, i * 1000, i);
            auto result = ring.push_frame(frame);
            REQUIRE(result.is_ok());
        }

        CHECK_FALSE(ring.empty());

        // Pop 10 frames
        for (int i = 0; i < 10; ++i) {
            auto result = ring.pop_frame();
            REQUIRE(result.is_ok());

            auto frame = std::move(result.value());
            CHECK(frame.header.frame_type == wirebit::FrameType::CAN);
            CHECK(frame.header.payload_len == 3);
            CHECK(frame.header.timestamp == static_cast<wirebit::TimeNs>(i * 1000));
            CHECK(frame.header.frame_id == static_cast<wirebit::FrameId>(i));
            CHECK(frame.payload[0] == static_cast<wirebit::Byte>(i));
        }

        CHECK(ring.empty());
    }

    SUBCASE("Empty frame (zero payload)") {
        auto ring_result = wirebit::FrameRing::create(4096);
        REQUIRE(ring_result.is_ok());
        auto ring = std::move(ring_result.value());

        wirebit::Frame frame(wirebit::FrameType::ETH, wirebit::Bytes{});
        auto push_result = ring.push_frame(frame);
        REQUIRE(push_result.is_ok());

        auto pop_result = ring.pop_frame();
        REQUIRE(pop_result.is_ok());

        auto popped = std::move(pop_result.value());
        CHECK(popped.header.frame_type == wirebit::FrameType::ETH);
        CHECK(popped.header.payload_len == 0);
        CHECK(popped.payload.empty());
    }

    SUBCASE("Large frame") {
        auto ring_result = wirebit::FrameRing::create(8192);
        REQUIRE(ring_result.is_ok());
        auto ring = std::move(ring_result.value());

        // Create a large payload (1000 bytes)
        wirebit::Bytes payload(1000);
        for (size_t i = 0; i < payload.size(); ++i) {
            payload[i] = static_cast<wirebit::Byte>(i % 256);
        }

        wirebit::Frame frame(wirebit::FrameType::SERIAL, payload);
        auto push_result = ring.push_frame(frame);
        REQUIRE(push_result.is_ok());

        auto pop_result = ring.pop_frame();
        REQUIRE(pop_result.is_ok());

        auto popped = std::move(pop_result.value());
        CHECK(popped.payload.size() == 1000);
        for (size_t i = 0; i < popped.payload.size(); ++i) {
            CHECK(popped.payload[i] == static_cast<wirebit::Byte>(i % 256));
        }
    }

    SUBCASE("Ring buffer full") {
        auto ring_result = wirebit::FrameRing::create(256); // Small ring
        REQUIRE(ring_result.is_ok());
        auto ring = std::move(ring_result.value());

        // Fill the ring
        int count = 0;
        while (true) {
            wirebit::Bytes payload = {1, 2, 3};
            wirebit::Frame frame(wirebit::FrameType::SERIAL, payload);
            auto result = ring.push_frame(frame);
            if (!result.is_ok()) {
                break;
            }
            count++;
        }

        CHECK(count > 0);
        CHECK(ring.usage() > 0.5f); // Should be at least 50% full

        // Pop one frame
        auto pop_result = ring.pop_frame();
        REQUIRE(pop_result.is_ok());

        // Should be able to push one more
        wirebit::Frame frame(wirebit::FrameType::SERIAL, wirebit::Bytes{1, 2, 3});
        auto push_result = ring.push_frame(frame);
        CHECK(push_result.is_ok());
    }

    SUBCASE("Pop from empty ring") {
        auto ring_result = wirebit::FrameRing::create(4096);
        REQUIRE(ring_result.is_ok());
        auto ring = std::move(ring_result.value());

        auto result = ring.pop_frame();
        CHECK(result.is_err());
        CHECK(result.error().code == wirebit::Error::TIMEOUT);
    }
}

TEST_CASE("FrameRing shared memory") {
    SUBCASE("Create SHM ring and use it") {
        const wirebit::String shm_name = "/wirebit_test_ring";

        // Clean up any existing SHM first
        shm_unlink(shm_name.c_str());

        // Create ring in SHM
        auto create_result = wirebit::FrameRing::create_shm(shm_name, 4096);
        REQUIRE(create_result.is_ok());
        auto ring = std::move(create_result.value());

        // Push a frame
        wirebit::Bytes payload = {0xDE, 0xAD, 0xBE, 0xEF};
        wirebit::Frame frame(wirebit::FrameType::CAN, payload, 99999, 42);
        auto push_result = ring.push_frame(frame);
        REQUIRE(push_result.is_ok());

        // Pop the frame
        auto pop_result = ring.pop_frame();
        REQUIRE(pop_result.is_ok());

        auto popped = std::move(pop_result.value());
        CHECK(popped.header.frame_type == wirebit::FrameType::CAN);
        CHECK(popped.header.timestamp == 99999);
        CHECK(popped.header.frame_id == 42);
        CHECK(popped.payload.size() == 4);
        CHECK(popped.payload[0] == 0xDE);
        CHECK(popped.payload[3] == 0xEF);

        // Clean up SHM (will be done by destructor, but explicit is fine)
        shm_unlink(shm_name.c_str());
    }
}
