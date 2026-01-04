#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <chrono>
#include <doctest/doctest.h>
#include <thread>
#include <wirebit/wirebit.hpp>

TEST_CASE("ShmLink basic operations") {
    SUBCASE("Create and attach") {
        const char *link_name = "test_link_basic";

        // Create server side
        auto server_result = wirebit::ShmLink::create(wirebit::String(link_name), 4096);
        REQUIRE(server_result.is_ok());
        auto server = std::move(server_result.value());

        CHECK(server.name() == wirebit::String(link_name));
        CHECK(server.can_send());
        CHECK_FALSE(server.can_recv());

        // Small delay to ensure SHM is fully created
        std::this_thread::sleep_for(std::chrono::milliseconds(10));

        // Attach client side
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        auto client_result = wirebit::ShmLink::attach(wirebit::String(link_name));
        REQUIRE(client_result.is_ok());
        auto client = std::move(client_result.value());

        CHECK(client.name() == wirebit::String(link_name));
        CHECK(client.can_send());
        CHECK_FALSE(client.can_recv());
    }

    SUBCASE("Send and receive single frame") {
        const char *link_name = "test_link_single";

        auto server_result = wirebit::ShmLink::create(wirebit::String(link_name), 4096);
        REQUIRE(server_result.is_ok());
        auto server = std::move(server_result.value());

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        auto client_result = wirebit::ShmLink::attach(wirebit::String(link_name));
        REQUIRE(client_result.is_ok());
        auto client = std::move(client_result.value());

        // Server sends to client
        wirebit::Bytes payload = {1, 2, 3, 4, 5};
        wirebit::Frame frame = wirebit::make_frame(wirebit::FrameType::SERIAL, payload, 1, 2);

        auto send_result = server.send(frame);
        REQUIRE(send_result.is_ok());

        // Client receives
        auto recv_result = client.recv();
        REQUIRE(recv_result.is_ok());

        auto received = std::move(recv_result.value());
        CHECK(received.type() == wirebit::FrameType::SERIAL);
        CHECK(received.payload.size() == 5);
        CHECK(received.payload[0] == 1);
        CHECK(received.payload[4] == 5);
    }

    SUBCASE("Bidirectional communication") {
        const char *link_name = "test_link_bidir";

        auto server_result = wirebit::ShmLink::create(wirebit::String(link_name), 4096);
        REQUIRE(server_result.is_ok());
        auto server = std::move(server_result.value());

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        auto client_result = wirebit::ShmLink::attach(wirebit::String(link_name));
        REQUIRE(client_result.is_ok());
        auto client = std::move(client_result.value());

        // Server -> Client
        wirebit::Bytes payload1 = {0xAA, 0xBB};
        wirebit::Frame frame1 = wirebit::make_frame(wirebit::FrameType::CAN, payload1, 1, 2);
        REQUIRE(server.send(frame1).is_ok());

        auto recv1 = client.recv();
        REQUIRE(recv1.is_ok());
        CHECK(recv1.value().payload[0] == 0xAA);

        // Client -> Server
        wirebit::Bytes payload2 = {0xCC, 0xDD};
        wirebit::Frame frame2 = wirebit::make_frame(wirebit::FrameType::ETHERNET, payload2, 2, 1);
        REQUIRE(client.send(frame2).is_ok());

        auto recv2 = server.recv();
        REQUIRE(recv2.is_ok());
        CHECK(recv2.value().payload[0] == 0xCC);
    }

    SUBCASE("Multiple frames") {
        const char *link_name = "test_link_multi";

        auto server_result = wirebit::ShmLink::create(wirebit::String(link_name), 8192);
        REQUIRE(server_result.is_ok());
        auto server = std::move(server_result.value());

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        auto client_result = wirebit::ShmLink::attach(wirebit::String(link_name));
        REQUIRE(client_result.is_ok());
        auto client = std::move(client_result.value());

        // Send 10 frames
        for (int i = 0; i < 10; ++i) {
            wirebit::Bytes payload = {static_cast<wirebit::Byte>(i)};
            wirebit::Frame frame = wirebit::make_frame(wirebit::FrameType::SERIAL, payload, i, 0);
            REQUIRE(server.send(frame).is_ok());
        }

        // Receive 10 frames
        for (int i = 0; i < 10; ++i) {
            auto result = client.recv();
            REQUIRE(result.is_ok());

            auto frame = std::move(result.value());
            CHECK(frame.payload[0] == static_cast<wirebit::Byte>(i));
        }
    }
}

TEST_CASE("ShmLink with LinkModel") {
    SUBCASE("Perfect link (no impairments)") {
        const char *link_name = "test_link_perfect";

        wirebit::LinkModel perfect(0, 0, 0.0, 0.0, 0.0, 0, 42);

        auto server_result = wirebit::ShmLink::create(wirebit::String(link_name), 4096, &perfect);
        REQUIRE(server_result.is_ok());
        auto server = std::move(server_result.value());

        CHECK(server.has_model());

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        auto client_result = wirebit::ShmLink::attach(wirebit::String(link_name));
        REQUIRE(client_result.is_ok());
        auto client = std::move(client_result.value());

        // Send 10 frames
        for (int i = 0; i < 10; ++i) {
            wirebit::Bytes payload = {static_cast<wirebit::Byte>(i)};
            wirebit::Frame frame = wirebit::make_frame(wirebit::FrameType::SERIAL, payload);
            REQUIRE(server.send(frame).is_ok());
        }

        // All should be received
        int received = 0;
        for (int i = 0; i < 10; ++i) {
            auto result = client.recv();
            if (result.is_ok()) {
                received++;
            }
        }

        CHECK(received == 10);
    }

    SUBCASE("Lossy link (packet loss)") {
        const char *link_name = "test_link_lossy";

        // 50% packet loss for testing
        wirebit::LinkModel lossy(0, 0, 0.5, 0.0, 0.0, 0, 42);

        auto server_result = wirebit::ShmLink::create(wirebit::String(link_name), 8192, &lossy);
        REQUIRE(server_result.is_ok());
        auto server = std::move(server_result.value());

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        auto client_result = wirebit::ShmLink::attach(wirebit::String(link_name));
        REQUIRE(client_result.is_ok());
        auto client = std::move(client_result.value());

        // Send 100 frames
        for (int i = 0; i < 100; ++i) {
            wirebit::Bytes payload = {static_cast<wirebit::Byte>(i)};
            wirebit::Frame frame = wirebit::make_frame(wirebit::FrameType::SERIAL, payload);
            server.send(frame); // Ignore result (some will be dropped)
        }

        // Count received frames
        int received = 0;
        for (int i = 0; i < 100; ++i) {
            auto result = client.recv();
            if (result.is_ok()) {
                received++;
            }
        }

        // Should receive approximately 50% (allow some variance)
        CHECK(received > 30);
        CHECK(received < 70);

        // Check statistics
        auto stats = server.stats();
        uint64_t sent = stats.frames_sent;
        uint64_t dropped = stats.frames_dropped;

        CHECK(sent == 100);
        CHECK(dropped > 30);
        CHECK(dropped < 70);
    }

    SUBCASE("Link with corruption") {
        const char *link_name = "test_link_corrupt";

        // 100% corruption for testing
        wirebit::LinkModel corrupt(0, 0, 0.0, 0.0, 1.0, 0, 42);

        auto server_result = wirebit::ShmLink::create(wirebit::String(link_name), 4096, &corrupt);
        REQUIRE(server_result.is_ok());
        auto server = std::move(server_result.value());

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        auto client_result = wirebit::ShmLink::attach(wirebit::String(link_name));
        REQUIRE(client_result.is_ok());
        auto client = std::move(client_result.value());

        // Send frame with known data
        wirebit::Bytes original = {0x00, 0x00, 0x00, 0x00};
        wirebit::Frame frame = wirebit::make_frame(wirebit::FrameType::SERIAL, original);
        REQUIRE(server.send(frame).is_ok());

        // Receive and check if corrupted
        auto result = client.recv();
        REQUIRE(result.is_ok());

        auto received = std::move(result.value());

        // At least one byte should be different
        bool corrupted = false;
        for (size_t i = 0; i < received.payload.size(); ++i) {
            if (received.payload[i] != original[i]) {
                corrupted = true;
                break;
            }
        }

        CHECK(corrupted);

        // Check statistics
        auto stats = server.stats();
        uint64_t corrupted_count = stats.frames_corrupted;
        CHECK(corrupted_count == 1);
    }
}

TEST_CASE("ShmLink statistics") {
    SUBCASE("Track sent/received frames") {
        const char *link_name = "test_link_stats";

        auto server_result = wirebit::ShmLink::create(wirebit::String(link_name), 4096);
        REQUIRE(server_result.is_ok());
        auto server = std::move(server_result.value());

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        auto client_result = wirebit::ShmLink::attach(wirebit::String(link_name));
        REQUIRE(client_result.is_ok());
        auto client = std::move(client_result.value());

        // Send 5 frames
        for (int i = 0; i < 5; ++i) {
            wirebit::Bytes payload = {1, 2, 3};
            wirebit::Frame frame = wirebit::make_frame(wirebit::FrameType::SERIAL, payload);
            server.send(frame);
        }

        auto server_stats = server.stats();
        uint64_t sent = server_stats.frames_sent;
        CHECK(sent == 5);

        // Receive 5 frames
        for (int i = 0; i < 5; ++i) {
            client.recv();
        }

        auto client_stats = client.stats();
        uint64_t received = client_stats.frames_received;
        CHECK(received == 5);
    }

    SUBCASE("Reset statistics") {
        const char *link_name = "test_link_reset";

        auto server_result = wirebit::ShmLink::create(wirebit::String(link_name), 4096);
        REQUIRE(server_result.is_ok());
        auto server = std::move(server_result.value());

        // Send some frames
        for (int i = 0; i < 3; ++i) {
            wirebit::Bytes payload = {1};
            wirebit::Frame frame = wirebit::make_frame(wirebit::FrameType::SERIAL, payload);
            server.send(frame);
        }

        auto stats1 = server.stats();
        uint64_t sent1 = stats1.frames_sent;
        CHECK(sent1 == 3);

        // Reset
        server.reset_stats();

        auto stats2 = server.stats();
        uint64_t sent2 = stats2.frames_sent;
        CHECK(sent2 == 0);
    }
}

TEST_CASE("ShmLink ring usage") {
    SUBCASE("Check ring capacity and usage") {
        const char *link_name = "test_link_usage";

        auto server_result = wirebit::ShmLink::create(wirebit::String(link_name), 4096);
        REQUIRE(server_result.is_ok());
        auto server = std::move(server_result.value());

        CHECK(server.tx_capacity() == 4096);
        CHECK(server.rx_capacity() == 4096);

        // Initially empty
        CHECK(server.tx_usage() == 0.0f);
        CHECK(server.rx_usage() == 0.0f);
    }
}

TEST_CASE("ShmLink model enable/disable") {
    SUBCASE("Enable and disable model") {
        const char *link_name = "test_link_model_toggle";

        auto server_result = wirebit::ShmLink::create(wirebit::String(link_name), 4096);
        REQUIRE(server_result.is_ok());
        auto server = std::move(server_result.value());

        CHECK_FALSE(server.has_model());

        // Enable model
        wirebit::LinkModel model(1000000, 0, 0.0, 0.0, 0.0, 0, 42);
        server.set_model(model);
        CHECK(server.has_model());

        // Disable model
        server.clear_model();
        CHECK_FALSE(server.has_model());
    }
}
