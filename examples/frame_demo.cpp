/**
 * @file frame_demo.cpp
 * @brief Demonstrates advanced Frame usage with different protocols
 */

#include <echo/echo.hpp>
#include <wirebit/wirebit.hpp>

void demo_serial_frame() {
    echo::info("=== Serial Frame Demo ===").bold().cyan();

    // Create a serial frame with ASCII data
    wirebit::Bytes ascii_data = {'H', 'e', 'l', 'l', 'o', ' ', 'W', 'o', 'r', 'l', 'd', '!'};
    wirebit::Frame serial_frame = wirebit::make_frame(wirebit::FrameType::SERIAL, ascii_data,
                                                      1, // src_endpoint_id = UART1
                                                      2  // dst_endpoint_id = UART2
    );

    echo::info("Created serial frame:");
    echo::info("  Type: SERIAL");
    echo::info("  Source: UART", serial_frame.header.src_endpoint_id);
    echo::info("  Destination: UART", serial_frame.header.dst_endpoint_id);
    echo::info("  Payload: ", serial_frame.payload.size(), " bytes");
    echo::info("  Timestamp: ", serial_frame.header.tx_timestamp_ns, " ns");

    // Add metadata (e.g., baud rate, parity info)
    wirebit::Bytes metadata = {0x00, 0x25, 0x80, 0x00}; // 9600 baud, 8N1
    serial_frame.set_meta(metadata);
    echo::info("  Metadata: ", serial_frame.meta.size(), " bytes");

    // Encode and decode
    auto encoded = wirebit::encode_frame(serial_frame);
    echo::info("  Encoded size: ", encoded.size(), " bytes");

    auto decoded_result = wirebit::decode_frame(encoded);
    if (decoded_result.is_ok()) {
        auto decoded = std::move(decoded_result.value());
        echo::info("  Decode successful!").green();

        // Verify payload
        bool payload_match = true;
        for (size_t i = 0; i < decoded.payload.size(); ++i) {
            if (decoded.payload[i] != ascii_data[i]) {
                payload_match = false;
                break;
            }
        }
        echo::info("  Payload match: ", (payload_match ? "YES" : "NO"));
    }

    echo::info("");
}

void demo_can_frame() {
    echo::info("=== CAN Frame Demo ===").bold().cyan();

    // Create a CAN frame (typical automotive data)
    wirebit::Bytes can_data = {
        0x12, 0x34, 0x56, 0x78, // CAN ID + data
        0xAA, 0xBB, 0xCC, 0xDD  // More data
    };

    wirebit::Frame can_frame = wirebit::make_frame(wirebit::FrameType::CAN, can_data,
                                                   10, // src_endpoint_id = CAN controller 10
                                                   0   // dst_endpoint_id = 0 (broadcast)
    );

    echo::info("Created CAN frame:");
    echo::info("  Type: CAN");
    echo::info("  Source: CAN", can_frame.header.src_endpoint_id);
    echo::info("  Broadcast: ", (can_frame.is_broadcast() ? "YES" : "NO"));
    echo::info("  Payload: ", can_frame.payload.size(), " bytes");

    // Display payload in hex
    echo::info("  Data: ");
    for (auto byte : can_frame.payload) {
        echo::info("0x", std::hex, static_cast<int>(byte), " ");
    }
    echo::info("");

    echo::info("");
}

void demo_ethernet_frame() {
    echo::info("=== Ethernet Frame Demo ===").bold().cyan();

    // Create an Ethernet frame (simplified)
    wirebit::Bytes eth_data = {
        // Destination MAC (6 bytes)
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
        // Source MAC (6 bytes)
        0x00, 0x11, 0x22, 0x33, 0x44, 0x55,
        // EtherType (2 bytes) - 0x0800 = IPv4
        0x08, 0x00,
        // Payload (simplified)
        0x45, 0x00, 0x00, 0x54 // IPv4 header start
    };

    wirebit::Frame eth_frame = wirebit::make_frame(wirebit::FrameType::ETHERNET, eth_data,
                                                   100, // src_endpoint_id = eth0
                                                   200  // dst_endpoint_id = eth1
    );

    echo::info("Created Ethernet frame:");
    echo::info("  Type: ETHERNET");
    echo::info("  Source: eth", eth_frame.header.src_endpoint_id);
    echo::info("  Destination: eth", eth_frame.header.dst_endpoint_id);
    echo::info("  Payload: ", eth_frame.payload.size(), " bytes");
    echo::info("  Total frame size: ", eth_frame.total_size(), " bytes");

    echo::info("");
}

void demo_scheduled_delivery() {
    echo::info("=== Scheduled Delivery Demo ===").bold().cyan();

    // Create a frame with scheduled delivery time
    wirebit::Bytes data = {0x01, 0x02, 0x03};

    uint64_t now = wirebit::now_ns();
    uint64_t deliver_in_future = now + 1000000000; // Deliver in 1 second

    wirebit::Frame scheduled_frame = wirebit::make_frame_with_timestamps(wirebit::FrameType::SERIAL, data,
                                                                         now,               // tx_timestamp_ns
                                                                         deliver_in_future, // deliver_at_ns
                                                                         1,                 // src
                                                                         2                  // dst
    );

    echo::info("Created scheduled frame:");
    echo::info("  TX time: ", scheduled_frame.header.tx_timestamp_ns, " ns");
    echo::info("  Delivery time: ", scheduled_frame.header.deliver_at_ns, " ns");

    uint64_t delay = scheduled_frame.header.deliver_at_ns - scheduled_frame.header.tx_timestamp_ns;
    echo::info("  Scheduled delay: ", delay, " ns (", wirebit::ns_to_ms(delay), " ms)");

    echo::info("");
}

void demo_frame_validation() {
    echo::info("=== Frame Validation Demo ===").bold().cyan();

    // Create a valid frame
    wirebit::Bytes data = {0xDE, 0xAD, 0xBE, 0xEF};
    wirebit::Frame frame = wirebit::make_frame(wirebit::FrameType::CAN, data);
    auto encoded = wirebit::encode_frame(frame);

    // Validate header
    auto validate_result = wirebit::validate_frame_header(encoded);
    if (validate_result.is_ok()) {
        echo::info("Frame header validation: PASSED").green();
    } else {
        echo::error("Frame header validation: FAILED").red();
    }

    // Peek frame type without full decode
    auto type_result = wirebit::peek_frame_type(encoded);
    if (type_result.is_ok()) {
        auto frame_type = type_result.value();
        echo::info("Peeked frame type: ", static_cast<int>(frame_type));
    }

    // Test with corrupted data
    wirebit::Bytes corrupted = {0x01, 0x02, 0x03}; // Too small
    auto corrupt_validate = wirebit::validate_frame_header(corrupted);
    if (corrupt_validate.is_err()) {
        echo::info("Corrupted frame validation: FAILED (as expected)").green();
    }

    // Test with wrong magic number
    wirebit::Bytes wrong_magic = encoded;
    wrong_magic[0] = 0xFF; // Corrupt magic number
    auto wrong_validate = wirebit::validate_frame_header(wrong_magic);
    if (wrong_validate.is_err()) {
        echo::info("Wrong magic validation: FAILED (as expected)").green();
    }

    echo::info("");
}

void demo_frame_metadata() {
    echo::info("=== Frame Metadata Demo ===").bold().cyan();

    // Create frame with application-specific metadata
    wirebit::Bytes payload = {0x01, 0x02, 0x03, 0x04};
    wirebit::Frame frame = wirebit::make_frame(wirebit::FrameType::SERIAL, payload);

    // Add metadata: sequence number, checksum, flags
    wirebit::Bytes metadata = {
        0x00, 0x00, 0x00, 0x42, // Sequence number: 66
        0xAB, 0xCD,             // Checksum
        0x01                    // Flags: ACK required
    };
    frame.set_meta(metadata);

    echo::info("Frame with metadata:");
    echo::info("  Payload: ", frame.payload.size(), " bytes");
    echo::info("  Metadata: ", frame.meta.size(), " bytes");
    echo::info("  Total: ", frame.total_size(), " bytes");

    // Encode and decode to verify metadata preservation
    auto encoded = wirebit::encode_frame(frame);
    auto decoded_result = wirebit::decode_frame(encoded);

    if (decoded_result.is_ok()) {
        auto decoded = std::move(decoded_result.value());

        bool meta_match = true;
        if (decoded.meta.size() == metadata.size()) {
            for (size_t i = 0; i < metadata.size(); ++i) {
                if (decoded.meta[i] != metadata[i]) {
                    meta_match = false;
                    break;
                }
            }
        } else {
            meta_match = false;
        }

        echo::info("  Metadata preserved: ", (meta_match ? "YES" : "NO")).green();
    }

    echo::info("");
}

void demo_large_frame() {
    echo::info("=== Large Frame Demo ===").bold().cyan();

    // Create a large frame (e.g., file transfer)
    size_t large_size = 65536; // 64 KB
    wirebit::Bytes large_payload(large_size, 0xAA);

    // Fill with pattern
    for (size_t i = 0; i < large_size; ++i) {
        large_payload[i] = static_cast<wirebit::Byte>(i & 0xFF);
    }

    wirebit::Frame large_frame = wirebit::make_frame(wirebit::FrameType::ETHERNET, large_payload, 1, 2);

    echo::info("Created large frame:");
    echo::info("  Payload: ", large_frame.payload.size(), " bytes");
    echo::info("  Header: ", sizeof(wirebit::FrameHeader), " bytes");
    echo::info("  Total: ", large_frame.total_size(), " bytes");

    // Encode
    auto start = wirebit::now_ns();
    auto encoded = wirebit::encode_frame(large_frame);
    auto encode_time = wirebit::now_ns() - start;

    echo::info("  Encoded in: ", wirebit::ns_to_us(encode_time), " µs");
    echo::info("  Encoded size: ", encoded.size(), " bytes");

    // Decode
    start = wirebit::now_ns();
    auto decoded_result = wirebit::decode_frame(encoded);
    auto decode_time = wirebit::now_ns() - start;

    if (decoded_result.is_ok()) {
        echo::info("  Decoded in: ", wirebit::ns_to_us(decode_time), " µs").green();

        auto decoded = std::move(decoded_result.value());
        echo::info("  Payload size match: ", (decoded.payload.size() == large_size ? "YES" : "NO"));
    }

    echo::info("");
}

int main() {
    echo::info("╔════════════════════════════════════════╗").bold().cyan();
    echo::info("║   Wirebit Frame Demonstration         ║").bold().cyan();
    echo::info("╚════════════════════════════════════════╝").bold().cyan();
    echo::info("");

    demo_serial_frame();
    demo_can_frame();
    demo_ethernet_frame();
    demo_scheduled_delivery();
    demo_frame_validation();
    demo_frame_metadata();
    demo_large_frame();

    echo::info("╔════════════════════════════════════════╗").bold().green();
    echo::info("║   All demonstrations completed!        ║").bold().green();
    echo::info("╚════════════════════════════════════════╝").bold().green();

    return 0;
}
