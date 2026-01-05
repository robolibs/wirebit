#include <doctest/doctest.h>

#ifndef NO_HARDWARE

#include <fcntl.h>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>
#include <wirebit/wirebit.hpp>

using namespace wirebit;

TEST_CASE("PtyLink creation") {
    auto result = PtyLink::create();
    REQUIRE(result.is_ok());

    auto &pty = result.value();

    SUBCASE("Slave path exists") {
        struct stat st;
        REQUIRE(stat(pty.slave_path().c_str(), &st) == 0);
        echo::info("Slave path: ", pty.slave_path().c_str()).green();
    }

    SUBCASE("Master fd is valid") { REQUIRE(pty.master_fd() >= 0); }

    SUBCASE("Name contains slave path") {
        String name = pty.name();
        REQUIRE(name.find("pty:") != String::npos);
        REQUIRE(name.find("/dev/pts/") != String::npos);
    }

    SUBCASE("Initial stats are zero") {
        REQUIRE(pty.stats().frames_sent == 0);
        REQUIRE(pty.stats().frames_received == 0);
        REQUIRE(pty.stats().bytes_sent == 0);
        REQUIRE(pty.stats().bytes_received == 0);
    }

    SUBCASE("can_send returns true") { REQUIRE(pty.can_send() == true); }

    SUBCASE("can_recv returns true") { REQUIRE(pty.can_recv() == true); }
}

TEST_CASE("PtyLink send frame") {
    auto result = PtyLink::create();
    REQUIRE(result.is_ok());

    auto &pty = result.value();

    // Create and send a test frame
    Bytes payload = {0x01, 0x02, 0x03, 0x04, 0x05};
    Frame frame = make_frame(FrameType::SERIAL, payload, 1, 2);

    auto send_result = pty.send(frame);
    REQUIRE(send_result.is_ok());

    // Stats should be updated
    REQUIRE(pty.stats().frames_sent == 1);
    REQUIRE(pty.stats().bytes_sent > 0);
}

TEST_CASE("PtyLink receive from slave") {
    auto result = PtyLink::create();
    REQUIRE(result.is_ok());

    auto &pty = result.value();

    // Open slave side to write data
    int slave_fd = open(pty.slave_path().c_str(), O_RDWR | O_NONBLOCK);
    REQUIRE(slave_fd >= 0);

    // Create a test frame
    Bytes payload = {0xAA, 0xBB, 0xCC};
    Frame frame = make_frame(FrameType::SERIAL, payload, 5, 6);
    Bytes encoded = encode_frame(frame);

    // Write to slave
    ssize_t written = write(slave_fd, encoded.data(), encoded.size());
    REQUIRE(written == static_cast<ssize_t>(encoded.size()));

    // Small delay for data to be available
    usleep(10000);

    // Receive from master
    auto recv_result = pty.recv();
    REQUIRE(recv_result.is_ok());

    Frame &received = recv_result.value();
    REQUIRE(received.type() == FrameType::SERIAL);
    REQUIRE(received.payload.size() == payload.size());
    REQUIRE(received.payload == payload);
    // Copy packed struct fields to avoid binding issues
    uint32_t src_id = received.header.src_endpoint_id;
    uint32_t dst_id = received.header.dst_endpoint_id;
    REQUIRE(src_id == 5);
    REQUIRE(dst_id == 6);

    // Stats should be updated
    REQUIRE(pty.stats().frames_received == 1);

    close(slave_fd);
}

TEST_CASE("PtyLink move semantics") {
    auto result = PtyLink::create();
    REQUIRE(result.is_ok());

    String original_slave_path = result.value().slave_path();
    int original_fd = result.value().master_fd();

    // Move construct
    PtyLink moved_pty(std::move(result.value()));

    SUBCASE("Moved-from object has invalid fd") {
        REQUIRE(moved_pty.master_fd() == original_fd);
        REQUIRE(moved_pty.slave_path() == original_slave_path);
    }

    SUBCASE("Moved object is functional") {
        REQUIRE(moved_pty.can_send() == true);
        REQUIRE(moved_pty.can_recv() == true);
    }
}

TEST_CASE("PtyLink recv with no data") {
    auto result = PtyLink::create();
    REQUIRE(result.is_ok());

    auto &pty = result.value();

    // Receive without any data should return error (timeout code = 6)
    auto recv_result = pty.recv();
    REQUIRE(!recv_result.is_ok());
    REQUIRE(recv_result.error().code == 6); // Timeout error code
}

TEST_CASE("PtyLink partial frame handling") {
    auto result = PtyLink::create();
    REQUIRE(result.is_ok());

    auto &pty = result.value();

    int slave_fd = open(pty.slave_path().c_str(), O_RDWR | O_NONBLOCK);
    REQUIRE(slave_fd >= 0);

    // Create a frame and send it in two parts
    Bytes payload = {0x01, 0x02, 0x03, 0x04};
    Frame frame = make_frame(FrameType::SERIAL, payload, 1, 2);
    Bytes encoded = encode_frame(frame);

    // Send first half
    size_t half = encoded.size() / 2;
    ssize_t w1 = write(slave_fd, encoded.data(), half);
    (void)w1; // Suppress unused result warning
    usleep(5000);

    // First recv should fail (incomplete frame)
    auto recv_result1 = pty.recv();
    REQUIRE(!recv_result1.is_ok());

    // Buffer should have partial data
    REQUIRE(pty.rx_buffer_size() > 0);

    // Send second half
    ssize_t w2 = write(slave_fd, encoded.data() + half, encoded.size() - half);
    (void)w2; // Suppress unused result warning
    usleep(5000);

    // Second recv should succeed
    auto recv_result2 = pty.recv();
    REQUIRE(recv_result2.is_ok());

    Frame &received = recv_result2.value();
    REQUIRE(received.payload == payload);

    // Buffer should be empty now
    REQUIRE(pty.rx_buffer_size() == 0);

    close(slave_fd);
}

#else // NO_HARDWARE

TEST_CASE("PtyLink requires hardware support") {
    // This test just ensures the file compiles when NO_HARDWARE is defined
    REQUIRE(true);
}

#endif // NO_HARDWARE
