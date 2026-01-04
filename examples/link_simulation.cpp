/**
 * @file link_simulation.cpp
 * @brief Demonstrates link simulation with LinkModel
 */

#include <chrono>
#include <echo/echo.hpp>
#include <thread>
#include <wirebit/wirebit.hpp>

void demo_perfect_link() {
    echo::info("=== Perfect Link (No Impairments) ===").bold().cyan();

    // Create a perfect link model
    wirebit::LinkModel perfect(0,   // No latency
                               0,   // No jitter
                               0.0, // No drops
                               0.0, // No duplicates
                               0.0, // No corruption
                               0,   // Unlimited bandwidth
                               42);

    echo::info("Link properties:");
    echo::info("  Deterministic: ", (perfect.is_deterministic() ? "YES" : "NO"));
    echo::info("  Bandwidth limited: ", (perfect.has_bandwidth_limit() ? "YES" : "NO"));

    // Simulate sending frames
    wirebit::DeterministicRNG rng(42);
    uint64_t next_send_time = 0;

    int frames_sent = 0;
    int frames_delivered = 0;

    for (int i = 0; i < 10; ++i) {
        wirebit::Bytes payload = {static_cast<wirebit::Byte>(i)};
        wirebit::Frame frame = wirebit::make_frame(wirebit::FrameType::SERIAL, payload);

        // Determine action
        auto action = wirebit::determine_frame_action(perfect, rng);
        frames_sent++;

        if (action == wirebit::FrameAction::DELIVER) {
            frames_delivered++;
        }
    }

    echo::info("Results:");
    echo::info("  Frames sent: ", frames_sent);
    echo::info("  Frames delivered: ", frames_delivered);
    echo::info("  Delivery rate: ", (100.0 * frames_delivered / frames_sent), "%").green();
    echo::info("");
}

void demo_lossy_link() {
    echo::info("=== Lossy Link (10% Packet Loss) ===").bold().cyan();

    wirebit::LinkModel lossy(1000000, // 1ms latency
                             0,       // No jitter
                             0.1,     // 10% drop rate
                             0.0,     // No duplicates
                             0.0,     // No corruption
                             0,       // Unlimited bandwidth
                             42);

    echo::info("Link properties:");
    echo::info("  Drop probability: ", lossy.drop_prob * 100, "%");

    wirebit::DeterministicRNG rng(42);

    int frames_sent = 0;
    int frames_dropped = 0;
    int frames_delivered = 0;

    for (int i = 0; i < 1000; ++i) {
        auto action = wirebit::determine_frame_action(lossy, rng);
        frames_sent++;

        if (action == wirebit::FrameAction::DROP) {
            frames_dropped++;
        } else if (action == wirebit::FrameAction::DELIVER) {
            frames_delivered++;
        }
    }

    echo::info("Results (1000 frames):");
    echo::info("  Frames sent: ", frames_sent);
    echo::info("  Frames dropped: ", frames_dropped);
    echo::info("  Frames delivered: ", frames_delivered);
    echo::info("  Drop rate: ", (100.0 * frames_dropped / frames_sent), "%");
    echo::info("  Delivery rate: ", (100.0 * frames_delivered / frames_sent), "%").green();
    echo::info("");
}

void demo_bandwidth_limited_link() {
    echo::info("=== Bandwidth Limited Link (1 Mbps) ===").bold().cyan();

    wirebit::LinkModel limited(0,       // No latency
                               0,       // No jitter
                               0.0,     // No drops
                               0.0,     // No duplicates
                               0.0,     // No corruption
                               1000000, // 1 Mbps
                               42);

    echo::info("Link properties:");
    echo::info("  Bandwidth: ", limited.bandwidth_bps, " bps (", limited.bandwidth_bps / 1000000, " Mbps)");

    wirebit::DeterministicRNG rng(42);
    uint64_t next_send_time = 0;
    uint64_t now = wirebit::now_ns();

    // Send 10 frames of 1000 bytes each
    echo::info("Sending 10 frames of 1000 bytes each:");

    for (int i = 0; i < 10; ++i) {
        uint32_t payload_len = 1000;

        uint64_t deliver_at = wirebit::compute_deliver_at_ns(limited, now, payload_len, next_send_time, rng);

        uint64_t transmit_time = next_send_time - now;

        echo::info("  Frame ", i + 1, ": transmit_time=", wirebit::ns_to_ms(transmit_time), " ms");
    }

    uint64_t total_time = next_send_time - now;
    echo::info("Total transmission time: ", wirebit::ns_to_ms(total_time), " ms");

    // Calculate theoretical time
    uint64_t total_bits = 10 * 1000 * 8;
    uint64_t theoretical_time_ns = (total_bits * 1000000000ULL) / limited.bandwidth_bps;
    echo::info("Theoretical time: ", wirebit::ns_to_ms(theoretical_time_ns), " ms").green();
    echo::info("");
}

void demo_jittery_link() {
    echo::info("=== Jittery Link (10ms ± 5ms) ===").bold().cyan();

    wirebit::LinkModel jittery(10000000, // 10ms base latency
                               5000000,  // 5ms jitter
                               0.0,      // No drops
                               0.0,      // No duplicates
                               0.0,      // No corruption
                               0,        // Unlimited bandwidth
                               42);

    echo::info("Link properties:");
    echo::info("  Base latency: ", wirebit::ns_to_ms(jittery.base_latency_ns), " ms");
    echo::info("  Jitter: ±", wirebit::ns_to_ms(jittery.jitter_ns), " ms");

    wirebit::DeterministicRNG rng(42);
    uint64_t next_send_time = 0;
    uint64_t now = wirebit::now_ns();

    // Measure latency distribution
    uint64_t min_latency = UINT64_MAX;
    uint64_t max_latency = 0;
    uint64_t total_latency = 0;
    int count = 100;

    for (int i = 0; i < count; ++i) {
        uint64_t deliver_at = wirebit::compute_deliver_at_ns(jittery, now, 100, next_send_time, rng);

        uint64_t latency = deliver_at - now;
        min_latency = std::min(min_latency, latency);
        max_latency = std::max(max_latency, latency);
        total_latency += latency;
    }

    uint64_t avg_latency = total_latency / count;

    echo::info("Latency statistics (100 frames):");
    echo::info("  Min: ", wirebit::ns_to_ms(min_latency), " ms");
    echo::info("  Max: ", wirebit::ns_to_ms(max_latency), " ms");
    echo::info("  Avg: ", wirebit::ns_to_ms(avg_latency), " ms").green();
    echo::info("");
}

void demo_realistic_wan() {
    echo::info("=== Realistic WAN Link ===").bold().cyan();

    wirebit::LinkModel wan(50000000,  // 50ms base latency
                           10000000,  // 10ms jitter
                           0.01,      // 1% packet loss
                           0.001,     // 0.1% duplication
                           0.0001,    // 0.01% corruption
                           100000000, // 100 Mbps
                           42);

    echo::info("Link properties:");
    echo::info("  Base latency: ", wirebit::ns_to_ms(wan.base_latency_ns), " ms");
    echo::info("  Jitter: ±", wirebit::ns_to_ms(wan.jitter_ns), " ms");
    echo::info("  Drop rate: ", wan.drop_prob * 100, "%");
    echo::info("  Duplicate rate: ", wan.dup_prob * 100, "%");
    echo::info("  Corrupt rate: ", wan.corrupt_prob * 100, "%");
    echo::info("  Bandwidth: ", wan.bandwidth_bps / 1000000, " Mbps");

    wirebit::DeterministicRNG rng(42);

    int frames_sent = 0;
    int frames_dropped = 0;
    int frames_duplicated = 0;
    int frames_corrupted = 0;
    int frames_delivered = 0;

    for (int i = 0; i < 10000; ++i) {
        auto action = wirebit::determine_frame_action(wan, rng);
        frames_sent++;

        switch (action) {
        case wirebit::FrameAction::DROP:
            frames_dropped++;
            break;
        case wirebit::FrameAction::DUPLICATE:
            frames_duplicated++;
            frames_delivered++; // Original still delivered
            break;
        case wirebit::FrameAction::CORRUPT:
            frames_corrupted++;
            frames_delivered++; // Delivered but corrupted
            break;
        case wirebit::FrameAction::DELIVER:
            frames_delivered++;
            break;
        }
    }

    echo::info("Results (10,000 frames):");
    echo::info("  Frames sent: ", frames_sent);
    echo::info("  Dropped: ", frames_dropped, " (", (100.0 * frames_dropped / frames_sent), "%)");
    echo::info("  Duplicated: ", frames_duplicated, " (", (100.0 * frames_duplicated / frames_sent), "%)");
    echo::info("  Corrupted: ", frames_corrupted, " (", (100.0 * frames_corrupted / frames_sent), "%)");
    echo::info("  Delivered: ", frames_delivered, " (", (100.0 * frames_delivered / frames_sent), "%)").green();
    echo::info("");
}

void demo_corruption() {
    echo::info("=== Data Corruption Demo ===").bold().cyan();

    wirebit::Bytes original = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77};

    echo::info("Original data:");
    echo::info("  ");
    for (auto byte : original) {
        echo::info("0x", std::hex, static_cast<int>(byte), " ");
    }
    echo::info("");

    // Corrupt the data
    wirebit::Bytes corrupted = original;
    wirebit::DeterministicRNG rng(42);
    wirebit::corrupt_payload(corrupted, rng);

    echo::info("Corrupted data:");
    echo::info("  ");
    for (auto byte : corrupted) {
        echo::info("0x", std::hex, static_cast<int>(byte), " ");
    }
    echo::info("");

    // Count differences
    int bit_flips = 0;
    for (size_t i = 0; i < original.size(); ++i) {
        uint8_t diff = original[i] ^ corrupted[i];
        while (diff) {
            if (diff & 1)
                bit_flips++;
            diff >>= 1;
        }
    }

    echo::info("Bit flips: ", bit_flips).yellow();
    echo::info("");
}

int main() {
    echo::info("╔════════════════════════════════════════╗").bold().cyan();
    echo::info("║   Wirebit Link Simulation Demo        ║").bold().cyan();
    echo::info("╚════════════════════════════════════════╝").bold().cyan();
    echo::info("");

    demo_perfect_link();
    demo_lossy_link();
    demo_bandwidth_limited_link();
    demo_jittery_link();
    demo_realistic_wan();
    demo_corruption();

    echo::info("╔════════════════════════════════════════╗").bold().green();
    echo::info("║   All simulations completed!           ║").bold().green();
    echo::info("╚════════════════════════════════════════╝").bold().green();

    return 0;
}
