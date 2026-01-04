#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include <memory>
#include <wirebit/wirebit.hpp>

TEST_CASE("EthEndpoint basic operations") {
    SUBCASE("Create Ethernet endpoint") {
        auto link_result = wirebit::ShmLink::create(wirebit::String("eth_basic"), 4096);
        REQUIRE(link_result.is_ok());
        auto link = std::make_shared<wirebit::ShmLink>(std::move(link_result.value()));

        wirebit::MacAddr mac = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};
        wirebit::EthConfig config;
        config.bandwidth_bps = 1000000000; // 1 Gbps

        wirebit::EthEndpoint endpoint(link, config, 1, mac);

        CHECK(endpoint.endpoint_id() == 1);
        CHECK(endpoint.get_config().bandwidth_bps == 1000000000);
        CHECK(endpoint.get_mac_addr() == mac);
        CHECK(endpoint.name() == wirebit::String("eth_1"));
    }

    SUBCASE("MAC address formatting") {
        wirebit::MacAddr mac = {0x02, 0x42, 0xAC, 0x11, 0x00, 0x02};
        wirebit::String mac_str = wirebit::mac_to_string(mac);
        CHECK(mac_str == wirebit::String("02:42:ac:11:00:02"));
    }

    SUBCASE("MAC address parsing") {
        auto result = wirebit::string_to_mac(wirebit::String("02:42:ac:11:00:02"));
        REQUIRE(result.is_ok());
        wirebit::MacAddr mac = result.value();
        CHECK(mac[0] == 0x02);
        CHECK(mac[1] == 0x42);
        CHECK(mac[2] == 0xAC);
        CHECK(mac[3] == 0x11);
        CHECK(mac[4] == 0x00);
        CHECK(mac[5] == 0x02);
    }

    SUBCASE("Create Ethernet frame") {
        wirebit::MacAddr dst = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}; // Broadcast
        wirebit::MacAddr src = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};
        wirebit::Bytes payload = {0x01, 0x02, 0x03, 0x04};

        wirebit::Bytes frame = wirebit::make_eth_frame(dst, src, wirebit::ETH_P_IP, payload);

        // Check minimum frame size (60 bytes without FCS)
        CHECK(frame.size() == wirebit::ETH_ZLEN);

        // Check destination MAC
        CHECK(frame[0] == 0xFF);
        CHECK(frame[5] == 0xFF);

        // Check source MAC
        CHECK(frame[6] == 0x02);
        CHECK(frame[11] == 0x01);

        // Check EtherType (0x0800 = IPv4)
        CHECK(frame[12] == 0x08);
        CHECK(frame[13] == 0x00);

        // Check payload
        CHECK(frame[14] == 0x01);
        CHECK(frame[17] == 0x04);
    }

    SUBCASE("Parse Ethernet frame") {
        wirebit::Bytes frame = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, // dst MAC
                                0x02, 0x00, 0x00, 0x00, 0x00, 0x01, // src MAC
                                0x08, 0x00,                         // EtherType (IPv4)
                                0x01, 0x02, 0x03, 0x04};            // payload

        wirebit::MacAddr dst_mac, src_mac;
        uint16_t ethertype;
        wirebit::Bytes payload;

        auto result = wirebit::parse_eth_frame(frame, dst_mac, src_mac, ethertype, payload);
        REQUIRE(result.is_ok());

        CHECK(dst_mac == wirebit::MAC_BROADCAST);
        CHECK(src_mac[0] == 0x02);
        CHECK(src_mac[5] == 0x01);
        CHECK(ethertype == wirebit::ETH_P_IP);
        CHECK(payload.size() == 4);
        CHECK(payload[0] == 0x01);
        CHECK(payload[3] == 0x04);
    }

    SUBCASE("Send and receive Ethernet frame") {
        auto server_result = wirebit::ShmLink::create(wirebit::String("eth_comm"), 8192);
        REQUIRE(server_result.is_ok());
        auto server_link = std::make_shared<wirebit::ShmLink>(std::move(server_result.value()));

        auto client_result = wirebit::ShmLink::attach(wirebit::String("eth_comm"));
        REQUIRE(client_result.is_ok());
        auto client_link = std::make_shared<wirebit::ShmLink>(std::move(client_result.value()));

        wirebit::MacAddr mac1 = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};
        wirebit::MacAddr mac2 = {0x02, 0x00, 0x00, 0x00, 0x00, 0x02};

        wirebit::EthConfig config;
        wirebit::EthEndpoint tx(server_link, config, 1, mac1);
        wirebit::EthEndpoint rx(client_link, config, 2, mac2);

        // Create Ethernet frame
        wirebit::Bytes payload = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88};
        wirebit::Bytes frame = wirebit::make_eth_frame(mac2, mac1, wirebit::ETH_P_IP, payload);

        // Send frame
        auto send_result = tx.send_eth(frame);
        REQUIRE(send_result.is_ok());

        // Receive frame
        rx.process();
        auto recv_result = rx.recv_eth();
        REQUIRE(recv_result.is_ok());

        wirebit::Bytes received = recv_result.value();

        // Parse received frame
        wirebit::MacAddr dst_mac, src_mac;
        uint16_t ethertype;
        wirebit::Bytes recv_payload;
        wirebit::parse_eth_frame(received, dst_mac, src_mac, ethertype, recv_payload);

        CHECK(dst_mac == mac2);
        CHECK(src_mac == mac1);
        CHECK(ethertype == wirebit::ETH_P_IP);
        CHECK(recv_payload.size() >= payload.size());
        CHECK(recv_payload[0] == 0x11);
        CHECK(recv_payload[7] == 0x88);
    }

    SUBCASE("Broadcast frame reception") {
        auto server_result = wirebit::ShmLink::create(wirebit::String("eth_bcast"), 8192);
        REQUIRE(server_result.is_ok());
        auto server_link = std::make_shared<wirebit::ShmLink>(std::move(server_result.value()));

        auto client_result = wirebit::ShmLink::attach(wirebit::String("eth_bcast"));
        REQUIRE(client_result.is_ok());
        auto client_link = std::make_shared<wirebit::ShmLink>(std::move(client_result.value()));

        wirebit::MacAddr mac1 = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};
        wirebit::MacAddr mac2 = {0x02, 0x00, 0x00, 0x00, 0x00, 0x02};

        wirebit::EthConfig config;
        wirebit::EthEndpoint tx(server_link, config, 1, mac1);
        wirebit::EthEndpoint rx(client_link, config, 2, mac2);

        // Create broadcast frame
        wirebit::Bytes payload = {0xAA, 0xBB, 0xCC, 0xDD};
        wirebit::Bytes frame = wirebit::make_eth_frame(wirebit::MAC_BROADCAST, mac1, wirebit::ETH_P_ARP, payload);

        // Send frame
        auto send_result = tx.send_eth(frame);
        REQUIRE(send_result.is_ok());

        // Receive frame (should accept broadcast)
        rx.process();
        auto recv_result = rx.recv_eth();
        REQUIRE(recv_result.is_ok());

        wirebit::Bytes received = recv_result.value();

        // Parse received frame
        wirebit::MacAddr dst_mac, src_mac;
        uint16_t ethertype;
        wirebit::Bytes recv_payload;
        wirebit::parse_eth_frame(received, dst_mac, src_mac, ethertype, recv_payload);

        CHECK(dst_mac == wirebit::MAC_BROADCAST);
        CHECK(ethertype == wirebit::ETH_P_ARP);
    }

    SUBCASE("Frame filtering (non-promiscuous)") {
        auto server_result = wirebit::ShmLink::create(wirebit::String("eth_filter"), 8192);
        REQUIRE(server_result.is_ok());
        auto server_link = std::make_shared<wirebit::ShmLink>(std::move(server_result.value()));

        auto client_result = wirebit::ShmLink::attach(wirebit::String("eth_filter"));
        REQUIRE(client_result.is_ok());
        auto client_link = std::make_shared<wirebit::ShmLink>(std::move(client_result.value()));

        wirebit::MacAddr mac1 = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};
        wirebit::MacAddr mac2 = {0x02, 0x00, 0x00, 0x00, 0x00, 0x02};
        wirebit::MacAddr mac3 = {0x02, 0x00, 0x00, 0x00, 0x00, 0x03};

        wirebit::EthConfig config;
        config.promiscuous = false;
        wirebit::EthEndpoint tx(server_link, config, 1, mac1);
        wirebit::EthEndpoint rx(client_link, config, 2, mac2);

        // Create frame for different MAC (should be filtered)
        wirebit::Bytes payload = {0x01, 0x02, 0x03, 0x04};
        wirebit::Bytes frame = wirebit::make_eth_frame(mac3, mac1, wirebit::ETH_P_IP, payload);

        // Send frame
        auto send_result = tx.send_eth(frame);
        REQUIRE(send_result.is_ok());

        // Try to receive (should fail - frame not for us)
        auto process_result = rx.process();
        CHECK(!process_result.is_ok()); // Should be filtered

        auto recv_result = rx.recv_eth();
        CHECK(!recv_result.is_ok()); // No frames available
    }

    SUBCASE("Promiscuous mode") {
        auto server_result = wirebit::ShmLink::create(wirebit::String("eth_promisc"), 8192);
        REQUIRE(server_result.is_ok());
        auto server_link = std::make_shared<wirebit::ShmLink>(std::move(server_result.value()));

        auto client_result = wirebit::ShmLink::attach(wirebit::String("eth_promisc"));
        REQUIRE(client_result.is_ok());
        auto client_link = std::make_shared<wirebit::ShmLink>(std::move(client_result.value()));

        wirebit::MacAddr mac1 = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};
        wirebit::MacAddr mac2 = {0x02, 0x00, 0x00, 0x00, 0x00, 0x02};
        wirebit::MacAddr mac3 = {0x02, 0x00, 0x00, 0x00, 0x00, 0x03};

        wirebit::EthConfig config;
        config.promiscuous = true; // Enable promiscuous mode
        wirebit::EthEndpoint tx(server_link, config, 1, mac1);
        wirebit::EthEndpoint rx(client_link, config, 2, mac2);

        // Create frame for different MAC
        wirebit::Bytes payload = {0x01, 0x02, 0x03, 0x04};
        wirebit::Bytes frame = wirebit::make_eth_frame(mac3, mac1, wirebit::ETH_P_IP, payload);

        // Send frame
        auto send_result = tx.send_eth(frame);
        REQUIRE(send_result.is_ok());

        // Receive frame (should succeed in promiscuous mode)
        rx.process();
        auto recv_result = rx.recv_eth();
        REQUIRE(recv_result.is_ok()); // Should receive even though not for us
    }

    SUBCASE("Different bandwidth rates") {
        auto link_result = wirebit::ShmLink::create(wirebit::String("eth_bw"), 4096);
        REQUIRE(link_result.is_ok());
        auto link = std::make_shared<wirebit::ShmLink>(std::move(link_result.value()));

        wirebit::MacAddr mac = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};

        // Test different bandwidth configurations
        wirebit::EthConfig config_10m;
        config_10m.bandwidth_bps = 10000000; // 10 Mbps
        wirebit::EthEndpoint eth_10m(link, config_10m, 1, mac);
        CHECK(eth_10m.get_config().bandwidth_bps == 10000000);

        wirebit::EthConfig config_100m;
        config_100m.bandwidth_bps = 100000000; // 100 Mbps
        wirebit::EthEndpoint eth_100m(link, config_100m, 2, mac);
        CHECK(eth_100m.get_config().bandwidth_bps == 100000000);

        wirebit::EthConfig config_1g;
        config_1g.bandwidth_bps = 1000000000; // 1 Gbps
        wirebit::EthEndpoint eth_1g(link, config_1g, 3, mac);
        CHECK(eth_1g.get_config().bandwidth_bps == 1000000000);
    }

    SUBCASE("Helper function: make_eth_endpoint") {
        auto link_result = wirebit::ShmLink::create(wirebit::String("eth_helper"), 4096);
        REQUIRE(link_result.is_ok());
        auto link = std::make_shared<wirebit::ShmLink>(std::move(link_result.value()));

        // Create endpoint with auto-generated MAC
        auto endpoint = wirebit::make_eth_endpoint(link, 42, 1000000000);

        CHECK(endpoint->endpoint_id() == 42);
        CHECK(endpoint->get_config().bandwidth_bps == 1000000000);

        // Check auto-generated MAC (should be 02:00:00:00:00:2a for ID 42)
        wirebit::MacAddr expected_mac = {0x02, 0x00, 0x00, 0x00, 0x00, 0x2A};
        CHECK(endpoint->get_mac_addr() == expected_mac);
    }

    SUBCASE("Generic Endpoint interface") {
        auto server_result = wirebit::ShmLink::create(wirebit::String("eth_generic"), 8192);
        REQUIRE(server_result.is_ok());
        auto server_link = std::make_shared<wirebit::ShmLink>(std::move(server_result.value()));

        auto client_result = wirebit::ShmLink::attach(wirebit::String("eth_generic"));
        REQUIRE(client_result.is_ok());
        auto client_link = std::make_shared<wirebit::ShmLink>(std::move(client_result.value()));

        wirebit::MacAddr mac1 = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};
        wirebit::MacAddr mac2 = {0x02, 0x00, 0x00, 0x00, 0x00, 0x02};

        wirebit::EthConfig config;
        wirebit::EthEndpoint tx(server_link, config, 1, mac1);
        wirebit::EthEndpoint rx(client_link, config, 2, mac2);

        // Use generic send() interface (creates broadcast frame)
        wirebit::Bytes payload = {0x11, 0x22, 0x33, 0x44};
        auto send_result = tx.send(payload);
        REQUIRE(send_result.is_ok());

        // Use generic recv() interface
        rx.process();
        auto recv_result = rx.recv();
        REQUIRE(recv_result.is_ok());

        wirebit::Bytes received = recv_result.value();

        // Parse received frame
        wirebit::MacAddr dst_mac, src_mac;
        uint16_t ethertype;
        wirebit::Bytes recv_payload;
        wirebit::parse_eth_frame(received, dst_mac, src_mac, ethertype, recv_payload);

        CHECK(dst_mac == wirebit::MAC_BROADCAST); // Generic send uses broadcast
        CHECK(src_mac == mac1);
    }
}
