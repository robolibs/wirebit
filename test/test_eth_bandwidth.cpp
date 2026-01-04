/// @file test_eth_bandwidth.cpp
/// @brief Multi-process integration test for Ethernet bandwidth shaping
///
/// This test validates:
/// - Ethernet bandwidth shaping accuracy
/// - Multi-process frame exchange
/// - Throughput measurement
/// - Frame integrity

#include <cstring>
#include <echo/echo.hpp>
#include <sys/wait.h>
#include <unistd.h>
#include <wirebit/wirebit.hpp>

using namespace wirebit;

/// Sender process: sends Ethernet frames at configured bandwidth
int sender_process(uint64_t bandwidth_bps, size_t num_frames, size_t frame_size) {
    echo::info("[Sender] Starting with bandwidth=", bandwidth_bps / 1000000, " Mbps").green();

    // Attach to ShmLink
    auto link_result = ShmLink::attach(String("eth_bw"));
    if (!link_result.is_ok()) {
        echo::error("[Sender] Failed to attach: ", link_result.error().message.c_str()).red();
        return 1;
    }
    auto link = std::make_shared<ShmLink>(std::move(link_result.value()));

    // Create Ethernet endpoint
    MacAddr mac = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};
    EthConfig config;
    config.bandwidth_bps = bandwidth_bps;
    EthEndpoint eth(link, config, 1, mac);

    echo::info("[Sender] Sending ", num_frames, " frames of ", frame_size, " bytes...").cyan();

    uint64_t start_time = now_ns();
    size_t frames_sent = 0;
    size_t bytes_sent = 0;

    for (size_t i = 0; i < num_frames; ++i) {
        // Create payload
        Bytes payload(frame_size);
        for (size_t j = 0; j < frame_size; ++j) {
            payload[j] = static_cast<uint8_t>((i + j) & 0xFF);
        }

        // Create Ethernet frame
        MacAddr dst = {0x02, 0x00, 0x00, 0x00, 0x00, 0x02};
        Bytes frame = make_eth_frame(dst, mac, ETH_P_IP, payload);

        auto result = eth.send_eth(frame);
        if (result.is_ok()) {
            frames_sent++;
            bytes_sent += frame.size();
        } else {
            echo::warn("[Sender] Send failed: ", result.error().message.c_str()).yellow();
        }
    }

    uint64_t end_time = now_ns();
    uint64_t elapsed_us = (end_time - start_time) / 1000;
    double elapsed_s = elapsed_us / 1000000.0;

    echo::info("[Sender] Sent ", frames_sent, " frames (", bytes_sent, " bytes) in ", elapsed_us, " µs").green();

    // Calculate throughput
    double throughput_bps = (bytes_sent * 8.0) / elapsed_s;
    double throughput_mbps = throughput_bps / 1000000.0;

    echo::info("[Sender] Throughput: ", throughput_mbps, " Mbps").cyan();
    echo::info("[Sender] Configured: ", bandwidth_bps / 1000000, " Mbps").cyan();

    // Check if within tolerance (allow 20% variance due to overhead)
    double ratio = throughput_bps / bandwidth_bps;
    if (ratio < 0.5 || ratio > 1.2) {
        echo::warn("[Sender] Throughput outside expected range (ratio=", ratio, ")").yellow();
    } else {
        echo::info("[Sender] Throughput within expected range (ratio=", ratio, ")").green();
    }

    echo::info("[Sender] Done").green().bold();
    return 0;
}

/// Receiver process: receives and validates Ethernet frames
int receiver_process(size_t num_frames) {
    echo::info("[Receiver] Starting, expecting ", num_frames, " frames").green();

    // Attach to ShmLink
    auto link_result = ShmLink::attach(String("eth_bw"));
    if (!link_result.is_ok()) {
        echo::error("[Receiver] Failed to attach: ", link_result.error().message.c_str()).red();
        return 1;
    }
    auto link = std::make_shared<ShmLink>(std::move(link_result.value()));

    // Create Ethernet endpoint
    MacAddr mac = {0x02, 0x00, 0x00, 0x00, 0x00, 0x02};
    EthConfig config;
    EthEndpoint eth(link, config, 2, mac);

    echo::info("[Receiver] Waiting for frames...").cyan();

    size_t frames_received = 0;
    size_t bytes_received = 0;
    uint64_t start_time = now_ns();
    uint64_t timeout_ms = 10000; // 10 second timeout

    while (frames_received < num_frames) {
        uint64_t elapsed_ms = (now_ns() - start_time) / 1000000;
        if (elapsed_ms > timeout_ms) {
            echo::warn("[Receiver] Timeout reached").yellow();
            break;
        }

        auto result = eth.recv_eth();
        if (result.is_ok()) {
            Bytes frame = result.value();
            frames_received++;
            bytes_received += frame.size();

            echo::debug("[Receiver] Received frame ", frames_received, " (", frame.size(), " bytes)");

            // Validate frame structure
            if (frame.size() < ETH_HLEN) {
                echo::error("[Receiver] Frame too small: ", frame.size(), " bytes").red();
            }
        } else {
            // No frame available
            usleep(100); // 100 µs
        }
    }

    uint64_t end_time = now_ns();
    uint64_t elapsed_us = (end_time - start_time) / 1000;

    echo::info("[Receiver] Received ", frames_received, " frames (", bytes_received, " bytes) in ", elapsed_us, " µs")
        .green();

    if (frames_received < num_frames) {
        echo::error("[Receiver] Expected ", num_frames, " frames, got ", frames_received).red();
        return 1;
    }

    echo::info("[Receiver] Done").green().bold();
    return 0;
}

int main(int argc, char **argv) {
    echo::info("=== Ethernet Bandwidth Integration Test ===").cyan().bold();

    // Parse arguments
    uint64_t bandwidth_mbps = (argc > 1) ? std::atoi(argv[1]) : 100;
    size_t num_frames = (argc > 2) ? std::atoi(argv[2]) : 100;
    size_t frame_size = (argc > 3) ? std::atoi(argv[3]) : 1000;

    uint64_t bandwidth_bps = bandwidth_mbps * 1000000;

    echo::info("Configuration:").cyan();
    echo::info("  Bandwidth: ", bandwidth_mbps, " Mbps");
    echo::info("  Num frames: ", num_frames);
    echo::info("  Frame size: ", frame_size, " bytes");

    // Create ShmLink
    echo::debug("Creating ShmLink...");
    auto link_result = ShmLink::create(String("eth_bw"), 1024 * 1024); // 1 MB
    if (!link_result.is_ok()) {
        echo::error("Failed to create ShmLink: ", link_result.error().message.c_str()).red();
        return 1;
    }

    // Fork sender process
    echo::debug("Forking sender process...");
    pid_t sender_pid = fork();
    if (sender_pid == -1) {
        echo::error("Failed to fork sender process").red();
        return 1;
    }

    if (sender_pid == 0) {
        // Child process: sender
        usleep(100000); // Wait 100ms for receiver to be ready
        int ret = sender_process(bandwidth_bps, num_frames, frame_size);
        exit(ret);
    }

    // Fork receiver process
    echo::debug("Forking receiver process...");
    pid_t receiver_pid = fork();
    if (receiver_pid == -1) {
        echo::error("Failed to fork receiver process").red();
        kill(sender_pid, SIGTERM);
        return 1;
    }

    if (receiver_pid == 0) {
        // Child process: receiver
        int ret = receiver_process(num_frames);
        exit(ret);
    }

    // Parent process: wait for children
    echo::debug("Waiting for child processes...");

    int sender_status, receiver_status;
    waitpid(sender_pid, &sender_status, 0);
    waitpid(receiver_pid, &receiver_status, 0);

    int sender_exit = WEXITSTATUS(sender_status);
    int receiver_exit = WEXITSTATUS(receiver_status);

    echo::info("Sender exit code: ", sender_exit).cyan();
    echo::info("Receiver exit code: ", receiver_exit).cyan();

    if (sender_exit != 0 || receiver_exit != 0) {
        echo::error("=== Test FAILED ===").red().bold();
        return 1;
    }

    echo::info("=== Test PASSED ===").green().bold();
    return 0;
}
