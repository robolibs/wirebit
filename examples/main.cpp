#include <echo/echo.hpp>
#include <wirebit/wirebit.hpp>

int main() {
    echo::info("Wirebit Example").bold().cyan();

    // Create a simple frame
    wirebit::Bytes payload = {0x01, 0x02, 0x03, 0x04, 0x05};
    wirebit::Frame frame = wirebit::make_frame(wirebit::FrameType::SERIAL, payload, 1, 2);

    echo::debug("Created frame with ", frame.payload.size(), " bytes");
    echo::debug("Frame timestamp: ", frame.header.tx_timestamp_ns, " ns");
    echo::debug("Frame type: ", static_cast<int>(frame.type()));
    echo::debug("Source endpoint: ", frame.header.src_endpoint_id);
    echo::debug("Destination endpoint: ", frame.header.dst_endpoint_id);

    // Serialize and deserialize
    auto serialized = wirebit::encode_frame(frame);
    echo::debug("Serialized frame: ", serialized.size(), " bytes");

    auto result = wirebit::decode_frame(serialized);
    if (result.is_ok()) {
        auto deserialized = std::move(result.value());
        echo::info("Frame deserialized successfully").green();
        echo::debug("Payload size: ", deserialized.payload.size());
        echo::debug("Timestamps match: ",
                    (deserialized.header.tx_timestamp_ns == frame.header.tx_timestamp_ns ? "yes" : "no"));
    } else {
        echo::error("Failed to deserialize frame").red();
        return 1;
    }

    // Test ring buffer
    echo::info("Testing FrameRing...").cyan();
    auto ring_result = wirebit::FrameRing::create(4096);
    if (!ring_result.is_ok()) {
        echo::error("Failed to create ring buffer").red();
        return 1;
    }

    auto ring = std::move(ring_result.value());
    echo::debug("Ring capacity: ", ring.capacity(), " bytes");

    // Push frame
    auto push_result = ring.push_frame(frame);
    if (push_result.is_ok()) {
        echo::info("Frame pushed to ring").green();
    } else {
        echo::error("Failed to push frame").red();
        return 1;
    }

    // Pop frame
    auto pop_result = ring.pop_frame();
    if (pop_result.is_ok()) {
        auto popped = std::move(pop_result.value());
        echo::info("Frame popped from ring").green();
        echo::debug("Popped payload size: ", popped.payload.size());
    } else {
        echo::error("Failed to pop frame").red();
        return 1;
    }

    echo::info("All tests passed!").bold().green();
    return 0;
}
