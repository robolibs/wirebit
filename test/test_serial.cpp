#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include <memory>
#include <wirebit/wirebit.hpp>

TEST_CASE("SerialEndpoint basic operations") {
    SUBCASE("Create serial endpoint") {
        auto link_result = wirebit::ShmLink::create(wirebit::String("ser_basic"), 4096);
        REQUIRE(link_result.is_ok());
        auto link = std::make_shared<wirebit::ShmLink>(std::move(link_result.value()));

        wirebit::SerialConfig config;
        config.baud = 9600;
        config.data_bits = 8;
        config.stop_bits = 1;
        config.parity = 'N';

        wirebit::SerialEndpoint endpoint(link, config, 1);

        CHECK(endpoint.endpoint_id() == 1);
        CHECK(endpoint.config().baud == 9600);
        CHECK(endpoint.name() == wirebit::String("serial_1"));
        CHECK(endpoint.rx_buffer_size() == 0);
    }

    SUBCASE("Send and receive bytes") {
        // Create server link
        auto server_result = wirebit::ShmLink::create(wirebit::String("ser_sendrecv"), 8192);
        REQUIRE(server_result.is_ok());
        auto server_link = std::make_shared<wirebit::ShmLink>(std::move(server_result.value()));

        // Attach client link
        auto client_result = wirebit::ShmLink::attach(wirebit::String("ser_sendrecv"));
        REQUIRE(client_result.is_ok());
        auto client_link = std::make_shared<wirebit::ShmLink>(std::move(client_result.value()));

        wirebit::SerialConfig config;
        config.baud = 115200;

        wirebit::SerialEndpoint tx_endpoint(server_link, config, 1);
        wirebit::SerialEndpoint rx_endpoint(client_link, config, 2);

        // Send some bytes
        wirebit::Bytes data = {0x48, 0x65, 0x6C, 0x6C, 0x6F}; // "Hello"
        auto send_result = tx_endpoint.send(data);
        REQUIRE(send_result.is_ok());

        // Process and receive
        auto process_result = rx_endpoint.process();
        CHECK(process_result.is_ok());

        auto recv_result = rx_endpoint.recv();
        REQUIRE(recv_result.is_ok());

        auto received = recv_result.value();
        CHECK(received.size() == 5);
        CHECK(received[0] == 0x48);
        CHECK(received[4] == 0x6F);
    }

    SUBCASE("Baud rate pacing") {
        auto server_result = wirebit::ShmLink::create(wirebit::String("ser_pacing"), 4096);
        REQUIRE(server_result.is_ok());
        auto server_link = std::make_shared<wirebit::ShmLink>(std::move(server_result.value()));

        auto client_result = wirebit::ShmLink::attach(wirebit::String("ser_pacing"));
        REQUIRE(client_result.is_ok());
        auto client_link = std::make_shared<wirebit::ShmLink>(std::move(client_result.value()));

        wirebit::SerialConfig config;
        config.baud = 9600; // Slow baud rate for testing timing
        config.data_bits = 8;
        config.stop_bits = 1;
        config.parity = 'N';

        wirebit::SerialEndpoint tx_endpoint(server_link, config, 1);
        wirebit::SerialEndpoint rx_endpoint(client_link, config, 2);

        // Send 10 bytes
        wirebit::Bytes data(10);
        for (size_t i = 0; i < 10; ++i) {
            data[i] = static_cast<wirebit::Byte>(i);
        }

        auto send_result = tx_endpoint.send(data);
        CHECK(send_result.is_ok());

        // Verify frames were created (should be 10 frames, one per byte)
        int frame_count = 0;
        while (true) {
            auto recv_result = client_link->recv();
            if (!recv_result.is_ok()) {
                break;
            }
            frame_count++;
        }
        CHECK(frame_count == 10);
    }

    SUBCASE("Empty send") {
        auto link_result = wirebit::ShmLink::create(wirebit::String("ser_empty"), 4096);
        REQUIRE(link_result.is_ok());
        auto link = std::make_shared<wirebit::ShmLink>(std::move(link_result.value()));

        wirebit::SerialConfig config;
        wirebit::SerialEndpoint endpoint(link, config, 1);

        wirebit::Bytes empty_data;
        auto result = endpoint.send(empty_data);
        CHECK(result.is_ok());
    }

    SUBCASE("Receive with no data") {
        auto link_result = wirebit::ShmLink::create(wirebit::String("ser_nodata"), 4096);
        REQUIRE(link_result.is_ok());
        auto link = std::make_shared<wirebit::ShmLink>(std::move(link_result.value()));

        wirebit::SerialConfig config;
        wirebit::SerialEndpoint endpoint(link, config, 1);

        auto result = endpoint.recv();
        CHECK_FALSE(result.is_ok());
    }

    SUBCASE("Buffer management") {
        auto server_result = wirebit::ShmLink::create(wirebit::String("ser_buffer"), 8192);
        REQUIRE(server_result.is_ok());
        auto server_link = std::make_shared<wirebit::ShmLink>(std::move(server_result.value()));

        auto client_result = wirebit::ShmLink::attach(wirebit::String("ser_buffer"));
        REQUIRE(client_result.is_ok());
        auto client_link = std::make_shared<wirebit::ShmLink>(std::move(client_result.value()));

        wirebit::SerialConfig config;
        config.max_chunk_read = 3; // Read max 3 bytes at a time

        wirebit::SerialEndpoint tx_endpoint(server_link, config, 1);
        wirebit::SerialEndpoint rx_endpoint(client_link, config, 2);

        // Send 10 bytes
        wirebit::Bytes data(10);
        for (size_t i = 0; i < 10; ++i) {
            data[i] = static_cast<wirebit::Byte>(i);
        }
        tx_endpoint.send(data);

        // Process all frames
        rx_endpoint.process();

        // Should have 10 bytes buffered
        CHECK(rx_endpoint.rx_buffer_size() == 10);

        // First recv should get 3 bytes (max_chunk_read)
        auto recv1 = rx_endpoint.recv();
        REQUIRE(recv1.is_ok());
        CHECK(recv1.value().size() == 3);
        CHECK(recv1.value()[0] == 0);
        CHECK(recv1.value()[2] == 2);

        // Should have 7 bytes remaining
        CHECK(rx_endpoint.rx_buffer_size() == 7);

        // Clear buffer
        rx_endpoint.clear_rx_buffer();
        CHECK(rx_endpoint.rx_buffer_size() == 0);
    }

    SUBCASE("Different serial configurations") {
        auto link_result = wirebit::ShmLink::create(wirebit::String("ser_configs"), 4096);
        REQUIRE(link_result.is_ok());
        auto link = std::make_shared<wirebit::ShmLink>(std::move(link_result.value()));

        // Test different configurations
        wirebit::SerialConfig config1;
        config1.baud = 9600;
        config1.data_bits = 7;
        config1.stop_bits = 2;
        config1.parity = 'E';

        wirebit::SerialEndpoint endpoint1(link, config1, 1);
        CHECK(endpoint1.config().baud == 9600);
        CHECK(endpoint1.config().data_bits == 7);
        CHECK(endpoint1.config().stop_bits == 2);
        CHECK(endpoint1.config().parity == 'E');

        wirebit::SerialConfig config2;
        config2.baud = 115200;
        config2.data_bits = 8;
        config2.stop_bits = 1;
        config2.parity = 'O';

        wirebit::SerialEndpoint endpoint2(link, config2, 2);
        CHECK(endpoint2.config().baud == 115200);
        CHECK(endpoint2.config().parity == 'O');
    }
}
