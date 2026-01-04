#include <echo/echo.hpp>
#include <wirebit/wirebit.hpp>

int main() {
    echo::print("Wirebit v", wirebit::VERSION).green().bold();
    echo::info("Header-only library initialized successfully");

    // Test basic types
    wirebit::Bytes payload = {1, 2, 3, 4, 5};
    wirebit::Frame frame = wirebit::make_frame(wirebit::FrameType::SERIAL, payload);
    echo::debug("Created frame with ", frame.value.payload.size(), " bytes");
    echo::debug("Frame timestamp: ", frame.timestamp, " ns");
    echo::debug("Frame ID: ", frame.value.frame_id);

    // Test serialization
    auto serialized = wirebit::serialize_frame(frame);
    echo::debug("Serialized frame size: ", serialized.size(), " bytes");

    // Test deserialization
    auto result = wirebit::deserialize_frame(serialized);
    if (result.is_ok()) {
        echo::info("Frame deserialization successful").green();
        auto &deserialized = result.value();
        echo::debug("Deserialized payload size: ", deserialized.value.payload.size());
        echo::debug("Deserialized timestamp: ", deserialized.timestamp);
        echo::debug("Timestamps match: ", (deserialized.timestamp == frame.timestamp ? "yes" : "no"));
    } else {
        echo::error("Frame deserialization failed: ", result.error().message.c_str()).red();
    }

    echo::print("All basic tests passed!").green().bold();
    return 0;
}
