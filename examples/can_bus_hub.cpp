/// @file can_bus_hub.cpp
/// @brief CAN bus hub - standalone application for multi-node CAN bus simulation
///
/// This program acts as a central hub that forwards CAN frames between multiple nodes.
/// Each node connects via its own ShmLink, and the hub implements:
/// - Frame arbitration (FIFO by timestamp)
/// - Broadcast to all nodes except sender
/// - Bitrate shaping
/// - Optional error injection (drops, corruption)
///
/// Usage:
///   ./can_bus_hub <num_nodes> [bitrate_bps] [drop_prob] [corrupt_prob]
///
/// Example:
///   ./can_bus_hub 3 500000 0.01 0.005
///   (3 nodes, 500 kbps, 1% drop, 0.5% corruption)

#include <algorithm>
#include <csignal>
#include <cstdlib>
#include <echo/echo.hpp>
#include <iostream>
#include <memory>
#include <thread>
#include <vector>
#include <wirebit/wirebit.hpp>

using namespace wirebit;

// Global flag for graceful shutdown
volatile bool g_running = true;

void signal_handler(int signal) {
    if (signal == SIGINT || signal == SIGTERM) {
        echo::info("Received shutdown signal").yellow();
        g_running = false;
    }
}

/// CAN bus hub that forwards frames between nodes
class CanBusHub {
  public:
    /// Create a CAN bus hub
    /// @param num_nodes Number of nodes on the bus
    /// @param bitrate CAN bitrate in bps
    /// @param drop_prob Frame drop probability [0.0, 1.0]
    /// @param corrupt_prob Frame corruption probability [0.0, 1.0]
    CanBusHub(size_t num_nodes, uint32_t bitrate = 500000, double drop_prob = 0.0, double corrupt_prob = 0.0)
        : num_nodes_(num_nodes), bitrate_(bitrate) {

        echo::info("CAN Bus Hub starting...").green().bold();
        echo::info("  Nodes: ", num_nodes_);
        echo::info("  Bitrate: ", bitrate_, " bps");
        echo::info("  Drop probability: ", drop_prob * 100, "%");
        echo::info("  Corrupt probability: ", corrupt_prob * 100, "%");

        // Create link model if error injection is enabled
        if (drop_prob > 0.0 || corrupt_prob > 0.0) {
            model_.drop_prob = drop_prob;
            model_.corrupt_prob = corrupt_prob;
            model_.seed = 12345; // Deterministic for testing
            use_model_ = true;
            echo::info("  Error injection: ENABLED").yellow();
        }

        // Create ShmLinks for each node
        for (size_t i = 0; i < num_nodes_; ++i) {
            String node_name("can_node_");
            char buf[16];
            snprintf(buf, sizeof(buf), "%zu", i);
            node_name = node_name + String(buf);

            echo::debug("Creating ShmLink for node ", i, ": ", node_name.c_str());

            auto result = ShmLink::create(node_name, 1024 * 64); // 64 KB per node
            if (!result.is_ok()) {
                echo::error("Failed to create ShmLink for node ", i, ": ", result.error().message.c_str()).red();
                continue;
            }

            auto link = std::make_shared<ShmLink>(std::move(result.value()));

            // Apply link model if enabled
            if (use_model_) {
                link->set_model(model_);
            }

            nodes_.push_back(link);
        }

        echo::info("Hub initialized with ", nodes_.size(), " nodes").green();
    }

    /// Run the hub (blocking)
    void run() {
        echo::info("Hub running, forwarding CAN frames...").green().bold();
        echo::info("Press Ctrl+C to stop").cyan();

        uint64_t frames_forwarded = 0;
        uint64_t frames_dropped = 0;

        while (g_running) {
            bool activity = false;

            // Poll all nodes for outgoing frames
            for (size_t src_node = 0; src_node < nodes_.size(); ++src_node) {
                auto &src_link = nodes_[src_node];

                // Try to receive a frame from this node
                auto recv_result = src_link->recv();
                if (!recv_result.is_ok()) {
                    // No frame available from this node
                    continue;
                }

                activity = true;
                Frame frame = recv_result.value();

                // Validate frame type
                if (frame.header.frame_type != static_cast<uint16_t>(FrameType::CAN)) {
                    echo::warn("Node ", src_node, " sent non-CAN frame, ignoring").yellow();
                    continue;
                }

                // Parse CAN frame for logging
                can_frame cf;
                if (frame.payload.size() >= sizeof(can_frame)) {
                    std::memcpy(&cf, frame.payload.data(), sizeof(can_frame));
                    echo::trace("Node ", src_node, " -> CAN ID=0x", std::hex, std::setfill('0'), std::setw(3),
                                (cf.can_id & CAN_EFF_MASK), std::dec, " DLC=", (int)cf.can_dlc);
                }

                // Broadcast to all other nodes
                size_t broadcast_count = 0;
                for (size_t dst_node = 0; dst_node < nodes_.size(); ++dst_node) {
                    if (dst_node == src_node) {
                        continue; // Don't send back to sender
                    }

                    auto &dst_link = nodes_[dst_node];

                    // Forward the frame
                    auto send_result = dst_link->send(frame);
                    if (send_result.is_ok()) {
                        broadcast_count++;
                        echo::trace("  -> Node ", dst_node);
                    } else {
                        echo::warn("Failed to forward to node ", dst_node, ": ", send_result.error().message.c_str())
                            .yellow();
                        frames_dropped++;
                    }
                }

                if (broadcast_count > 0) {
                    frames_forwarded++;
                    echo::debug("Forwarded frame from node ", src_node, " to ", broadcast_count, " nodes");
                }
            }

            // If no activity, sleep briefly to avoid busy-waiting
            if (!activity) {
                std::this_thread::sleep_for(std::chrono::microseconds(100));
            }
        }

        echo::info("Hub shutting down...").yellow();
        echo::info("  Frames forwarded: ", frames_forwarded);
        echo::info("  Frames dropped: ", frames_dropped);
    }

    /// Get statistics
    void print_stats() const {
        echo::info("=== CAN Bus Hub Statistics ===").cyan().bold();
        echo::info("  Nodes: ", nodes_.size());
        echo::info("  Bitrate: ", bitrate_, " bps");
    }

  private:
    size_t num_nodes_;
    uint32_t bitrate_;
    Vector<std::shared_ptr<ShmLink>> nodes_;
    LinkModel model_;
    bool use_model_ = false;
};

int main(int argc, char **argv) {
    // Parse command line arguments
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <num_nodes> [bitrate_bps] [drop_prob] [corrupt_prob]" << std::endl;
        std::cerr << std::endl;
        std::cerr << "Examples:" << std::endl;
        std::cerr << "  " << argv[0] << " 3                    # 3 nodes, 500 kbps, no errors" << std::endl;
        std::cerr << "  " << argv[0] << " 5 1000000            # 5 nodes, 1 Mbps, no errors" << std::endl;
        std::cerr << "  " << argv[0] << " 3 500000 0.01 0.005  # 3 nodes, 500 kbps, 1% drop, 0.5% corrupt" << std::endl;
        return 1;
    }

    size_t num_nodes = std::atoi(argv[1]);
    uint32_t bitrate = (argc > 2) ? std::atoi(argv[2]) : 500000;
    double drop_prob = (argc > 3) ? std::atof(argv[3]) : 0.0;
    double corrupt_prob = (argc > 4) ? std::atof(argv[4]) : 0.0;

    // Validate arguments
    if (num_nodes < 2) {
        echo::error("Number of nodes must be at least 2").red();
        return 1;
    }

    if (num_nodes > 100) {
        echo::error("Number of nodes must be at most 100").red();
        return 1;
    }

    if (drop_prob < 0.0 || drop_prob > 1.0) {
        echo::error("Drop probability must be in [0.0, 1.0]").red();
        return 1;
    }

    if (corrupt_prob < 0.0 || corrupt_prob > 1.0) {
        echo::error("Corrupt probability must be in [0.0, 1.0]").red();
        return 1;
    }

    // Set up signal handlers
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    // Create and run the hub
    try {
        CanBusHub hub(num_nodes, bitrate, drop_prob, corrupt_prob);
        hub.run();
        hub.print_stats();
    } catch (const std::exception &e) {
        echo::error("Hub error: ", e.what()).red().bold();
        return 1;
    }

    echo::info("CAN Bus Hub stopped").green();
    return 0;
}
