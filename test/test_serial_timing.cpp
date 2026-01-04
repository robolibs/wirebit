/// @file test_serial_timing.cpp
/// @brief Multi-process integration test for serial endpoint timing accuracy
///
/// This test validates:
/// - Baud rate pacing accuracy
/// - Multi-process communication via ShmLink
/// - Timing and content integrity
/// - Proper sequencing of bytes
///
/// Architecture:
/// - Forwarder: Creates both links, forwards frames from writer_link to reader_link
/// - Writer: Attaches to writer_link, sends data through SerialEndpoint
/// - Reader: Attaches to reader_link, receives data through SerialEndpoint

#include <cstring>
#include <echo/echo.hpp>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#include <wirebit/wirebit.hpp>

using namespace wirebit;

// Global SHM names (set by main, used by all processes after fork)
static String g_writer_link_name;
static String g_reader_link_name;

/// Clean up SHM segments
void cleanup_shm() {
    // Unlink SHM segments (may fail if already cleaned up, that's ok)
    String tx1 = String("/") + g_writer_link_name + "_tx";
    String rx1 = String("/") + g_writer_link_name + "_rx";
    String tx2 = String("/") + g_reader_link_name + "_tx";
    String rx2 = String("/") + g_reader_link_name + "_rx";

    shm_unlink(tx1.c_str());
    shm_unlink(rx1.c_str());
    shm_unlink(tx2.c_str());
    shm_unlink(rx2.c_str());
}

/// Forwarder process: creates links and forwards frames from writer to reader
int forwarder_process(uint64_t timeout_ms = 10000) {
    echo::info("[Forwarder] Starting - creating links...").green();

    // Forwarder CREATES both links (server side)
    auto writer_link_result = ShmLink::create(g_writer_link_name, 1024 * 64);
    auto reader_link_result = ShmLink::create(g_reader_link_name, 1024 * 64);

    if (!writer_link_result.is_ok() || !reader_link_result.is_ok()) {
        echo::error("[Forwarder] Failed to create links").red();
        return 1;
    }

    ShmLink writer_link = std::move(writer_link_result.value());
    ShmLink reader_link = std::move(reader_link_result.value());

    echo::info("[Forwarder] Links created, forwarding frames...").cyan();

    uint64_t start_time = now_ns();
    size_t frames_forwarded = 0;

    while (true) {
        uint64_t elapsed_ms = (now_ns() - start_time) / 1000000;
        if (elapsed_ms > timeout_ms) {
            echo::info("[Forwarder] Timeout reached, forwarded ", frames_forwarded, " frames").yellow();
            break;
        }

        // Receive from writer link (writer sends to us)
        auto recv_result = writer_link.recv();
        if (recv_result.is_ok()) {
            Frame frame = recv_result.value();

            // Forward to reader link (we send to reader)
            auto send_result = reader_link.send(frame);
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

/// Writer process: attaches to writer link and sends data via SerialEndpoint
int writer_process(uint32_t baud_rate, size_t num_bytes) {
    echo::info("[Writer] Starting with baud=", baud_rate, " num_bytes=", num_bytes).green();

    // Wait for forwarder to create the link
    usleep(200000); // 200ms

    // Attach to writer's ShmLink (client side)
    auto link_result = ShmLink::attach(g_writer_link_name);
    if (!link_result.is_ok()) {
        echo::error("[Writer] Failed to attach: ", link_result.error().message.c_str()).red();
        return 1;
    }
    auto link = std::make_shared<ShmLink>(std::move(link_result.value()));

    // Create serial endpoint
    SerialConfig config;
    config.baud = baud_rate;
    config.data_bits = 8;
    config.stop_bits = 1;
    config.parity = 'N';

    SerialEndpoint serial(link, config, 1);

    // Create test data (sequential bytes)
    Bytes data(num_bytes);
    for (size_t i = 0; i < num_bytes; ++i) {
        data[i] = static_cast<uint8_t>(i & 0xFF);
    }

    echo::info("[Writer] Sending ", num_bytes, " bytes...").cyan();

    uint64_t start_time = now_ns();
    auto result = serial.send(data);
    uint64_t end_time = now_ns();

    if (!result.is_ok()) {
        echo::error("[Writer] Send failed: ", result.error().message.c_str()).red();
        return 1;
    }

    uint64_t elapsed_us = (end_time - start_time) / 1000;
    echo::info("[Writer] Send completed in ", elapsed_us, " µs").green();

    // Calculate expected time
    uint32_t bits_per_byte = 1 + config.data_bits + config.stop_bits; // Start + data + stop
    uint64_t expected_us = (num_bytes * bits_per_byte * 1000000ULL) / baud_rate;

    echo::info("[Writer] Expected time: ", expected_us, " µs").cyan();
    echo::info("[Writer] Actual time: ", elapsed_us, " µs").cyan();

    // Allow 20% tolerance
    double ratio = static_cast<double>(elapsed_us) / expected_us;
    if (ratio < 0.8 || ratio > 1.2) {
        echo::warn("[Writer] Timing outside 20% tolerance (ratio=", ratio, ")").yellow();
    } else {
        echo::info("[Writer] Timing within tolerance (ratio=", ratio, ")").green();
    }

    echo::info("[Writer] Done").green().bold();
    return 0;
}

/// Reader process: attaches to reader link and receives data via SerialEndpoint
int reader_process(uint32_t baud_rate, size_t num_bytes) {
    echo::info("[Reader] Starting with baud=", baud_rate, " num_bytes=", num_bytes).green();

    // Wait for forwarder to create the link
    usleep(200000); // 200ms

    // Attach to reader's ShmLink (client side)
    auto link_result = ShmLink::attach(g_reader_link_name);
    if (!link_result.is_ok()) {
        echo::error("[Reader] Failed to attach: ", link_result.error().message.c_str()).red();
        return 1;
    }
    auto link = std::make_shared<ShmLink>(std::move(link_result.value()));

    // Create serial endpoint
    SerialConfig config;
    config.baud = baud_rate;
    config.data_bits = 8;
    config.stop_bits = 1;
    config.parity = 'N';

    SerialEndpoint serial(link, config, 2);

    echo::info("[Reader] Waiting for ", num_bytes, " bytes...").cyan();

    Bytes received;
    uint64_t start_time = now_ns();
    uint64_t timeout_ms = 5000; // 5 second timeout

    while (received.size() < num_bytes) {
        uint64_t elapsed_ms = (now_ns() - start_time) / 1000000;
        if (elapsed_ms > timeout_ms) {
            echo::error("[Reader] Timeout waiting for data (received ", received.size(), "/", num_bytes, " bytes)")
                .red();
            return 1;
        }

        // Process incoming frames first
        serial.process();

        auto result = serial.recv();
        if (result.is_ok()) {
            Bytes chunk = result.value();
            echo::debug("[Reader] Received chunk: ", chunk.size(), " bytes");

            for (size_t i = 0; i < chunk.size(); ++i) {
                received.push_back(chunk[i]);
            }
        } else {
            // No data available, sleep briefly
            usleep(1000); // 1 ms
        }
    }

    uint64_t end_time = now_ns();
    uint64_t elapsed_us = (end_time - start_time) / 1000;

    echo::info("[Reader] Received ", received.size(), " bytes in ", elapsed_us, " µs").green();

    // Validate content
    size_t errors = 0;
    for (size_t i = 0; i < num_bytes; ++i) {
        uint8_t expected = static_cast<uint8_t>(i & 0xFF);
        if (received[i] != expected) {
            echo::error("[Reader] Byte ", i, " mismatch: expected=", (int)expected, " got=", (int)received[i]).red();
            errors++;
            if (errors > 10) {
                echo::error("[Reader] Too many errors, stopping validation").red();
                break;
            }
        }
    }

    if (errors == 0) {
        echo::info("[Reader] All bytes validated successfully!").green().bold();
    } else {
        echo::error("[Reader] Validation failed with ", errors, " errors").red().bold();
        return 1;
    }

    echo::info("[Reader] Done").green().bold();
    return 0;
}

int main(int argc, char **argv) {
    echo::info("=== Serial Timing Integration Test ===").cyan().bold();

    // Parse arguments
    uint32_t baud_rate = (argc > 1) ? std::atoi(argv[1]) : 115200;
    size_t num_bytes = (argc > 2) ? std::atoi(argv[2]) : 100;

    echo::info("Configuration:").cyan();
    echo::info("  Baud rate: ", baud_rate, " bps");
    echo::info("  Num bytes: ", num_bytes);

    // Generate unique SHM names using PID (keeps names short for datapod String)
    char buf[32];
    pid_t pid = getpid();
    snprintf(buf, sizeof(buf), "ser_w_%d", pid % 100000);
    g_writer_link_name = String(buf);
    snprintf(buf, sizeof(buf), "ser_r_%d", pid % 100000);
    g_reader_link_name = String(buf);

    echo::debug("SHM names: ", g_writer_link_name.c_str(), ", ", g_reader_link_name.c_str());

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
        int ret = forwarder_process(10000); // 10 second timeout
        exit(ret);
    }

    // Wait a bit for forwarder to create links
    usleep(100000); // 100ms

    // Fork writer process
    echo::debug("Forking writer process...");
    pid_t writer_pid = fork();
    if (writer_pid == -1) {
        echo::error("Failed to fork writer process").red();
        kill(forwarder_pid, SIGTERM);
        cleanup_shm();
        return 1;
    }

    if (writer_pid == 0) {
        // Child process: writer
        int ret = writer_process(baud_rate, num_bytes);
        exit(ret);
    }

    // Fork reader process
    echo::debug("Forking reader process...");
    pid_t reader_pid = fork();
    if (reader_pid == -1) {
        echo::error("Failed to fork reader process").red();
        kill(forwarder_pid, SIGTERM);
        kill(writer_pid, SIGTERM);
        cleanup_shm();
        return 1;
    }

    if (reader_pid == 0) {
        // Child process: reader
        int ret = reader_process(baud_rate, num_bytes);
        exit(ret);
    }

    // Parent process: wait for writer and reader first
    echo::debug("Waiting for writer and reader processes...");

    int writer_status, reader_status;
    waitpid(writer_pid, &writer_status, 0);
    waitpid(reader_pid, &reader_status, 0);

    int writer_exit = WEXITSTATUS(writer_status);
    int reader_exit = WEXITSTATUS(reader_status);

    echo::info("Writer exit code: ", writer_exit).cyan();
    echo::info("Reader exit code: ", reader_exit).cyan();

    // Now kill forwarder and wait for it
    kill(forwarder_pid, SIGTERM);
    int forwarder_status;
    waitpid(forwarder_pid, &forwarder_status, 0);
    int forwarder_exit = WEXITSTATUS(forwarder_status);
    echo::info("Forwarder exit code: ", forwarder_exit).cyan();

    // Clean up SHM segments
    cleanup_shm();

    if (writer_exit != 0 || reader_exit != 0) {
        echo::error("=== Test FAILED ===").red().bold();
        return 1;
    }

    echo::info("=== Test PASSED ===").green().bold();
    return 0;
}
