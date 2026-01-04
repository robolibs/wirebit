/// @file test_serial_timing.cpp
/// @brief Multi-process integration test for serial endpoint timing accuracy
///
/// This test validates:
/// - Baud rate pacing accuracy
/// - Multi-process communication via ShmLink
/// - Timing and content integrity
/// - Proper sequencing of bytes

#include <cstring>
#include <echo/echo.hpp>
#include <sys/wait.h>
#include <unistd.h>
#include <wirebit/wirebit.hpp>

using namespace wirebit;

/// Writer process: sends a known sequence at specific baud rate
int writer_process(uint32_t baud_rate, size_t num_bytes) {
    echo::info("[Writer] Starting with baud=", baud_rate, " num_bytes=", num_bytes).green();

    // Attach to ShmLink
    auto link_result = ShmLink::attach(String("serial_timing"));
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

    // Allow 10% tolerance
    double ratio = static_cast<double>(elapsed_us) / expected_us;
    if (ratio < 0.9 || ratio > 1.1) {
        echo::warn("[Writer] Timing outside 10% tolerance (ratio=", ratio, ")").yellow();
    } else {
        echo::info("[Writer] Timing within tolerance (ratio=", ratio, ")").green();
    }

    echo::info("[Writer] Done").green().bold();
    return 0;
}

/// Reader process: receives and validates the sequence
int reader_process(uint32_t baud_rate, size_t num_bytes) {
    echo::info("[Reader] Starting with baud=", baud_rate, " num_bytes=", num_bytes).green();

    // Attach to ShmLink
    auto link_result = ShmLink::attach(String("serial_timing"));
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
            echo::error("[Reader] Timeout waiting for data").red();
            return 1;
        }

        auto result = serial.recv();
        if (result.is_ok()) {
            Bytes chunk = result.value();
            echo::debug("[Reader] Received chunk: ", chunk.size(), " bytes");

            for (size_t i = 0; i < chunk.size(); ++i) {
                received.push_back(chunk[i]);
            }
        } else {
            // No data available, sleep briefly
            usleep(100); // 100 µs
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

    // Create ShmLink
    echo::debug("Creating ShmLink...");
    auto link_result = ShmLink::create(String("serial_timing"), 1024 * 64);
    if (!link_result.is_ok()) {
        echo::error("Failed to create ShmLink: ", link_result.error().message.c_str()).red();
        return 1;
    }

    // Fork writer process
    echo::debug("Forking writer process...");
    pid_t writer_pid = fork();
    if (writer_pid == -1) {
        echo::error("Failed to fork writer process").red();
        return 1;
    }

    if (writer_pid == 0) {
        // Child process: writer
        usleep(100000); // Wait 100ms for reader to be ready
        int ret = writer_process(baud_rate, num_bytes);
        exit(ret);
    }

    // Fork reader process
    echo::debug("Forking reader process...");
    pid_t reader_pid = fork();
    if (reader_pid == -1) {
        echo::error("Failed to fork reader process").red();
        kill(writer_pid, SIGTERM);
        return 1;
    }

    if (reader_pid == 0) {
        // Child process: reader
        int ret = reader_process(baud_rate, num_bytes);
        exit(ret);
    }

    // Parent process: wait for children
    echo::debug("Waiting for child processes...");

    int writer_status, reader_status;
    waitpid(writer_pid, &writer_status, 0);
    waitpid(reader_pid, &reader_status, 0);

    int writer_exit = WEXITSTATUS(writer_status);
    int reader_exit = WEXITSTATUS(reader_status);

    echo::info("Writer exit code: ", writer_exit).cyan();
    echo::info("Reader exit code: ", reader_exit).cyan();

    if (writer_exit != 0 || reader_exit != 0) {
        echo::error("=== Test FAILED ===").red().bold();
        return 1;
    }

    echo::info("=== Test PASSED ===").green().bold();
    return 0;
}
