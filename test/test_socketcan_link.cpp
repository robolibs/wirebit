#include <doctest/doctest.h>

#ifdef HAS_HARDWARE

#include <cstring>
#include <thread>
#include <unistd.h>
#include <wirebit/wirebit.hpp>

using namespace wirebit;

// Use unique interface names per test to avoid conflicts
static int test_counter = 0;
static String make_test_interface() {
    char buf[32];
    snprintf(buf, sizeof(buf), "wbtest%d", test_counter++);
    return String(buf);
}

TEST_CASE("SocketCanLink creation") {
    String iface = make_test_interface();
    SocketCanConfig config{
        .interface_name = iface,
        .create_if_missing = true,
        .destroy_on_close = true,
    };

    auto result = SocketCanLink::create(config);
    REQUIRE(result.is_ok());

    auto &link = result.value();

    SUBCASE("Socket fd is valid") { REQUIRE(link.socket_fd() >= 0); }

    SUBCASE("Interface name is correct") { REQUIRE(link.interface_name() == iface); }

    SUBCASE("Name contains interface") {
        String name = link.name();
        REQUIRE(name.find("socketcan:") != String::npos);
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

TEST_CASE("SocketCanLink attach to non-existent interface fails") {
    auto result = SocketCanLink::attach("nonexistent_can_iface_xyz");
    REQUIRE(!result.is_ok());
    REQUIRE(result.error().code == 3); // not_found error code
}

TEST_CASE("SocketCanLink send and recv loopback") {
    String iface = make_test_interface();
    SocketCanConfig config{
        .interface_name = iface,
        .create_if_missing = true,
        .destroy_on_close = true,
    };

    // Create two links on the same interface for loopback
    auto result1 = SocketCanLink::create(config);
    REQUIRE(result1.is_ok());

    // Attach second link (interface already exists now)
    auto result2 = SocketCanLink::attach(iface);
    REQUIRE(result2.is_ok());

    auto &sender = result1.value();
    auto &receiver = result2.value();

    // Create a CAN frame
    can_frame cf = {};
    cf.can_id = 0x123;
    cf.can_dlc = 4;
    cf.data[0] = 0xDE;
    cf.data[1] = 0xAD;
    cf.data[2] = 0xBE;
    cf.data[3] = 0xEF;

    // Wrap in wirebit Frame
    Bytes payload(sizeof(can_frame));
    std::memcpy(payload.data(), &cf, sizeof(can_frame));
    Frame frame = make_frame(FrameType::CAN, std::move(payload), 1, 0);

    // Send frame
    auto send_result = sender.send(frame);
    REQUIRE(send_result.is_ok());
    REQUIRE(sender.stats().frames_sent == 1);

    // Small delay for vcan loopback
    usleep(5000);

    // Receive frame on second link
    auto recv_result = receiver.recv();
    REQUIRE(recv_result.is_ok());

    Frame &received = recv_result.value();
    REQUIRE(received.type() == FrameType::CAN);
    REQUIRE(received.payload.size() == sizeof(can_frame));

    // Extract and verify CAN frame
    can_frame received_cf;
    std::memcpy(&received_cf, received.payload.data(), sizeof(can_frame));

    REQUIRE(received_cf.can_id == 0x123);
    REQUIRE(received_cf.can_dlc == 4);
    REQUIRE(received_cf.data[0] == 0xDE);
    REQUIRE(received_cf.data[1] == 0xAD);
    REQUIRE(received_cf.data[2] == 0xBE);
    REQUIRE(received_cf.data[3] == 0xEF);

    REQUIRE(receiver.stats().frames_received == 1);
}

TEST_CASE("SocketCanLink recv with no data") {
    String iface = make_test_interface();
    SocketCanConfig config{
        .interface_name = iface,
        .create_if_missing = true,
        .destroy_on_close = true,
    };

    auto result = SocketCanLink::create(config);
    REQUIRE(result.is_ok());

    auto &link = result.value();

    // Receive without any data should return timeout error
    auto recv_result = link.recv();
    REQUIRE(!recv_result.is_ok());
    REQUIRE(recv_result.error().code == 6); // Timeout error code
}

TEST_CASE("SocketCanLink extended CAN frame") {
    String iface = make_test_interface();
    SocketCanConfig config{
        .interface_name = iface,
        .create_if_missing = true,
        .destroy_on_close = true,
    };

    auto result1 = SocketCanLink::create(config);
    REQUIRE(result1.is_ok());

    auto result2 = SocketCanLink::attach(iface);
    REQUIRE(result2.is_ok());

    auto &sender = result1.value();
    auto &receiver = result2.value();

    // Create extended CAN frame (29-bit ID)
    can_frame cf = {};
    cf.can_id = 0x12345678 | CAN_EFF_FLAG; // 29-bit ID with EFF flag
    cf.can_dlc = 8;
    for (int i = 0; i < 8; i++) {
        cf.data[i] = static_cast<uint8_t>(i * 0x11);
    }

    Bytes payload(sizeof(can_frame));
    std::memcpy(payload.data(), &cf, sizeof(can_frame));
    Frame frame = make_frame(FrameType::CAN, std::move(payload), 1, 0);

    auto send_result = sender.send(frame);
    REQUIRE(send_result.is_ok());

    usleep(5000);

    auto recv_result = receiver.recv();
    REQUIRE(recv_result.is_ok());

    can_frame received_cf;
    std::memcpy(&received_cf, recv_result.value().payload.data(), sizeof(can_frame));

    REQUIRE((received_cf.can_id & CAN_EFF_FLAG) != 0);
    REQUIRE((received_cf.can_id & CAN_EFF_MASK) == 0x12345678);
    REQUIRE(received_cf.can_dlc == 8);
}

TEST_CASE("SocketCanLink move semantics") {
    String iface = make_test_interface();
    SocketCanConfig config{
        .interface_name = iface,
        .create_if_missing = true,
        .destroy_on_close = true,
    };

    auto result = SocketCanLink::create(config);
    REQUIRE(result.is_ok());

    int original_fd = result.value().socket_fd();
    String original_iface = result.value().interface_name();

    // Move construct
    SocketCanLink moved_link(std::move(result.value()));

    SUBCASE("Moved object has correct fd") { REQUIRE(moved_link.socket_fd() == original_fd); }

    SUBCASE("Moved object has correct interface") { REQUIRE(moved_link.interface_name() == original_iface); }

    SUBCASE("Moved object is functional") {
        REQUIRE(moved_link.can_send() == true);
        REQUIRE(moved_link.can_recv() == true);
    }
}

TEST_CASE("SocketCanLink with CanEndpoint integration") {
    String iface = make_test_interface();
    SocketCanConfig link_config{
        .interface_name = iface,
        .create_if_missing = true,
        .destroy_on_close = true,
    };

    auto link_result = SocketCanLink::create(link_config);
    REQUIRE(link_result.is_ok());

    // Create shared pointer for CanEndpoint (move into shared_ptr)
    auto link_ptr = std::make_shared<SocketCanLink>(std::move(link_result.value()));

    // Create second link for receiving
    auto link2_result = SocketCanLink::attach(iface);
    REQUIRE(link2_result.is_ok());
    auto recv_link = std::make_shared<SocketCanLink>(std::move(link2_result.value()));

    // Create CAN endpoints
    CanConfig can_config{
        .bitrate = 500000,
        .loopback = false,
        .listen_only = false,
        .rx_buffer_size = 100,
    };

    CanEndpoint sender(link_ptr, can_config, 1);
    CanEndpoint receiver(recv_link, can_config, 2);

    // Create and send a CAN frame using CanEndpoint helper
    uint8_t data[] = {0xCA, 0xFE, 0xBA, 0xBE};
    can_frame cf = CanEndpoint::make_std_frame(0x200, data, 4);

    auto send_result = sender.send_can(cf);
    REQUIRE(send_result.is_ok());

    usleep(10000);

    // Receive
    can_frame received_cf;
    auto recv_result = receiver.recv_can(received_cf);
    REQUIRE(recv_result.is_ok());

    REQUIRE((received_cf.can_id & CAN_SFF_MASK) == 0x200);
    REQUIRE(received_cf.can_dlc == 4);
    REQUIRE(received_cf.data[0] == 0xCA);
    REQUIRE(received_cf.data[1] == 0xFE);
    REQUIRE(received_cf.data[2] == 0xBA);
    REQUIRE(received_cf.data[3] == 0xBE);
}

TEST_CASE("SocketCanLink reject non-CAN frame") {
    String iface = make_test_interface();
    SocketCanConfig config{
        .interface_name = iface,
        .create_if_missing = true,
        .destroy_on_close = true,
    };

    auto result = SocketCanLink::create(config);
    REQUIRE(result.is_ok());

    auto &link = result.value();

    // Try to send a SERIAL frame type
    Bytes payload = {0x01, 0x02, 0x03};
    Frame frame = make_frame(FrameType::SERIAL, std::move(payload), 1, 0);

    auto send_result = link.send(frame);
    REQUIRE(!send_result.is_ok());
    REQUIRE(send_result.error().code == 1); // invalid_argument error code
}

#else // HAS_HARDWARE

TEST_CASE("SocketCanLink requires HAS_HARDWARE") {
    // This test just ensures the file compiles when HAS_HARDWARE is not defined
    REQUIRE(true);
}

#endif // HAS_HARDWARE
