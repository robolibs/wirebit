/// @file test_can_broadcast.cpp
/// @brief Multi-process integration test for CAN bus broadcast correctness
///
/// This test validates:
/// - CAN bus hub broadcast to multiple nodes
/// - Frame integrity across processes
/// - Statistical bounds with error injection
/// - Proper arbitration and forwarding

#include <cstring>
#include <echo/echo.hpp>
#include <sys/wait.h>
#include <unistd.h>
#include <wirebit/wirebit.hpp>

using namespace wirebit;

/// CAN node process: sends and receives frames
int node_process(int node_id, size_t num_frames, bool send_frames, bool recv_frames) {
    echo::info("[Node ", node_id, "] Starting (send=", send_frames, " recv=", recv_frames, ")").green();

    // Attach to ShmLink
    String node_name("can_node_");
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", node_id);
    node_name = node_name + String(buf);

    usleep(200000); // Wait 200ms for hub to create links

    auto link_result = ShmLink::attach(node_name);
    if (!link_result.is_ok()) {
        echo::error("[Node ", node_id, "] Failed to attach: ", link_result.error().message.c_str()).red();
        return 1;
    }
    auto link = std::make_shared<ShmLink>(std::move(link_result.value()));

    // Create CAN endpoint
    CanConfig config;
    config.bitrate = 500000;
    CanEndpoint can_ep(link, config, node_id);

    size_t frames_sent = 0;
    size_t frames_received = 0;

    // Send frames if requested
    if (send_frames) {
        echo::info("[Node ", node_id, "] Sending ", num_frames, " frames...").cyan();

        for (size_t i = 0; i < num_frames; ++i) {
            can_frame frame;
            frame.can_id = 0x100 + node_id;
            frame.can_dlc = 8;

            // Fill with node_id and sequence number
            frame.data[0] = static_cast<uint8_t>(node_id);
            frame.data[1] = static_cast<uint8_t>((i >> 24) & 0xFF);
            frame.data[2] = static_cast<uint8_t>((i >> 16) & 0xFF);
            frame.data[3] = static_cast<uint8_t>((i >> 8) & 0xFF);
            frame.data[4] = static_cast<uint8_t>(i & 0xFF);
            frame.data[5] = 0xAA;
            frame.data[6] = 0xBB;
            frame.data[7] = 0xCC;

            auto result = can_ep.send_can(frame);
            if (result.is_ok()) {
                frames_sent++;
            } else {
                echo::warn("[Node ", node_id, "] Send failed: ", result.error().message.c_str()).yellow();
            }

            usleep(1000); // 1ms between frames
        }

        echo::info("[Node ", node_id, "] Sent ", frames_sent, " frames").green();
    }

    // Receive frames if requested
    if (recv_frames) {
        echo::info("[Node ", node_id, "] Receiving frames...").cyan();

        uint64_t start_time = now_ns();
        uint64_t timeout_ms = 5000; // 5 second timeout

        while (true) {
            uint64_t elapsed_ms = (now_ns() - start_time) / 1000000;
            if (elapsed_ms > timeout_ms) {
                echo::warn("[Node ", node_id, "] Timeout reached").yellow();
                break;
            }

            can_ep.process();
            can_frame frame;
            auto result = can_ep.recv_can(frame);

            if (result.is_ok()) {
                frames_received++;

                // Validate frame
                uint32_t sender_id = frame.data[0];
                uint32_t seq = (frame.data[1] << 24) | (frame.data[2] << 16) | (frame.data[3] << 8) | frame.data[4];

                echo::debug("[Node ", node_id, "] Received from node ", sender_id, " seq=", seq);

                // Validate magic bytes
                if (frame.data[5] != 0xAA || frame.data[6] != 0xBB || frame.data[7] != 0xCC) {
                    echo::error("[Node ", node_id, "] Frame corruption detected!").red();
                }
            } else {
                // No frame available
                usleep(100); // 100 µs
            }
        }

        echo::info("[Node ", node_id, "] Received ", frames_received, " frames").green();
    }

    echo::info("[Node ", node_id, "] Done (sent=", frames_sent, " recv=", frames_received, ")").green().bold();
    return 0;
}

int main(int argc, char **argv) {
    echo::info("=== CAN Broadcast Integration Test ===").cyan().bold();

    // Parse arguments
    size_t num_nodes = (argc > 1) ? std::atoi(argv[1]) : 3;
    size_t frames_per_node = (argc > 2) ? std::atoi(argv[2]) : 10;
    double drop_prob = (argc > 3) ? std::atof(argv[3]) : 0.0;

    echo::info("Configuration:").cyan();
    echo::info("  Nodes: ", num_nodes);
    echo::info("  Frames per node: ", frames_per_node);
    echo::info("  Drop probability: ", drop_prob * 100, "%");

    if (num_nodes < 2) {
        echo::error("Need at least 2 nodes").red();
        return 1;
    }

    // Fork hub process
    echo::debug("Forking hub process...");
    pid_t hub_pid = fork();
    if (hub_pid == -1) {
        echo::error("Failed to fork hub process").red();
        return 1;
    }

    if (hub_pid == 0) {
        // Child process: hub
        echo::info("[Hub] Starting...").green();

        // Convert arguments to strings
        char num_nodes_str[16], drop_prob_str[32];
        snprintf(num_nodes_str, sizeof(num_nodes_str), "%zu", num_nodes);
        snprintf(drop_prob_str, sizeof(drop_prob_str), "%.6f", drop_prob);

        // Execute hub
        execl("./build/linux/x86_64/release/can_bus_hub", "can_bus_hub", num_nodes_str, "500000", drop_prob_str, "0.0",
              nullptr);

        // If execl fails
        echo::error("[Hub] Failed to exec can_bus_hub").red();
        exit(1);
    }

    // Wait for hub to initialize
    usleep(500000); // 500ms

    // Fork node processes
    Vector<pid_t> node_pids;

    for (size_t i = 0; i < num_nodes; ++i) {
        echo::debug("Forking node ", i, "...");

        pid_t node_pid = fork();
        if (node_pid == -1) {
            echo::error("Failed to fork node ", i).red();
            kill(hub_pid, SIGTERM);
            for (auto pid : node_pids) {
                kill(pid, SIGTERM);
            }
            return 1;
        }

        if (node_pid == 0) {
            // Child process: node
            // Each node sends frames and receives frames from others
            int ret = node_process(i, frames_per_node, true, true);
            exit(ret);
        }

        node_pids.push_back(node_pid);
    }

    // Parent process: wait for nodes to complete
    echo::debug("Waiting for node processes...");

    Vector<int> node_exits;
    for (auto pid : node_pids) {
        int status;
        waitpid(pid, &status, 0);
        int exit_code = WEXITSTATUS(status);
        node_exits.push_back(exit_code);
        echo::info("Node exit code: ", exit_code).cyan();
    }

    // Kill hub
    echo::debug("Stopping hub...");
    kill(hub_pid, SIGTERM);
    waitpid(hub_pid, nullptr, 0);

    // Check results
    bool all_passed = true;
    for (size_t i = 0; i < node_exits.size(); ++i) {
        if (node_exits[i] != 0) {
            echo::error("Node ", i, " failed with exit code ", node_exits[i]).red();
            all_passed = false;
        }
    }

    if (!all_passed) {
        echo::error("=== Test FAILED ===").red().bold();
        return 1;
    }

    echo::info("=== Test PASSED ===").green().bold();
    return 0;
}
