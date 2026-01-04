/// CAN Endpoint Demo
/// Demonstrates CAN bus communication simulation with standard/extended frames and different bitrates

#include <iomanip>
#include <iostream>
#include <memory>
#include <wirebit/wirebit.hpp>

using namespace wirebit;

/// Example 1: Basic CAN communication
void example_basic_can() {
    std::cout << "\n=== Example 1: Basic CAN Communication ===" << std::endl;

    auto server_result = ShmLink::create(String("can_basic"), 8192);
    if (!server_result.is_ok())
        return;
    auto server_link = std::make_shared<ShmLink>(std::move(server_result.value()));

    auto client_result = ShmLink::attach(String("can_basic"));
    if (!client_result.is_ok())
        return;
    auto client_link = std::make_shared<ShmLink>(std::move(client_result.value()));

    CanConfig config;
    config.bitrate = 500000; // 500 kbps (standard CAN)

    CanEndpoint node1(server_link, config, 1);
    CanEndpoint node2(client_link, config, 2);

    // Send standard CAN frame
    uint8_t data[] = {0x01, 0x02, 0x03, 0x04};
    auto frame = CanEndpoint::make_std_frame(0x123, data, 4);

    std::cout << "Node 1 sends: ID=0x" << std::hex << frame.can_id << std::dec << " DLC=" << (int)frame.can_dlc
              << std::endl;

    node1.send_can(frame);

    // Receive frame
    node2.process();
    can_frame received;
    if (node2.recv_can(received).is_ok()) {
        std::cout << "Node 2 received: ID=0x" << std::hex << received.can_id << std::dec
                  << " DLC=" << (int)received.can_dlc << " Data: ";
        for (int i = 0; i < received.can_dlc; ++i) {
            std::cout << std::hex << std::setfill('0') << std::setw(2) << (int)received.data[i] << " ";
        }
        std::cout << std::dec << std::endl;
    }
}

/// Example 2: Standard vs Extended frames
void example_std_vs_ext() {
    std::cout << "\n=== Example 2: Standard vs Extended Frames ===" << std::endl;

    auto link_result = ShmLink::create(String("can_frames"), 4096);
    if (!link_result.is_ok())
        return;
    auto link = std::make_shared<ShmLink>(std::move(link_result.value()));

    CanConfig config;
    CanEndpoint endpoint(link, config, 1);

    // Standard frame (11-bit ID)
    uint8_t std_data[] = {0xAA, 0xBB};
    auto std_frame = CanEndpoint::make_std_frame(0x7FF, std_data, 2); // Max 11-bit ID

    std::cout << "Standard frame:" << std::endl;
    std::cout << "  ID: 0x" << std::hex << std::setw(3) << std::setfill('0') << (std_frame.can_id & CAN_SFF_MASK)
              << std::dec << " (11-bit)" << std::endl;
    std::cout << "  Extended: " << ((std_frame.can_id & CAN_EFF_FLAG) ? "Yes" : "No") << std::endl;

    // Extended frame (29-bit ID)
    uint8_t ext_data[] = {0x11, 0x22, 0x33, 0x44};
    auto ext_frame = CanEndpoint::make_ext_frame(0x1FFFFFFF, ext_data, 4); // Max 29-bit ID

    std::cout << "\nExtended frame:" << std::endl;
    std::cout << "  ID: 0x" << std::hex << std::setw(8) << std::setfill('0') << (ext_frame.can_id & CAN_EFF_MASK)
              << std::dec << " (29-bit)" << std::endl;
    std::cout << "  Extended: " << ((ext_frame.can_id & CAN_EFF_FLAG) ? "Yes" : "No") << std::endl;
}

/// Example 3: RTR (Remote Transmission Request) frames
void example_rtr() {
    std::cout << "\n=== Example 3: RTR Frames ===" << std::endl;

    auto link_result = ShmLink::create(String("can_rtr"), 4096);
    if (!link_result.is_ok())
        return;
    auto link = std::make_shared<ShmLink>(std::move(link_result.value()));

    CanConfig config;
    CanEndpoint endpoint(link, config, 1);

    // Standard RTR
    auto rtr_std = CanEndpoint::make_rtr_frame(0x100, false);
    std::cout << "Standard RTR: ID=0x" << std::hex << (rtr_std.can_id & CAN_SFF_MASK) << std::dec << std::endl;
    std::cout << "  RTR flag: " << ((rtr_std.can_id & CAN_RTR_FLAG) ? "Set" : "Clear") << std::endl;

    // Extended RTR
    auto rtr_ext = CanEndpoint::make_rtr_frame(0x1000000, true);
    std::cout << "\nExtended RTR: ID=0x" << std::hex << (rtr_ext.can_id & CAN_EFF_MASK) << std::dec << std::endl;
    std::cout << "  RTR flag: " << ((rtr_ext.can_id & CAN_RTR_FLAG) ? "Set" : "Clear") << std::endl;
    std::cout << "  Extended flag: " << ((rtr_ext.can_id & CAN_EFF_FLAG) ? "Set" : "Clear") << std::endl;
}

/// Example 4: Different CAN bitrates
void example_bitrates() {
    std::cout << "\n=== Example 4: Different CAN Bitrates ===" << std::endl;

    struct BitrateTest {
        uint32_t bitrate;
        const char *name;
    };

    BitrateTest tests[] = {
        {125000, "125 kbps (Low-speed CAN)"},
        {250000, "250 kbps"},
        {500000, "500 kbps (Standard)"},
        {1000000, "1 Mbps (CAN FD capable)"},
    };

    for (const auto &test : tests) {
        std::cout << "\n" << test.name << std::endl;

        auto link_result = ShmLink::create(String("can_bitrate"), 4096);
        if (!link_result.is_ok())
            continue;
        auto link = std::make_shared<ShmLink>(std::move(link_result.value()));

        CanConfig config;
        config.bitrate = test.bitrate;
        CanEndpoint endpoint(link, config, 1);

        // Send 8-byte frame
        uint8_t data[8] = {0, 1, 2, 3, 4, 5, 6, 7};
        auto frame = CanEndpoint::make_std_frame(0x200, data, 8);

        // Calculate frame time
        uint32_t overhead_bits = 47; // Standard frame overhead
        uint32_t data_bits = 8 * 8;  // 8 bytes
        uint32_t total_bits = overhead_bits + data_bits;
        total_bits += total_bits / 5; // Bit stuffing

        uint64_t frame_time_us = (total_bits * 1000000ULL) / test.bitrate;

        std::cout << "  Frame time: " << frame_time_us << " µs (" << total_bits << " bits)" << std::endl;
    }
}

/// Example 5: Multi-node CAN bus
void example_multi_node() {
    std::cout << "\n=== Example 5: Multi-Node CAN Bus ===" << std::endl;

    // Create a shared bus (in real CAN, all nodes share the same bus)
    auto bus_result = ShmLink::create(String("can_bus"), 16384);
    if (!bus_result.is_ok())
        return;
    auto bus = std::make_shared<ShmLink>(std::move(bus_result.value()));

    CanConfig config;
    config.bitrate = 500000;

    // Create 3 nodes on the same bus
    CanEndpoint node1(bus, config, 1);
    CanEndpoint node2(bus, config, 2);
    CanEndpoint node3(bus, config, 3);

    std::cout << "3 nodes on CAN bus (500 kbps)" << std::endl;

    // Node 1 broadcasts
    uint8_t data1[] = {0x10, 0x20};
    auto frame1 = CanEndpoint::make_std_frame(0x100, data1, 2);
    std::cout << "\nNode 1 broadcasts: ID=0x100" << std::endl;
    node1.send_can(frame1);

    // Node 2 broadcasts
    uint8_t data2[] = {0x30, 0x40};
    auto frame2 = CanEndpoint::make_std_frame(0x200, data2, 2);
    std::cout << "Node 2 broadcasts: ID=0x200" << std::endl;
    node2.send_can(frame2);

    // All nodes can receive all messages (broadcast nature of CAN)
    std::cout << "\nNote: In real CAN, all nodes would receive all frames" << std::endl;
    std::cout << "      (filtering is done by CAN controllers)" << std::endl;
}

/// Example 6: CAN with link model (bus errors)
void example_with_errors() {
    std::cout << "\n=== Example 6: CAN with Bus Errors ===" << std::endl;

    // Create link with 5% frame loss (simulating bus errors)
    LinkModel model(500,  // 500ns latency
                    100,  // 100ns jitter
                    0.05, // 5% loss
                    0.0,  // no duplication
                    0.01, // 1% corruption
                    0,    // unlimited bandwidth
                    42    // seed
    );

    auto server_result = ShmLink::create(String("can_errors"), 8192, &model);
    if (!server_result.is_ok())
        return;
    auto server_link = std::make_shared<ShmLink>(std::move(server_result.value()));

    auto client_result = ShmLink::attach(String("can_errors"));
    if (!client_result.is_ok())
        return;
    auto client_link = std::make_shared<ShmLink>(std::move(client_result.value()));

    CanConfig config;
    CanEndpoint tx(server_link, config, 1);
    CanEndpoint rx(client_link, config, 2);

    // Send 100 frames
    std::cout << "Sending 100 CAN frames with 5% loss + 1% corruption..." << std::endl;
    for (uint32_t i = 0; i < 100; ++i) {
        uint8_t data[] = {static_cast<uint8_t>(i)};
        auto frame = CanEndpoint::make_std_frame(0x300 + i, data, 1);
        tx.send_can(frame);
    }

    // Receive and count
    rx.process();
    size_t received = 0;
    while (true) {
        can_frame frame;
        if (!rx.recv_can(frame).is_ok())
            break;
        received++;
    }

    std::cout << "Received: " << received << " frames (expected ~94-95 due to 5% loss)" << std::endl;

    auto stats = server_link->stats();
    std::cout << "Link stats: sent=" << stats.frames_sent << " dropped=" << stats.frames_dropped
              << " corrupted=" << stats.frames_corrupted << std::endl;
}

/// Example 7: CAN data encoding patterns
void example_data_patterns() {
    std::cout << "\n=== Example 7: CAN Data Encoding Patterns ===" << std::endl;

    auto link_result = ShmLink::create(String("can_patterns"), 4096);
    if (!link_result.is_ok())
        return;
    auto link = std::make_shared<ShmLink>(std::move(link_result.value()));

    CanConfig config;
    CanEndpoint endpoint(link, config, 1);

    // Example: Encode a 16-bit value
    uint16_t value = 0x1234;
    uint8_t data1[2];
    data1[0] = value & 0xFF;        // Low byte
    data1[1] = (value >> 8) & 0xFF; // High byte
    auto frame1 = CanEndpoint::make_std_frame(0x400, data1, 2);
    std::cout << "16-bit value 0x" << std::hex << value << " encoded as: " << std::hex << std::setfill('0')
              << std::setw(2) << (int)data1[0] << " " << std::setw(2) << (int)data1[1] << std::dec << std::endl;

    // Example: Encode a 32-bit value
    uint32_t value32 = 0x12345678;
    uint8_t data2[4];
    data2[0] = value32 & 0xFF;
    data2[1] = (value32 >> 8) & 0xFF;
    data2[2] = (value32 >> 16) & 0xFF;
    data2[3] = (value32 >> 24) & 0xFF;
    auto frame2 = CanEndpoint::make_std_frame(0x401, data2, 4);
    std::cout << "32-bit value 0x" << std::hex << value32 << " encoded as: ";
    for (int i = 0; i < 4; ++i) {
        std::cout << std::hex << std::setfill('0') << std::setw(2) << (int)data2[i] << " ";
    }
    std::cout << std::dec << std::endl;

    // Example: Encode multiple signals in one frame
    uint8_t signal1 = 0x12;
    uint8_t signal2 = 0x34;
    uint16_t signal3 = 0x5678;
    uint8_t data3[4];
    data3[0] = signal1;
    data3[1] = signal2;
    data3[2] = signal3 & 0xFF;
    data3[3] = (signal3 >> 8) & 0xFF;
    auto frame3 = CanEndpoint::make_std_frame(0x402, data3, 4);
    std::cout << "Multiple signals: sig1=0x" << std::hex << (int)signal1 << " sig2=0x" << (int)signal2 << " sig3=0x"
              << signal3 << std::dec << std::endl;
}

int main() {
    std::cout << "=== Wirebit CAN Endpoint Demo ===" << std::endl;

    example_basic_can();
    example_std_vs_ext();
    example_rtr();
    example_bitrates();
    example_multi_node();
    example_with_errors();
    example_data_patterns();

    std::cout << "\n=== Demo Complete ===" << std::endl;
    return 0;
}
