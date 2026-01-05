#include <doctest/doctest.h>

#ifndef NO_HARDWARE

#include <cstring>
#include <thread>
#include <unistd.h>
#include <wirebit/wirebit.hpp>

using namespace wirebit;

// Use unique interface names per test to avoid conflicts
static int tun_test_counter = 0;
static String make_tun_test_interface() {
    char buf[32];
    snprintf(buf, sizeof(buf), "wbtun%d", tun_test_counter++);
    return String(buf);
}

TEST_CASE("TunLink creation") {
    String iface = make_tun_test_interface();
    TunConfig config{
        .interface_name = iface,
        .create_if_missing = true,
        .destroy_on_close = true,
        .set_up_on_create = true,
        .ip_address = "10.200.0.1/24",
    };

    auto result = TunLink::create(config);
    REQUIRE(result.is_ok());

    auto &link = result.value();

    SUBCASE("TUN fd is valid") { REQUIRE(link.tun_fd() >= 0); }

    SUBCASE("Interface name is correct") { REQUIRE(link.interface_name() == iface); }

    SUBCASE("Name contains interface") {
        String name = link.name();
        REQUIRE(name.find("tun:") != String::npos);
        REQUIRE(name.find(iface.c_str()) != String::npos);
    }

    SUBCASE("Initial stats are zero") {
        REQUIRE(link.stats().packets_sent == 0);
        REQUIRE(link.stats().packets_received == 0);
        REQUIRE(link.stats().bytes_sent == 0);
        REQUIRE(link.stats().bytes_received == 0);
        REQUIRE(link.stats().send_errors == 0);
        REQUIRE(link.stats().recv_errors == 0);
    }

    SUBCASE("can_send returns true") { REQUIRE(link.can_send() == true); }

    SUBCASE("can_recv returns true") { REQUIRE(link.can_recv() == true); }
}

TEST_CASE("TunLink attach to non-existent interface fails") {
    auto result = TunLink::attach("nonexistent_tun_iface_xyz");
    REQUIRE(!result.is_ok());
    // Should fail because interface doesn't exist
}

TEST_CASE("TunLink send IP packet") {
    String iface = make_tun_test_interface();
    TunConfig config{
        .interface_name = iface,
        .create_if_missing = true,
        .destroy_on_close = true,
        .set_up_on_create = true,
        .ip_address = "10.200.1.1/24",
    };

    auto result = TunLink::create(config);
    REQUIRE(result.is_ok());

    auto &link = result.value();

    // Create a minimal IPv4 packet
    // IP header: version(4) + IHL(5) = 0x45, then rest of 20-byte header
    Bytes ip_packet = {
        0x45,
        0x00,
        0x00,
        0x1C, // Version/IHL, DSCP/ECN, Total Length (28 bytes)
        0x00,
        0x01,
        0x00,
        0x00, // Identification, Flags/Fragment
        0x40,
        0x11,
        0x00,
        0x00, // TTL(64), Protocol(UDP=17), Checksum (placeholder)
        0x0A,
        0xC8,
        0x01,
        0x01, // Source IP: 10.200.1.1
        0x0A,
        0xC8,
        0x01,
        0x02, // Dest IP: 10.200.1.2
        // UDP header + minimal data
        0x00,
        0x50,
        0x00,
        0x51, // Src port 80, Dst port 81
        0x00,
        0x08,
        0x00,
        0x00, // Length 8, Checksum 0
    };

    // Wrap in wirebit Frame
    Frame frame = make_frame(FrameType::IP, ip_packet, 1, 0);

    // Send frame
    auto send_result = link.send(frame);
    REQUIRE(send_result.is_ok());
    REQUIRE(link.stats().packets_sent == 1);
    REQUIRE(link.stats().bytes_sent >= ip_packet.size());
}

TEST_CASE("TunLink recv with no data") {
    String iface = make_tun_test_interface();
    TunConfig config{
        .interface_name = iface,
        .create_if_missing = true,
        .destroy_on_close = true,
        .set_up_on_create = false, // Don't bring up = no kernel packets
        .ip_address = "",          // No IP = no unsolicited kernel packets
    };

    auto result = TunLink::create(config);
    REQUIRE(result.is_ok());

    auto &link = result.value();

    // Receive without any data should return timeout error
    // Note: Interface not brought UP to avoid kernel-generated packets
    auto recv_result = link.recv();
    REQUIRE(!recv_result.is_ok());
    REQUIRE(recv_result.error().code == 6); // Timeout error code
}

TEST_CASE("TunLink minimum packet size validation") {
    String iface = make_tun_test_interface();
    TunConfig config{
        .interface_name = iface,
        .create_if_missing = true,
        .destroy_on_close = true,
        .set_up_on_create = true,
        .ip_address = "10.200.3.1/24",
    };

    auto result = TunLink::create(config);
    REQUIRE(result.is_ok());

    auto &link = result.value();

    // Try to send packet smaller than IP header (20 bytes)
    Bytes small_payload = {0x45, 0x00, 0x00}; // Only 3 bytes
    Frame frame = make_frame(FrameType::IP, std::move(small_payload), 1, 0);

    auto send_result = link.send(frame);
    REQUIRE(!send_result.is_ok());
    REQUIRE(send_result.error().code == 1); // invalid_argument error code
}

TEST_CASE("TunLink move semantics") {
    String iface = make_tun_test_interface();
    TunConfig config{
        .interface_name = iface,
        .create_if_missing = true,
        .destroy_on_close = true,
        .set_up_on_create = true,
        .ip_address = "10.200.4.1/24",
    };

    auto result = TunLink::create(config);
    REQUIRE(result.is_ok());

    int original_fd = result.value().tun_fd();
    String original_iface = result.value().interface_name();

    // Move construct
    TunLink moved_link(std::move(result.value()));

    SUBCASE("Moved object has correct fd") { REQUIRE(moved_link.tun_fd() == original_fd); }

    SUBCASE("Moved object has correct interface") { REQUIRE(moved_link.interface_name() == original_iface); }

    SUBCASE("Moved object is functional") {
        REQUIRE(moved_link.can_send() == true);
        REQUIRE(moved_link.can_recv() == true);
    }
}

TEST_CASE("TunLink reject non-IP frame") {
    String iface = make_tun_test_interface();
    TunConfig config{
        .interface_name = iface,
        .create_if_missing = true,
        .destroy_on_close = true,
        .set_up_on_create = true,
        .ip_address = "10.200.5.1/24",
    };

    auto result = TunLink::create(config);
    REQUIRE(result.is_ok());

    auto &link = result.value();

    // Try to send an ETHERNET frame type
    Bytes payload(30, 0); // Big enough but wrong type
    Frame frame = make_frame(FrameType::ETHERNET, std::move(payload), 1, 0);

    auto send_result = link.send(frame);
    REQUIRE(!send_result.is_ok());
    REQUIRE(send_result.error().code == 1); // invalid_argument error code
}

TEST_CASE("TunLink interface creation verified with ip command") {
    String iface = make_tun_test_interface();
    TunConfig config{
        .interface_name = iface,
        .create_if_missing = true,
        .destroy_on_close = false, // Keep interface after link closes
        .set_up_on_create = true,
        .ip_address = "10.200.6.1/24",
    };

    {
        auto result = TunLink::create(config);
        REQUIRE(result.is_ok());

        // Verify interface exists using ip command
        String cmd = String("ip link show ") + iface + " 2>/dev/null";
        int ret = system(cmd.c_str());
        REQUIRE(ret == 0);

        // Verify IP address was assigned
        String ip_cmd = String("ip addr show ") + iface + " | grep '10.200.6.1' 2>/dev/null";
        int ip_ret = system(ip_cmd.c_str());
        REQUIRE(ip_ret == 0);
    }
    // TunLink destructor called here, but destroy_on_close=false so interface remains

    // Interface should still exist after TunLink is destroyed
    String check_cmd = String("ip link show ") + iface + " 2>/dev/null";
    int check_ret = system(check_cmd.c_str());
    REQUIRE(check_ret == 0);

    // Manual cleanup
    String cleanup = String("sudo ip link delete ") + iface + " 2>/dev/null";
    [[maybe_unused]] int cleanup_ret = system(cleanup.c_str());
}

TEST_CASE("TunLink interface destruction verified with ip command") {
    String iface = make_tun_test_interface();
    TunConfig config{
        .interface_name = iface,
        .create_if_missing = true,
        .destroy_on_close = true, // Destroy interface when link closes
        .set_up_on_create = true,
        .ip_address = "10.200.7.1/24",
    };

    {
        auto result = TunLink::create(config);
        REQUIRE(result.is_ok());

        // Verify interface exists while link is open
        String cmd = String("ip link show ") + iface + " 2>/dev/null";
        int ret = system(cmd.c_str());
        REQUIRE(ret == 0);
    }
    // TunLink destructor called here with destroy_on_close=true

    // Interface should NOT exist after TunLink is destroyed
    String check_cmd = String("ip link show ") + iface + " 2>/dev/null";
    int check_ret = system(check_cmd.c_str());
    REQUIRE(check_ret != 0); // Non-zero means interface doesn't exist
}

TEST_CASE("TunLink stats tracking") {
    String iface = make_tun_test_interface();
    TunConfig config{
        .interface_name = iface,
        .create_if_missing = true,
        .destroy_on_close = true,
        .set_up_on_create = true,
        .ip_address = "10.200.8.1/24",
    };

    auto result = TunLink::create(config);
    REQUIRE(result.is_ok());

    auto &link = result.value();

    // Initial stats should be zero
    REQUIRE(link.stats().packets_sent == 0);
    REQUIRE(link.stats().bytes_sent == 0);

    // Send a valid IP packet
    Bytes ip_packet = {
        0x45, 0x00, 0x00, 0x14, // Version/IHL, DSCP/ECN, Total Length (20 bytes - header only)
        0x00, 0x01, 0x00, 0x00, // Identification, Flags/Fragment
        0x40, 0x00, 0x00, 0x00, // TTL(64), Protocol(0), Checksum
        0x0A, 0xC8, 0x08, 0x01, // Source IP: 10.200.8.1
        0x0A, 0xC8, 0x08, 0x02, // Dest IP: 10.200.8.2
    };

    Frame frame = make_frame(FrameType::IP, ip_packet, 1, 0);

    auto send_result = link.send(frame);
    REQUIRE(send_result.is_ok());

    // Stats should be updated
    REQUIRE(link.stats().packets_sent == 1);
    REQUIRE(link.stats().bytes_sent >= ip_packet.size());

    // Reset stats
    link.reset_stats();
    REQUIRE(link.stats().packets_sent == 0);
    REQUIRE(link.stats().bytes_sent == 0);
}

TEST_CASE("TunLink creation without IP address") {
    String iface = make_tun_test_interface();
    TunConfig config{
        .interface_name = iface,
        .create_if_missing = true,
        .destroy_on_close = true,
        .set_up_on_create = true,
        .ip_address = "", // No IP address assignment
    };

    auto result = TunLink::create(config);
    REQUIRE(result.is_ok());

    auto &link = result.value();

    // Interface should exist but without IP
    String cmd = String("ip link show ") + iface + " 2>/dev/null";
    int ret = system(cmd.c_str());
    REQUIRE(ret == 0);

    // Should still be able to send/recv
    REQUIRE(link.can_send() == true);
    REQUIRE(link.can_recv() == true);
}

#else // NO_HARDWARE

TEST_CASE("TunLink requires hardware support") {
    // This test just ensures the file compiles when NO_HARDWARE is defined
    REQUIRE(true);
}

#endif // NO_HARDWARE
