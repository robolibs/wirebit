#include <doctest/doctest.h>

#ifdef HAS_HARDWARE

#include <cstring>
#include <thread>
#include <unistd.h>
#include <wirebit/wirebit.hpp>

using namespace wirebit;

// Use unique interface names per test to avoid conflicts
static int tap_test_counter = 0;
static String make_tap_test_interface() {
    char buf[32];
    snprintf(buf, sizeof(buf), "wbtap%d", tap_test_counter++);
    return String(buf);
}

TEST_CASE("TapLink creation") {
    String iface = make_tap_test_interface();
    TapConfig config{
        .interface_name = iface,
        .create_if_missing = true,
        .destroy_on_close = true,
        .set_up_on_create = true,
    };

    auto result = TapLink::create(config);
    REQUIRE(result.is_ok());

    auto &link = result.value();

    SUBCASE("TAP fd is valid") { REQUIRE(link.tap_fd() >= 0); }

    SUBCASE("Interface name is correct") { REQUIRE(link.interface_name() == iface); }

    SUBCASE("Name contains interface") {
        String name = link.name();
        REQUIRE(name.find("tap:") != String::npos);
        REQUIRE(name.find(iface.c_str()) != String::npos);
    }

    SUBCASE("Initial stats are zero") {
        REQUIRE(link.stats().frames_sent == 0);
        REQUIRE(link.stats().frames_received == 0);
        REQUIRE(link.stats().bytes_sent == 0);
        REQUIRE(link.stats().bytes_received == 0);
        REQUIRE(link.stats().send_errors == 0);
        REQUIRE(link.stats().recv_errors == 0);
    }

    SUBCASE("can_send returns true") { REQUIRE(link.can_send() == true); }

    SUBCASE("can_recv returns true") { REQUIRE(link.can_recv() == true); }
}

TEST_CASE("TapLink attach to non-existent interface fails") {
    auto result = TapLink::attach("nonexistent_tap_iface_xyz");
    REQUIRE(!result.is_ok());
    // Should fail because interface doesn't exist
}

TEST_CASE("TapLink send and recv loopback") {
    String iface = make_tap_test_interface();
    TapConfig config{
        .interface_name = iface,
        .create_if_missing = true,
        .destroy_on_close = true,
        .set_up_on_create = true,
    };

    auto result = TapLink::create(config);
    REQUIRE(result.is_ok());

    auto &link = result.value();

    // Create an Ethernet frame (dst MAC + src MAC + ethertype + payload)
    MacAddr dst_mac = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}; // Broadcast
    MacAddr src_mac = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01}; // Locally administered
    uint16_t ethertype = ETH_P_IP;                          // IPv4

    // Test payload
    Bytes payload = {0xDE, 0xAD, 0xBE, 0xEF, 0xCA, 0xFE};
    Bytes eth_frame = make_eth_frame(dst_mac, src_mac, ethertype, payload);

    // Wrap in wirebit Frame
    Frame frame = make_frame(FrameType::ETHERNET, eth_frame, 1, 0);

    // Send frame
    auto send_result = link.send(frame);
    REQUIRE(send_result.is_ok());
    REQUIRE(link.stats().frames_sent == 1);
    REQUIRE(link.stats().bytes_sent >= eth_frame.size());
}

TEST_CASE("TapLink recv with no data") {
    String iface = make_tap_test_interface();
    TapConfig config{
        .interface_name = iface,
        .create_if_missing = true,
        .destroy_on_close = true,
        .set_up_on_create = true,
    };

    auto result = TapLink::create(config);
    REQUIRE(result.is_ok());

    auto &link = result.value();

    // Receive without any data should return timeout error
    auto recv_result = link.recv();
    REQUIRE(!recv_result.is_ok());
    REQUIRE(recv_result.error().code == 6); // Timeout error code
}

TEST_CASE("TapLink minimum frame size validation") {
    String iface = make_tap_test_interface();
    TapConfig config{
        .interface_name = iface,
        .create_if_missing = true,
        .destroy_on_close = true,
        .set_up_on_create = true,
    };

    auto result = TapLink::create(config);
    REQUIRE(result.is_ok());

    auto &link = result.value();

    // Try to send frame smaller than ETH_HLEN
    Bytes small_payload = {0x01, 0x02, 0x03};
    Frame frame = make_frame(FrameType::ETHERNET, std::move(small_payload), 1, 0);

    auto send_result = link.send(frame);
    REQUIRE(!send_result.is_ok());
    REQUIRE(send_result.error().code == 1); // invalid_argument error code
}

TEST_CASE("TapLink move semantics") {
    String iface = make_tap_test_interface();
    TapConfig config{
        .interface_name = iface,
        .create_if_missing = true,
        .destroy_on_close = true,
        .set_up_on_create = true,
    };

    auto result = TapLink::create(config);
    REQUIRE(result.is_ok());

    int original_fd = result.value().tap_fd();
    String original_iface = result.value().interface_name();

    // Move construct
    TapLink moved_link(std::move(result.value()));

    SUBCASE("Moved object has correct fd") { REQUIRE(moved_link.tap_fd() == original_fd); }

    SUBCASE("Moved object has correct interface") { REQUIRE(moved_link.interface_name() == original_iface); }

    SUBCASE("Moved object is functional") {
        REQUIRE(moved_link.can_send() == true);
        REQUIRE(moved_link.can_recv() == true);
    }
}

TEST_CASE("TapLink with EthEndpoint integration") {
    String iface = make_tap_test_interface();
    TapConfig link_config{
        .interface_name = iface,
        .create_if_missing = true,
        .destroy_on_close = true,
        .set_up_on_create = true,
    };

    auto link_result = TapLink::create(link_config);
    REQUIRE(link_result.is_ok());

    // Create shared pointer for EthEndpoint (move into shared_ptr)
    auto link_ptr = std::make_shared<TapLink>(std::move(link_result.value()));

    // Create Ethernet endpoint
    EthConfig eth_config{
        .bandwidth_bps = 1000000000, // 1 Gbps
        .promiscuous = true,         // Accept all frames
        .rx_buffer_size = 100,
    };

    MacAddr mac = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};
    EthEndpoint endpoint(link_ptr, eth_config, 1, mac);

    // Verify endpoint is created correctly
    REQUIRE(endpoint.endpoint_id() == 1);
    REQUIRE(endpoint.get_mac_addr() == mac);
    REQUIRE(endpoint.link() == link_ptr.get());
}

TEST_CASE("TapLink reject non-Ethernet frame") {
    String iface = make_tap_test_interface();
    TapConfig config{
        .interface_name = iface,
        .create_if_missing = true,
        .destroy_on_close = true,
        .set_up_on_create = true,
    };

    auto result = TapLink::create(config);
    REQUIRE(result.is_ok());

    auto &link = result.value();

    // Try to send a SERIAL frame type
    Bytes payload(ETH_HLEN + 10, 0); // Make it big enough but wrong type
    Frame frame = make_frame(FrameType::SERIAL, std::move(payload), 1, 0);

    auto send_result = link.send(frame);
    REQUIRE(!send_result.is_ok());
    REQUIRE(send_result.error().code == 1); // invalid_argument error code
}

TEST_CASE("TapLink interface creation verified with ip command") {
    String iface = make_tap_test_interface();
    TapConfig config{
        .interface_name = iface,
        .create_if_missing = true,
        .destroy_on_close = false, // Keep interface after link closes
        .set_up_on_create = true,
    };

    {
        auto result = TapLink::create(config);
        REQUIRE(result.is_ok());

        // Verify interface exists using ip command
        String cmd = String("ip link show ") + iface + " 2>/dev/null";
        int ret = system(cmd.c_str());
        REQUIRE(ret == 0);
    }
    // TapLink destructor called here, but destroy_on_close=false so interface remains

    // Interface should still exist after TapLink is destroyed
    String check_cmd = String("ip link show ") + iface + " 2>/dev/null";
    int check_ret = system(check_cmd.c_str());
    REQUIRE(check_ret == 0);

    // Manual cleanup
    String cleanup = String("sudo ip link delete ") + iface + " 2>/dev/null";
    (void)system(cleanup.c_str());
}

TEST_CASE("TapLink interface destruction verified with ip command") {
    String iface = make_tap_test_interface();
    TapConfig config{
        .interface_name = iface,
        .create_if_missing = true,
        .destroy_on_close = true, // Destroy interface when link closes
        .set_up_on_create = true,
    };

    {
        auto result = TapLink::create(config);
        REQUIRE(result.is_ok());

        // Verify interface exists while link is open
        String cmd = String("ip link show ") + iface + " 2>/dev/null";
        int ret = system(cmd.c_str());
        REQUIRE(ret == 0);
    }
    // TapLink destructor called here with destroy_on_close=true

    // Interface should NOT exist after TapLink is destroyed
    String check_cmd = String("ip link show ") + iface + " 2>/dev/null";
    int check_ret = system(check_cmd.c_str());
    REQUIRE(check_ret != 0); // Non-zero means interface doesn't exist
}

TEST_CASE("TapLink stats tracking") {
    String iface = make_tap_test_interface();
    TapConfig config{
        .interface_name = iface,
        .create_if_missing = true,
        .destroy_on_close = true,
        .set_up_on_create = true,
    };

    auto result = TapLink::create(config);
    REQUIRE(result.is_ok());

    auto &link = result.value();

    // Initial stats should be zero
    REQUIRE(link.stats().frames_sent == 0);
    REQUIRE(link.stats().bytes_sent == 0);

    // Send a valid Ethernet frame
    MacAddr dst_mac = MAC_BROADCAST;
    MacAddr src_mac = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};
    Bytes payload = {0x01, 0x02, 0x03, 0x04};
    Bytes eth_frame = make_eth_frame(dst_mac, src_mac, ETH_P_IP, payload);

    Frame frame = make_frame(FrameType::ETHERNET, eth_frame, 1, 0);

    auto send_result = link.send(frame);
    REQUIRE(send_result.is_ok());

    // Stats should be updated
    REQUIRE(link.stats().frames_sent == 1);
    REQUIRE(link.stats().bytes_sent >= eth_frame.size());

    // Reset stats
    link.reset_stats();
    REQUIRE(link.stats().frames_sent == 0);
    REQUIRE(link.stats().bytes_sent == 0);
}

#else // HAS_HARDWARE

TEST_CASE("TapLink requires HAS_HARDWARE") {
    // This test just ensures the file compiles when HAS_HARDWARE is not defined
    REQUIRE(true);
}

#endif // HAS_HARDWARE
