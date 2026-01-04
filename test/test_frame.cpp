#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include <wirebit/wirebit.hpp>

TEST_CASE("Frame creation and serialization") {
    SUBCASE("Create frame with payload") {
        wirebit::Bytes payload = {1, 2, 3, 4, 5};
        wirebit::Frame frame = wirebit::make_frame(wirebit::FrameType::SERIAL, payload);

        CHECK(frame.value.frame_type == wirebit::FrameType::SERIAL);
        CHECK(frame.value.payload.size() == 5);
        CHECK(frame.timestamp > 0); // Should have a valid timestamp
    }

    SUBCASE("Serialize and deserialize frame") {
        wirebit::Bytes payload = {0xDE, 0xAD, 0xBE, 0xEF};
        wirebit::Frame original = wirebit::make_frame(wirebit::FrameType::CAN, payload, wirebit::FrameId(42));

        auto serialized = wirebit::serialize_frame(original);
        CHECK(serialized.size() > 0);

        auto result = wirebit::deserialize_frame(serialized);
        REQUIRE(result.is_ok());

        auto &deserialized = result.value();
        CHECK(deserialized.value.frame_type == wirebit::FrameType::CAN);
        CHECK(deserialized.value.frame_id == 42);
        CHECK(deserialized.value.payload.size() == 4);
        CHECK(deserialized.value.payload[0] == 0xDE);
        CHECK(deserialized.value.payload[1] == 0xAD);
        CHECK(deserialized.value.payload[2] == 0xBE);
        CHECK(deserialized.value.payload[3] == 0xEF);
        CHECK(deserialized.timestamp == original.timestamp); // Timestamp preserved
    }

    SUBCASE("Deserialize invalid data") {
        wirebit::Bytes invalid_data = {1, 2, 3}; // Too small
        auto result = wirebit::deserialize_frame(invalid_data);
        CHECK(result.is_err());
    }

    SUBCASE("Frame with explicit timestamp") {
        wirebit::Bytes payload = {1, 2, 3};
        wirebit::TimeNs custom_ts = 123456789;
        wirebit::Frame frame = wirebit::make_frame(wirebit::FrameType::ETH, payload, custom_ts, wirebit::FrameId(99));

        CHECK(frame.timestamp == custom_ts);
        CHECK(frame.value.frame_id == 99);
        CHECK(frame.value.frame_type == wirebit::FrameType::ETH);
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

    SUBCASE("datapod::Stamp::now() integration") {
        auto t1 = wirebit::now_ns();
        auto t2 = datapod::Stamp<int>::now();
        // Should be very close (within 1ms)
        CHECK(std::abs(t2 - t1) < 1000000);
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
