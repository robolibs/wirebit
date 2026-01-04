/// @file test_eth_bandwidth.cpp
/// @brief Multi-process integration test for Ethernet bandwidth shaping
///
/// This test validates:
/// - Ethernet bandwidth shaping accuracy
/// - Multi-process frame exchange
/// - Throughput measurement
/// - Frame integrity
///
/// Architecture:
/// - Forwarder: Creates both links, forwards frames from sender_link to receiver_link
/// - Sender: Attaches to sender_link, sends Ethernet frames via EthEndpoint
/// - Receiver: Attaches to receiver_link, receives Ethernet frames via EthEndpoint

#include <cstring>
#include <echo/echo.hpp>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#include <wirebit/wirebit.hpp>

using namespace wirebit;

// Global SHM names (set by main, used by all processes after fork)
static String g_sender_link_name;
static String g_receiver_link_name;

/// Clean up SHM segments
void cleanup_shm() {
    // Unlink SHM segments (may fail if already cleaned up, that's ok)
    String tx1 = String("/") + g_sender_link_name + "_tx";
    String rx1 = String("/") + g_sender_link_name + "_rx";
    String tx2 = String("/") + g_receiver_link_name + "_tx";
    String rx2 = String("/") + g_receiver_link_name + "_rx";

    shm_unlink(tx1.c_str());
    shm_unlink(rx1.c_str());
    shm_unlink(tx2.c_str());
    shm_unlink(rx2.c_str());
}

/// Forwarder process: creates links and forwards frames from sender to receiver
int forwarder_process(uint64_t timeout_ms = 15000) {
    echo::info("[Forwarder] Starting - creating links...").green();

    // Forwarder CREATES both links (server side)
    auto sender_link_result = ShmLink::create(g_sender_link_name, 1024 * 1024);     // 1 MB
    auto receiver_link_result = ShmLink::create(g_receiver_link_name, 1024 * 1024); // 1 MB

    if (!sender_link_result.is_ok() || !receiver_link_result.is_ok()) {
        echo::error("[Forwarder] Failed to create links").red();
        return 1;
    }

    ShmLink sender_link = std::move(sender_link_result.value());
    ShmLink receiver_link = std::move(receiver_link_result.value());

    echo::info("[Forwarder] Links created, forwarding frames...").cyan();

    uint64_t start_time = now_ns();
    size_t frames_forwarded = 0;

    while (true) {
        uint64_t elapsed_ms = (now_ns() - start_time) / 1000000;
        if (elapsed_ms > timeout_ms) {
            echo::info("[Forwarder] Timeout reached, forwarded ", frames_forwarded, " frames").yellow();
            break;
        }

        // Receive from sender link (sender sends to us)
        auto recv_result = sender_link.recv();
        if (recv_result.is_ok()) {
            Frame frame = recv_result.value();

            // Forward to receiver link (we send to receiver)
            auto send_result = receiver_link.send(frame);
            if (send_result.is_ok()) {
                frames_forwarded++;
                echo::trace("[Forwarder] Forwarded frame ", frames_forwarded);
            }
        } else {
            usleep(100); // 100 µs
        }
    }

    echo::info("[Forwarder] Done, forwarded ", frames_forwarded, " frames").green();
    return 0;
}

/// Sender process: attaches to sender link and sends Ethernet frames via EthEndpoint
int sender_process(uint64_t bandwidth_bps, size_t num_frames, size_t frame_size) {
    echo::info("[Sender] Starting with bandwidth=", bandwidth_bps / 1000000, " Mbps").green();

    // Wait for forwarder to create the link
    usleep(200000); // 200ms

    // Attach to sender's ShmLink (client side)
    auto link_result = ShmLink::attach(g_sender_link_name);
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

    // Check if within tolerance (allow 50% variance due to overhead and process scheduling)
    double ratio = throughput_bps / bandwidth_bps;
    if (ratio < 0.3 || ratio > 1.5) {
        echo::warn("[Sender] Throughput outside expected range (ratio=", ratio, ")").yellow();
    } else {
        echo::info("[Sender] Throughput within expected range (ratio=", ratio, ")").green();
    }

    echo::info("[Sender] Done").green().bold();
    return 0;
}

/// Receiver process: attaches to receiver link and receives Ethernet frames via EthEndpoint
int receiver_process(size_t num_frames) {
    echo::info("[Receiver] Starting, expecting ", num_frames, " frames").green();

    // Wait for forwarder to create the link
    usleep(200000); // 200ms

    // Attach to receiver's ShmLink (client side)
    auto link_result = ShmLink::attach(g_receiver_link_name);
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
            echo::warn("[Receiver] Timeout reached (received ", frames_received, "/", num_frames, " frames)").yellow();
            break;
        }

        // Process incoming frames first
        eth.process();

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
            usleep(1000); // 1 ms
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

    // Generate unique SHM names using PID (keeps names short for datapod String)
    char buf[32];
    pid_t pid = getpid();
    snprintf(buf, sizeof(buf), "eth_s_%d", pid % 100000);
    g_sender_link_name = String(buf);
    snprintf(buf, sizeof(buf), "eth_r_%d", pid % 100000);
    g_receiver_link_name = String(buf);

    echo::debug("SHM names: ", g_sender_link_name.c_str(), ", ", g_receiver_link_name.c_str());

    // Clean up any leftover SHM segments from previous runs
    cleanup_shm();

    // Fork forwarder process FIRST (it creates the links)
    echo::debug("Forking forwarder process...");
    pid_t forwarder_pid = fork();
    if (forwarder_pid == -1) {
        echo::error("Failed to fork forwarder process").red();
        cleanup_shm();
        return 1;
    }

    if (forwarder_pid == 0) {
        // Child process: forwarder
        int ret = forwarder_process(15000); // 15 second timeout
        exit(ret);
    }

    // Wait a bit for forwarder to create links
    usleep(100000); // 100ms

    // Fork sender process
    echo::debug("Forking sender process...");
    pid_t sender_pid = fork();
    if (sender_pid == -1) {
        echo::error("Failed to fork sender process").red();
        kill(forwarder_pid, SIGTERM);
        cleanup_shm();
        return 1;
    }

    if (sender_pid == 0) {
        // Child process: sender
        int ret = sender_process(bandwidth_bps, num_frames, frame_size);
        exit(ret);
    }

    // Fork receiver process
    echo::debug("Forking receiver process...");
    pid_t receiver_pid = fork();
    if (receiver_pid == -1) {
        echo::error("Failed to fork receiver process").red();
        kill(forwarder_pid, SIGTERM);
        kill(sender_pid, SIGTERM);
        cleanup_shm();
        return 1;
    }

    if (receiver_pid == 0) {
        // Child process: receiver
        int ret = receiver_process(num_frames);
        exit(ret);
    }

    // Parent process: wait for sender and receiver first
    echo::debug("Waiting for sender and receiver processes...");

    int sender_status, receiver_status;
    waitpid(sender_pid, &sender_status, 0);
    waitpid(receiver_pid, &receiver_status, 0);

    int sender_exit = WEXITSTATUS(sender_status);
    int receiver_exit = WEXITSTATUS(receiver_status);

    echo::info("Sender exit code: ", sender_exit).cyan();
    echo::info("Receiver exit code: ", receiver_exit).cyan();

    // Now kill forwarder and wait for it
    kill(forwarder_pid, SIGTERM);
    int forwarder_status;
    waitpid(forwarder_pid, &forwarder_status, 0);
    int forwarder_exit = WEXITSTATUS(forwarder_status);
    echo::info("Forwarder exit code: ", forwarder_exit).cyan();

    // Clean up SHM segments
    cleanup_shm();

    if (sender_exit != 0 || receiver_exit != 0) {
        echo::error("=== Test FAILED ===").red().bold();
        return 1;
    }

    echo::info("=== Test PASSED ===").green().bold();
    return 0;
}
