/// @file ethernet_demo.cpp
/// @brief Comprehensive demonstration of Ethernet endpoint functionality
///
/// This file demonstrates various Ethernet L2 communication scenarios:
/// 1. Basic Ethernet frame communication
/// 2. Broadcast vs unicast frames
/// 3. Different EtherTypes (IPv4, ARP, IPv6)
/// 4. Different bandwidth rates (10M, 100M, 1G bps)
/// 5. Multi-node Ethernet switch simulation
/// 6. Ethernet with network errors (loss, corruption)
/// 7. VLAN tagging and frame parsing

#include <echo/echo.hpp>
#include <iomanip>
#include <iostream>
#include <thread>
#include <wirebit/wirebit.hpp>

using namespace wirebit;

/// Helper function to print Ethernet frame details
void print_frame(const Bytes &frame) {
    if (frame.size() < ETH_HLEN) {
        echo::error("Frame too small").red();
        return;
    }

    MacAddr dst_mac, src_mac;
    uint16_t ethertype;
    Bytes payload;
    parse_eth_frame(frame, dst_mac, src_mac, ethertype, payload);

    std::cout << "  Dst MAC: " << mac_to_string(dst_mac).c_str() << std::endl;
    std::cout << "  Src MAC: " << mac_to_string(src_mac).c_str() << std::endl;
    std::cout << "  EtherType: 0x" << std::hex << std::setfill('0') << std::setw(4) << ethertype << std::dec
              << std::endl;
    std::cout << "  Payload: " << payload.size() << " bytes" << std::endl;
}

/// Example 1: Basic Ethernet communication
void example_basic_ethernet() {
    echo::info("=== Example 1: Basic Ethernet Communication ===").cyan().bold();

    // Create shared memory link
    auto server_result = ShmLink::create(String("eth_basic"), 8192);
    if (!server_result.is_ok()) {
        echo::error("Failed to create ShmLink").red();
        return;
    }
    auto server_link = std::make_shared<ShmLink>(std::move(server_result.value()));

    auto client_result = ShmLink::attach(String("eth_basic"));
    if (!client_result.is_ok()) {
        echo::error("Failed to attach to ShmLink").red();
        return;
    }
    auto client_link = std::make_shared<ShmLink>(std::move(client_result.value()));

    // Create two Ethernet endpoints
    MacAddr mac1 = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};
    MacAddr mac2 = {0x02, 0x00, 0x00, 0x00, 0x00, 0x02};

    EthConfig config;
    config.bandwidth_bps = 1000000000; // 1 Gbps

    EthEndpoint node1(server_link, config, 1, mac1);
    EthEndpoint node2(client_link, config, 2, mac2);

    // Node 1 sends to Node 2
    Bytes payload = {0x48, 0x65, 0x6C, 0x6C, 0x6F}; // "Hello"
    Bytes frame = make_eth_frame(mac2, mac1, ETH_P_IP, payload);

    std::cout << "Node 1 sends to Node 2:" << std::endl;
    print_frame(frame);

    auto send_result = node1.send_eth(frame);
    if (!send_result.is_ok()) {
        echo::error("Send failed").red();
        return;
    }

    // Node 2 receives
    node2.process();
    auto recv_result = node2.recv_eth();
    if (recv_result.is_ok()) {
        std::cout << "\nNode 2 received:" << std::endl;
        print_frame(recv_result.value());
    }

    std::cout << std::endl;
}

/// Example 2: Broadcast vs Unicast
void example_broadcast_unicast() {
    echo::info("=== Example 2: Broadcast vs Unicast ===").cyan().bold();

    auto server_result = ShmLink::create(String("eth_bcast"), 8192);
    if (!server_result.is_ok())
        return;
    auto server_link = std::make_shared<ShmLink>(std::move(server_result.value()));

    auto client_result = ShmLink::attach(String("eth_bcast"));
    if (!client_result.is_ok())
        return;
    auto client_link = std::make_shared<ShmLink>(std::move(client_result.value()));

    MacAddr mac1 = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};
    MacAddr mac2 = {0x02, 0x00, 0x00, 0x00, 0x00, 0x02};

    EthConfig config;
    EthEndpoint node1(server_link, config, 1, mac1);
    EthEndpoint node2(client_link, config, 2, mac2);

    // Broadcast frame (ARP request)
    std::cout << "Broadcast ARP request:" << std::endl;
    Bytes arp_payload = {0x00, 0x01, 0x08, 0x00, 0x06, 0x04, 0x00, 0x01}; // ARP request header
    Bytes bcast_frame = make_eth_frame(MAC_BROADCAST, mac1, ETH_P_ARP, arp_payload);
    print_frame(bcast_frame);

    node1.send_eth(bcast_frame);
    node2.process();
    auto recv1 = node2.recv_eth();
    if (recv1.is_ok()) {
        echo::info("Node 2 received broadcast frame").green();
    }

    // Unicast frame (ARP reply)
    std::cout << "\nUnicast ARP reply:" << std::endl;
    Bytes arp_reply = {0x00, 0x01, 0x08, 0x00, 0x06, 0x04, 0x00, 0x02}; // ARP reply header
    Bytes unicast_frame = make_eth_frame(mac1, mac2, ETH_P_ARP, arp_reply);
    print_frame(unicast_frame);

    std::cout << std::endl;
}

/// Example 3: Different EtherTypes
void example_ethertypes() {
    echo::info("=== Example 3: Different EtherTypes ===").cyan().bold();

    auto link_result = ShmLink::create(String("eth_types"), 4096);
    if (!link_result.is_ok())
        return;
    auto link = std::make_shared<ShmLink>(std::move(link_result.value()));

    MacAddr mac = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};
    EthConfig config;
    EthEndpoint endpoint(link, config, 1, mac);

    // IPv4 packet
    std::cout << "IPv4 frame (0x0800):" << std::endl;
    Bytes ipv4_payload = {0x45, 0x00, 0x00, 0x28}; // IPv4 header start
    Bytes ipv4_frame = make_eth_frame(MAC_BROADCAST, mac, ETH_P_IP, ipv4_payload);
    std::cout << "  EtherType: 0x" << std::hex << ETH_P_IP << std::dec << " (IPv4)" << std::endl;

    // ARP packet
    std::cout << "\nARP frame (0x0806):" << std::endl;
    Bytes arp_payload = {0x00, 0x01, 0x08, 0x00};
    Bytes arp_frame = make_eth_frame(MAC_BROADCAST, mac, ETH_P_ARP, arp_payload);
    std::cout << "  EtherType: 0x" << std::hex << ETH_P_ARP << std::dec << " (ARP)" << std::endl;

    // IPv6 packet
    std::cout << "\nIPv6 frame (0x86DD):" << std::endl;
    Bytes ipv6_payload = {0x60, 0x00, 0x00, 0x00}; // IPv6 header start
    Bytes ipv6_frame = make_eth_frame(MAC_BROADCAST, mac, ETH_P_IPV6, ipv6_payload);
    std::cout << "  EtherType: 0x" << std::hex << ETH_P_IPV6 << std::dec << " (IPv6)" << std::endl;

    // VLAN tagged frame
    std::cout << "\nVLAN tagged frame (0x8100):" << std::endl;
    Bytes vlan_payload = {0x00, 0x64, 0x08, 0x00}; // VLAN ID 100, EtherType IPv4
    Bytes vlan_frame = make_eth_frame(MAC_BROADCAST, mac, ETH_P_8021Q, vlan_payload);
    std::cout << "  EtherType: 0x" << std::hex << ETH_P_8021Q << std::dec << " (802.1Q VLAN)" << std::endl;

    std::cout << std::endl;
}

/// Example 4: Different Bandwidth Rates
void example_bandwidth_rates() {
    echo::info("=== Example 4: Different Bandwidth Rates ===").cyan().bold();

    auto link_result = ShmLink::create(String("eth_bw"), 4096);
    if (!link_result.is_ok())
        return;
    auto link = std::make_shared<ShmLink>(std::move(link_result.value()));

    MacAddr mac = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};
    Bytes payload(1000); // 1000 byte payload

    // Test different bandwidth rates
    struct BandwidthTest {
        uint64_t bps;
        const char *name;
    };

    BandwidthTest tests[] = {{10000000, "10 Mbps"}, {100000000, "100 Mbps"}, {1000000000, "1 Gbps"}};

    for (const auto &test : tests) {
        EthConfig config;
        config.bandwidth_bps = test.bps;
        EthEndpoint endpoint(link, config, 1, mac);

        Bytes frame = make_eth_frame(MAC_BROADCAST, mac, ETH_P_IP, payload);

        // Calculate expected transmission time
        uint64_t wire_bytes = frame.size() + 20; // Frame + preamble + IFG
        uint64_t wire_bits = wire_bytes * 8;
        uint64_t frame_time_ns = (wire_bits * 1000000000ULL) / test.bps;
        uint64_t frame_time_us = frame_time_ns / 1000;

        std::cout << test.name << std::endl;
        std::cout << "  Frame size: " << frame.size() << " bytes" << std::endl;
        std::cout << "  Wire bytes: " << wire_bytes << " bytes (with overhead)" << std::endl;
        std::cout << "  Frame time: " << frame_time_us << " µs" << std::endl;
        std::cout << std::endl;
    }
}

/// Example 5: Multi-Node Ethernet Switch
void example_ethernet_switch() {
    echo::info("=== Example 5: Multi-Node Ethernet Switch ===").cyan().bold();

    auto link_result = ShmLink::create(String("eth_switch"), 16384);
    if (!link_result.is_ok())
        return;
    auto link = std::make_shared<ShmLink>(std::move(link_result.value()));

    // Create 3 nodes on the same "switch"
    MacAddr mac1 = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};
    MacAddr mac2 = {0x02, 0x00, 0x00, 0x00, 0x00, 0x02};
    MacAddr mac3 = {0x02, 0x00, 0x00, 0x00, 0x00, 0x03};

    EthConfig config;
    config.promiscuous = true; // All nodes see all frames (like a hub)

    EthEndpoint node1(link, config, 1, mac1);
    EthEndpoint node2(link, config, 2, mac2);
    EthEndpoint node3(link, config, 3, mac3);

    std::cout << "3 nodes on Ethernet switch (1 Gbps)" << std::endl;
    std::cout << "  Node 1: " << mac_to_string(mac1).c_str() << std::endl;
    std::cout << "  Node 2: " << mac_to_string(mac2).c_str() << std::endl;
    std::cout << "  Node 3: " << mac_to_string(mac3).c_str() << std::endl;

    // Node 1 broadcasts
    std::cout << "\nNode 1 broadcasts ARP request:" << std::endl;
    Bytes arp_req = {0x00, 0x01, 0x08, 0x00, 0x06, 0x04, 0x00, 0x01};
    Bytes bcast = make_eth_frame(MAC_BROADCAST, mac1, ETH_P_ARP, arp_req);
    node1.send_eth(bcast);

    std::cout << "Note: In promiscuous mode, all nodes receive all frames" << std::endl;
    std::cout << "      (simulating a hub or switch in learning mode)" << std::endl;

    std::cout << std::endl;
}

/// Example 6: Ethernet with Network Errors
void example_network_errors() {
    echo::info("=== Example 6: Ethernet with Network Errors ===").cyan().bold();

    // Create link with error model
    LinkModel model;
    model.base_latency_ns = 1000; // 1 µs latency
    model.jitter_ns = 200;        // 200 ns jitter
    model.drop_prob = 0.05;       // 5% packet loss
    model.corrupt_prob = 0.01;    // 1% corruption

    auto server_result = ShmLink::create(String("eth_errors"), 8192);
    if (!server_result.is_ok())
        return;
    auto server_link = std::make_shared<ShmLink>(std::move(server_result.value()));
    server_link->set_model(model);

    auto client_result = ShmLink::attach(String("eth_errors"));
    if (!client_result.is_ok())
        return;
    auto client_link = std::make_shared<ShmLink>(std::move(client_result.value()));

    MacAddr mac1 = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};
    MacAddr mac2 = {0x02, 0x00, 0x00, 0x00, 0x00, 0x02};

    EthConfig config;
    EthEndpoint tx(server_link, config, 1, mac1);
    EthEndpoint rx(client_link, config, 2, mac2);

    std::cout << "Sending 100 Ethernet frames with 5% loss + 1% corruption..." << std::endl;

    int sent = 0;
    int received = 0;

    for (int i = 0; i < 100; ++i) {
        Bytes payload = {static_cast<uint8_t>(i)};
        Bytes frame = make_eth_frame(mac2, mac1, ETH_P_IP, payload);

        auto send_result = tx.send_eth(frame);
        if (send_result.is_ok()) {
            sent++;
        }

        // Try to receive
        rx.process();
        auto recv_result = rx.recv_eth();
        if (recv_result.is_ok()) {
            received++;
        }
    }

    std::cout << "Sent: " << sent << " frames" << std::endl;
    std::cout << "Received: " << received << " frames" << std::endl;
    std::cout << "Loss rate: " << std::fixed << std::setprecision(1) << (100.0 * (sent - received) / sent) << "%"
              << std::endl;

    std::cout << std::endl;
}

/// Example 7: Frame Size Variations
void example_frame_sizes() {
    echo::info("=== Example 7: Frame Size Variations ===").cyan().bold();

    auto link_result = ShmLink::create(String("eth_sizes"), 4096);
    if (!link_result.is_ok())
        return;
    auto link = std::make_shared<ShmLink>(std::move(link_result.value()));

    MacAddr mac = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};
    EthConfig config;
    EthEndpoint endpoint(link, config, 1, mac);

    struct FrameTest {
        size_t payload_size;
        const char *description;
    };

    FrameTest tests[] = {{0, "Minimum frame (no payload)"},
                         {46, "Minimum payload (46 bytes)"},
                         {100, "Small frame (100 bytes)"},
                         {500, "Medium frame (500 bytes)"},
                         {1500, "Maximum MTU (1500 bytes)"}};

    for (const auto &test : tests) {
        Bytes payload(test.payload_size);
        Bytes frame = make_eth_frame(MAC_BROADCAST, mac, ETH_P_IP, payload);

        std::cout << test.description << std::endl;
        std::cout << "  Payload: " << test.payload_size << " bytes" << std::endl;
        std::cout << "  Frame: " << frame.size() << " bytes (with padding)" << std::endl;
        std::cout << std::endl;
    }
}

int main() {
    echo::info("=== Wirebit Ethernet Endpoint Demo ===").cyan().bold();
    std::cout << std::endl;

    example_basic_ethernet();
    example_broadcast_unicast();
    example_ethertypes();
    example_bandwidth_rates();
    example_ethernet_switch();
    example_network_errors();
    example_frame_sizes();

    echo::info("All examples completed!").green().bold();
    return 0;
}
