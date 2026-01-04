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
        wirebit::Frame frame =
            wirebit::make_frame_with_timestamps(wirebit::FrameType::SERIAL, payload, 12345, 0, 100, 200);

        // Push frame
        auto push_result = ring.push_frame(frame);
        REQUIRE(push_result.is_ok());
        CHECK_FALSE(ring.empty());

        // Pop frame
        auto pop_result = ring.pop_frame();
        REQUIRE(pop_result.is_ok());

        auto popped = std::move(pop_result.value());
        CHECK(popped.type() == wirebit::FrameType::SERIAL);
        CHECK(popped.payload.size() == 5);

        uint64_t ts = popped.header.tx_timestamp_ns;
        CHECK(ts == 12345);

        uint32_t src = popped.header.src_endpoint_id;
        CHECK(src == 100);

        uint32_t dst = popped.header.dst_endpoint_id;
        CHECK(dst == 200);

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
            wirebit::Frame frame =
                wirebit::make_frame_with_timestamps(wirebit::FrameType::CAN, payload, i * 1000, 0, i, 0);
            auto result = ring.push_frame(frame);
            REQUIRE(result.is_ok());
        }

        CHECK_FALSE(ring.empty());

        // Pop 10 frames
        for (int i = 0; i < 10; ++i) {
            auto result = ring.pop_frame();
            REQUIRE(result.is_ok());

            auto frame = std::move(result.value());
            CHECK(frame.type() == wirebit::FrameType::CAN);
            CHECK(frame.payload.size() == 3);

            uint64_t ts = frame.header.tx_timestamp_ns;
            CHECK(ts == static_cast<uint64_t>(i * 1000));

            uint32_t src = frame.header.src_endpoint_id;
            CHECK(src == static_cast<uint32_t>(i));

            CHECK(frame.payload[0] == static_cast<wirebit::Byte>(i));
        }

        CHECK(ring.empty());
    }

    SUBCASE("Empty frame (zero payload)") {
        auto ring_result = wirebit::FrameRing::create(4096);
        REQUIRE(ring_result.is_ok());
        auto ring = std::move(ring_result.value());

        wirebit::Frame frame = wirebit::make_frame(wirebit::FrameType::ETHERNET, wirebit::Bytes{});
        auto push_result = ring.push_frame(frame);
        REQUIRE(push_result.is_ok());

        auto pop_result = ring.pop_frame();
        REQUIRE(pop_result.is_ok());

        auto popped = std::move(pop_result.value());
        CHECK(popped.type() == wirebit::FrameType::ETHERNET);
        CHECK(popped.payload.empty());
    }

    SUBCASE("Large frame") {
        auto ring_result = wirebit::FrameRing::create(8192);
        REQUIRE(ring_result.is_ok());
        auto ring = std::move(ring_result.value());

        wirebit::Bytes large_payload(1000, 0xAA);
        wirebit::Frame frame = wirebit::make_frame(wirebit::FrameType::SERIAL, large_payload);
        auto push_result = ring.push_frame(frame);
        REQUIRE(push_result.is_ok());

        auto pop_result = ring.pop_frame();
        REQUIRE(pop_result.is_ok());

        auto popped = std::move(pop_result.value());
        CHECK(popped.payload.size() == 1000);
        CHECK(popped.payload[0] == 0xAA);
        CHECK(popped.payload[999] == 0xAA);
    }

    SUBCASE("Ring buffer full") {
        auto ring_result = wirebit::FrameRing::create(512);
        REQUIRE(ring_result.is_ok());
        auto ring = std::move(ring_result.value());

        // Fill the ring
        int frames_pushed = 0;
        for (int i = 0; i < 100; ++i) {
            wirebit::Bytes payload(50, static_cast<wirebit::Byte>(i));
            wirebit::Frame frame = wirebit::make_frame(wirebit::FrameType::CAN, payload);
            auto result = ring.push_frame(frame);
            if (result.is_ok()) {
                frames_pushed++;
            } else {
                break;
            }
        }

        CHECK(frames_pushed > 0);
        bool is_full_or_high_usage = ring.full() || ring.usage() > 0.8f;
        CHECK(is_full_or_high_usage);
    }

    SUBCASE("Pop from empty ring") {
        auto ring_result = wirebit::FrameRing::create(4096);
        REQUIRE(ring_result.is_ok());
        auto ring = std::move(ring_result.value());

        auto result = ring.pop_frame();
        CHECK(result.is_err());
    }
}

TEST_CASE("FrameRing with shared memory") {
    SUBCASE("Create ring in shared memory") {
        wirebit::String shm_name = "/wirebit_test_ring";
        auto ring_result = wirebit::FrameRing::create_shm(shm_name, 4096);
        REQUIRE(ring_result.is_ok());

        auto ring = std::move(ring_result.value());
        CHECK(ring.capacity() == 4096);
        CHECK(ring.empty());

        // Push a frame
        wirebit::Bytes payload = {0xDE, 0xAD, 0xBE, 0xEF};
        wirebit::Frame frame = wirebit::make_frame_with_timestamps(wirebit::FrameType::CAN, payload, 99999, 0, 42, 0);
        auto push_result = ring.push_frame(frame);
        REQUIRE(push_result.is_ok());

        // Pop the frame
        auto pop_result = ring.pop_frame();
        REQUIRE(pop_result.is_ok());

        auto popped = std::move(pop_result.value());
        CHECK(popped.type() == wirebit::FrameType::CAN);

        uint64_t ts = popped.header.tx_timestamp_ns;
        CHECK(ts == 99999);

        uint32_t src = popped.header.src_endpoint_id;
        CHECK(src == 42);

        CHECK(popped.payload.size() == 4);
        CHECK(popped.payload[0] == 0xDE);
        CHECK(popped.payload[3] == 0xEF);

        // Clean up SHM (will be done by destructor, but explicit is fine)
        shm_unlink(shm_name.c_str());
    }
}

TEST_CASE("FrameRing stress test") {
    SUBCASE("Many small frames") {
        auto ring_result = wirebit::FrameRing::create(16384);
        REQUIRE(ring_result.is_ok());
        auto ring = std::move(ring_result.value());

        const int num_frames = 100;
        for (int i = 0; i < num_frames; ++i) {
            wirebit::Bytes payload = {static_cast<wirebit::Byte>(i & 0xFF)};
            wirebit::Frame frame = wirebit::make_frame_with_timestamps(wirebit::FrameType::SERIAL, payload, i, 0, i, 0);
            auto result = ring.push_frame(frame);
            REQUIRE(result.is_ok());
        }

        for (int i = 0; i < num_frames; ++i) {
            auto result = ring.pop_frame();
            REQUIRE(result.is_ok());

            auto frame = std::move(result.value());
            uint64_t ts = frame.header.tx_timestamp_ns;
            CHECK(ts == static_cast<uint64_t>(i));

            CHECK(frame.payload[0] == static_cast<wirebit::Byte>(i & 0xFF));
        }

        CHECK(ring.empty());
    }
}
