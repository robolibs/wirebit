#include <doctest/doctest.h>
#include <memory>
#include <wirebit/wirebit.hpp>

TEST_CASE("CanEndpoint basic operations") {
    SUBCASE("Create CAN endpoint") {
        auto link_result = wirebit::ShmLink::create(wirebit::String("can_basic"), 4096);
        REQUIRE(link_result.is_ok());
        auto link = std::make_shared<wirebit::ShmLink>(std::move(link_result.value()));

        wirebit::CanConfig config;
        config.bitrate = 500000; // 500 kbps

        wirebit::CanEndpoint endpoint(link, config, 1);

        CHECK(endpoint.endpoint_id() == 1);
        CHECK(endpoint.config().bitrate == 500000);
        CHECK(endpoint.name() == wirebit::String("can_1"));
        CHECK(endpoint.rx_buffer_size() == 0);
    }

    SUBCASE("Send and receive standard CAN frame") {
        auto server_result = wirebit::ShmLink::create(wirebit::String("can_std"), 8192);
        REQUIRE(server_result.is_ok());
        auto server_link = std::make_shared<wirebit::ShmLink>(std::move(server_result.value()));

        auto client_result = wirebit::ShmLink::attach(wirebit::String("can_std"));
        REQUIRE(client_result.is_ok());
        auto client_link = std::make_shared<wirebit::ShmLink>(std::move(client_result.value()));

        wirebit::CanConfig config;
        wirebit::CanEndpoint tx(server_link, config, 1);
        wirebit::CanEndpoint rx(client_link, config, 2);

        // Create standard frame (11-bit ID)
        uint8_t data[] = {0x11, 0x22, 0x33, 0x44};
        auto frame = wirebit::CanEndpoint::make_std_frame(0x123, data, 4);

        CHECK((uint32_t)frame.can_id == 0x123);
        CHECK((uint8_t)frame.can_dlc == 4);
        CHECK((uint8_t)frame.data[0] == 0x11);
        CHECK((uint8_t)frame.data[3] == 0x44);

        // Send frame
        auto send_result = tx.send_can(frame);
        REQUIRE(send_result.is_ok());

        // Receive frame
        rx.process();
        wirebit::can_frame received;
        auto recv_result = rx.recv_can(received);
        REQUIRE(recv_result.is_ok());

        CHECK((uint32_t)received.can_id == 0x123);
        CHECK((uint8_t)received.can_dlc == 4);
        CHECK((uint8_t)received.data[0] == 0x11);
        CHECK((uint8_t)received.data[3] == 0x44);
    }

    SUBCASE("Send and receive extended CAN frame") {
        auto server_result = wirebit::ShmLink::create(wirebit::String("can_ext"), 8192);
        REQUIRE(server_result.is_ok());
        auto server_link = std::make_shared<wirebit::ShmLink>(std::move(server_result.value()));

        auto client_result = wirebit::ShmLink::attach(wirebit::String("can_ext"));
        REQUIRE(client_result.is_ok());
        auto client_link = std::make_shared<wirebit::ShmLink>(std::move(client_result.value()));

        wirebit::CanConfig config;
        wirebit::CanEndpoint tx(server_link, config, 1);
        wirebit::CanEndpoint rx(client_link, config, 2);

        // Create extended frame (29-bit ID)
        uint8_t data[] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x00, 0x11};
        auto frame = wirebit::CanEndpoint::make_ext_frame(0x12345678, data, 8);

        CHECK((frame.can_id & CAN_EFF_FLAG) != 0);
        CHECK((frame.can_id & CAN_EFF_MASK) == 0x12345678);
        CHECK((uint8_t)frame.can_dlc == 8);

        // Send and receive
        tx.send_can(frame);
        rx.process();

        wirebit::can_frame received;
        auto recv_result = rx.recv_can(received);
        REQUIRE(recv_result.is_ok());

        CHECK((received.can_id & CAN_EFF_MASK) == 0x12345678);
        CHECK((uint8_t)received.can_dlc == 8);
        CHECK((uint8_t)received.data[0] == 0xAA);
        CHECK((uint8_t)received.data[7] == 0x11);
    }

    SUBCASE("RTR frame") {
        auto link_result = wirebit::ShmLink::create(wirebit::String("can_rtr"), 4096);
        REQUIRE(link_result.is_ok());
        auto link = std::make_shared<wirebit::ShmLink>(std::move(link_result.value()));

        wirebit::CanConfig config;
        wirebit::CanEndpoint endpoint(link, config, 1);

        // Create RTR frame
        auto rtr_std = wirebit::CanEndpoint::make_rtr_frame(0x100, false);
        CHECK((rtr_std.can_id & CAN_RTR_FLAG) != 0);
        CHECK((rtr_std.can_id & CAN_SFF_MASK) == 0x100);
        CHECK((uint8_t)rtr_std.can_dlc == 0);

        auto rtr_ext = wirebit::CanEndpoint::make_rtr_frame(0x1000000, true);
        CHECK((rtr_ext.can_id & CAN_RTR_FLAG) != 0);
        CHECK((rtr_ext.can_id & CAN_EFF_FLAG) != 0);
        CHECK((rtr_ext.can_id & CAN_EFF_MASK) == 0x1000000);
    }

    SUBCASE("Multiple frames") {
        auto server_result = wirebit::ShmLink::create(wirebit::String("can_multi"), 8192);
        REQUIRE(server_result.is_ok());
        auto server_link = std::make_shared<wirebit::ShmLink>(std::move(server_result.value()));

        auto client_result = wirebit::ShmLink::attach(wirebit::String("can_multi"));
        REQUIRE(client_result.is_ok());
        auto client_link = std::make_shared<wirebit::ShmLink>(std::move(client_result.value()));

        wirebit::CanConfig config;
        wirebit::CanEndpoint tx(server_link, config, 1);
        wirebit::CanEndpoint rx(client_link, config, 2);

        // Send 10 frames
        for (uint32_t i = 0; i < 10; ++i) {
            uint8_t data[] = {static_cast<uint8_t>(i)};
            auto frame = wirebit::CanEndpoint::make_std_frame(0x100 + i, data, 1);
            tx.send_can(frame);
        }

        // Receive all frames
        rx.process();
        CHECK(rx.rx_buffer_size() == 10);

        for (uint32_t i = 0; i < 10; ++i) {
            wirebit::can_frame received;
            auto result = rx.recv_can(received);
            REQUIRE(result.is_ok());
            CHECK((uint32_t)received.can_id == 0x100 + i);
            CHECK((uint8_t)received.data[0] == i);
        }

        CHECK(rx.rx_buffer_size() == 0);
    }

    SUBCASE("Invalid DLC") {
        auto link_result = wirebit::ShmLink::create(wirebit::String("can_dlc"), 4096);
        REQUIRE(link_result.is_ok());
        auto link = std::make_shared<wirebit::ShmLink>(std::move(link_result.value()));

        wirebit::CanConfig config;
        wirebit::CanEndpoint endpoint(link, config, 1);

        // Create frame with invalid DLC
        wirebit::can_frame frame = {};
        frame.can_id = 0x123;
        frame.can_dlc = 15; // Invalid!

        auto result = endpoint.send_can(frame);
        CHECK_FALSE(result.is_ok());
    }

    SUBCASE("Receive with no data") {
        auto link_result = wirebit::ShmLink::create(wirebit::String("can_nodata"), 4096);
        REQUIRE(link_result.is_ok());
        auto link = std::make_shared<wirebit::ShmLink>(std::move(link_result.value()));

        wirebit::CanConfig config;
        wirebit::CanEndpoint endpoint(link, config, 1);

        wirebit::can_frame frame;
        auto result = endpoint.recv_can(frame);
        CHECK_FALSE(result.is_ok());
    }

    SUBCASE("Buffer management") {
        auto server_result = wirebit::ShmLink::create(wirebit::String("can_buf"), 8192);
        REQUIRE(server_result.is_ok());
        auto server_link = std::make_shared<wirebit::ShmLink>(std::move(server_result.value()));

        auto client_result = wirebit::ShmLink::attach(wirebit::String("can_buf"));
        REQUIRE(client_result.is_ok());
        auto client_link = std::make_shared<wirebit::ShmLink>(std::move(client_result.value()));

        wirebit::CanConfig config;
        config.rx_buffer_size = 5; // Small buffer

        wirebit::CanEndpoint tx(server_link, config, 1);
        wirebit::CanEndpoint rx(client_link, config, 2);

        // Send 10 frames
        for (uint32_t i = 0; i < 10; ++i) {
            uint8_t data[] = {static_cast<uint8_t>(i)};
            auto frame = wirebit::CanEndpoint::make_std_frame(i, data, 1);
            tx.send_can(frame);
        }

        // Process - should buffer up to rx_buffer_size
        rx.process();
        CHECK(rx.rx_buffer_size() <= 5);

        // Clear buffer
        rx.clear_rx_buffer();
        CHECK(rx.rx_buffer_size() == 0);
    }

    SUBCASE("Different bitrates") {
        auto link_result = wirebit::ShmLink::create(wirebit::String("can_rate"), 4096);
        REQUIRE(link_result.is_ok());
        auto link = std::make_shared<wirebit::ShmLink>(std::move(link_result.value()));

        // Test different bitrates
        wirebit::CanConfig config1;
        config1.bitrate = 125000; // 125 kbps
        wirebit::CanEndpoint endpoint1(link, config1, 1);
        CHECK(endpoint1.config().bitrate == 125000);

        wirebit::CanConfig config2;
        config2.bitrate = 1000000; // 1 Mbps
        wirebit::CanEndpoint endpoint2(link, config2, 2);
        CHECK(endpoint2.config().bitrate == 1000000);
    }

    SUBCASE("Endpoint interface compatibility") {
        auto link_result = wirebit::ShmLink::create(wirebit::String("can_iface"), 4096);
        REQUIRE(link_result.is_ok());
        auto link = std::make_shared<wirebit::ShmLink>(std::move(link_result.value()));

        wirebit::CanConfig config;
        wirebit::CanEndpoint endpoint(link, config, 1);

        // Test Endpoint interface methods
        wirebit::Endpoint *ep = &endpoint;
        CHECK(ep->name() == wirebit::String("can_1"));
        CHECK(ep->link() == link.get());

        // Send via Endpoint interface
        wirebit::can_frame frame = {};
        frame.can_id = 0x456;
        frame.can_dlc = 2;
        frame.data[0] = 0xAA;
        frame.data[1] = 0xBB;

        wirebit::Bytes data(sizeof(wirebit::can_frame));
        std::memcpy(data.data(), &frame, sizeof(wirebit::can_frame));

        auto send_result = ep->send(data);
        CHECK(send_result.is_ok());
    }
}
