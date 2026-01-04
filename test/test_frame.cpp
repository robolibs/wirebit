#include <doctest/doctest.h>
#include <wirebit/wirebit.hpp>

TEST_CASE("Frame creation and serialization") {
    SUBCASE("Create frame with payload") {
        wirebit::Bytes payload = {1, 2, 3, 4, 5};
        wirebit::Frame frame = wirebit::make_frame(wirebit::FrameType::SERIAL, payload);

        CHECK(frame.type() == wirebit::FrameType::SERIAL);
        CHECK(frame.payload.size() == 5);

        // Copy packed field to avoid reference binding issue
        uint64_t ts = frame.header.tx_timestamp_ns;
        CHECK(ts > 0); // Should have a valid timestamp

        uint32_t magic = frame.header.magic;
        CHECK(magic == 0x57424954);

        uint16_t version = frame.header.version;
        CHECK(version == 1);
    }

    SUBCASE("Serialize and deserialize frame") {
        wirebit::Bytes payload = {0xDE, 0xAD, 0xBE, 0xEF};
        wirebit::Frame original = wirebit::make_frame(wirebit::FrameType::CAN, payload, 42, 0);

        auto serialized = wirebit::encode_frame(original);
        CHECK(serialized.size() > 0);

        auto result = wirebit::decode_frame(serialized);
        REQUIRE(result.is_ok());

        auto deserialized = std::move(result.value());
        CHECK(deserialized.type() == wirebit::FrameType::CAN);

        uint32_t src_id = deserialized.header.src_endpoint_id;
        CHECK(src_id == 42);

        CHECK(deserialized.payload.size() == 4);
        CHECK(deserialized.payload[0] == 0xDE);
        CHECK(deserialized.payload[1] == 0xAD);
        CHECK(deserialized.payload[2] == 0xBE);
        CHECK(deserialized.payload[3] == 0xEF);

        uint64_t orig_ts = original.header.tx_timestamp_ns;
        uint64_t deser_ts = deserialized.header.tx_timestamp_ns;
        CHECK(deser_ts == orig_ts); // Timestamp preserved
    }

    SUBCASE("Deserialize invalid data") {
        wirebit::Bytes invalid_data = {1, 2, 3}; // Too small
        auto result = wirebit::decode_frame(invalid_data);
        CHECK(result.is_err());
    }

    SUBCASE("Frame with explicit timestamp") {
        wirebit::Bytes payload = {1, 2, 3};
        uint64_t custom_ts = 123456789;
        wirebit::Frame frame =
            wirebit::make_frame_with_timestamps(wirebit::FrameType::ETHERNET, payload, custom_ts, 0, 99, 0);

        uint64_t ts = frame.header.tx_timestamp_ns;
        CHECK(ts == custom_ts);

        uint32_t src_id = frame.header.src_endpoint_id;
        CHECK(src_id == 99);

        CHECK(frame.type() == wirebit::FrameType::ETHERNET);
    }
}

TEST_CASE("Time utilities") {
    SUBCASE("Time conversions") {
        wirebit::TimeNs ns = 1000000000; // 1 second

        CHECK(wirebit::ns_to_us(ns) == 1000000);
        CHECK(wirebit::ns_to_ms(ns) == 1000);
        CHECK(wirebit::ns_to_s(ns) == 1.0);

        CHECK(wirebit::us_to_ns(1000000) == 1000000000);
        CHECK(wirebit::ms_to_ns(1000) == 1000000000);
        CHECK(wirebit::s_to_ns(1.0) == 1000000000);
    }

    SUBCASE("now_ns returns valid timestamp") {
        auto t1 = wirebit::now_ns();
        auto t2 = wirebit::now_ns();
        CHECK(t2 >= t1); // Time should be monotonic
    }
}

TEST_CASE("LinkModel") {
    SUBCASE("Default model is deterministic") {
        wirebit::LinkModel model;
        CHECK(model.is_deterministic());
        CHECK_FALSE(model.has_bandwidth_limit());
    }

    SUBCASE("Model with parameters") {
        wirebit::LinkModel model(1000.0, 100.0, 50.0, 0.01, 0.001, 1000000);
        CHECK_FALSE(model.is_deterministic());
        CHECK(model.has_bandwidth_limit());
    }

    SUBCASE("Transmission delay calculation") {
        wirebit::LinkModel model(0, 0, 0, 0, 0, 1000000); // 1 Mbps
        size_t data_size = 1000;                          // 1000 bytes = 8000 bits
        auto delay = wirebit::compute_transmission_delay(model, data_size);
        // 8000 bits / 1000000 bps = 0.008 seconds = 8000000 ns
        CHECK(delay == 8000000);
    }
}
