#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include <wirebit/wirebit.hpp>

TEST_CASE("DeterministicRNG") {
    SUBCASE("Same seed produces same sequence") {
        wirebit::DeterministicRNG rng1(12345);
        wirebit::DeterministicRNG rng2(12345);

        for (int i = 0; i < 100; ++i) {
            CHECK(rng1.next() == rng2.next());
        }
    }

    SUBCASE("Different seeds produce different sequences") {
        wirebit::DeterministicRNG rng1(12345);
        wirebit::DeterministicRNG rng2(54321);

        bool all_same = true;
        for (int i = 0; i < 100; ++i) {
            if (rng1.next() != rng2.next()) {
                all_same = false;
                break;
            }
        }
        CHECK_FALSE(all_same);
    }

    SUBCASE("Uniform distribution in [0, 1)") {
        wirebit::DeterministicRNG rng(42);

        for (int i = 0; i < 1000; ++i) {
            double val = rng.uniform();
            CHECK(val >= 0.0);
            CHECK(val < 1.0);
        }
    }

    SUBCASE("Range function") {
        wirebit::DeterministicRNG rng(999);

        // Test range(100)
        for (int i = 0; i < 1000; ++i) {
            uint64_t val = rng.range(100);
            CHECK(val < 100);
        }

        // Test range(0) edge case
        CHECK(rng.range(0) == 0);
    }

    SUBCASE("Reseed changes sequence") {
        wirebit::DeterministicRNG rng(100);
        uint64_t val1 = rng.next();

        rng.seed(100); // Reset to same seed
        uint64_t val2 = rng.next();

        CHECK(val1 == val2);
    }

    SUBCASE("State tracking") {
        wirebit::DeterministicRNG rng(777);
        uint64_t initial_state = rng.state();

        rng.next();
        uint64_t new_state = rng.state();

        CHECK(new_state != initial_state);
    }
}

TEST_CASE("LinkModel basic properties") {
    SUBCASE("Default model is deterministic") {
        wirebit::LinkModel model;
        CHECK(model.is_deterministic());
        CHECK_FALSE(model.has_bandwidth_limit());
        CHECK_FALSE(model.can_drop());
        CHECK_FALSE(model.can_duplicate());
        CHECK_FALSE(model.can_corrupt());
    }

    SUBCASE("Model with latency only") {
        wirebit::LinkModel model(1000000, 0, 0.0, 0.0, 0.0, 0, 42);

        uint64_t base_latency = model.base_latency_ns;
        CHECK(base_latency == 1000000);
        CHECK(model.is_deterministic());
    }

    SUBCASE("Model with jitter is non-deterministic") {
        wirebit::LinkModel model(1000000, 500000, 0.0, 0.0, 0.0, 0, 42);
        CHECK_FALSE(model.is_deterministic());

        uint64_t jitter = model.jitter_ns;
        CHECK(jitter == 500000);
    }

    SUBCASE("Model with packet loss") {
        wirebit::LinkModel model(0, 0, 0.1, 0.0, 0.0, 0, 42);
        CHECK(model.can_drop());
        CHECK_FALSE(model.is_deterministic());

        double drop_prob = model.drop_prob;
        CHECK(drop_prob == 0.1);
    }

    SUBCASE("Model with duplication") {
        wirebit::LinkModel model(0, 0, 0.0, 0.05, 0.0, 0, 42);
        CHECK(model.can_duplicate());

        double dup_prob = model.dup_prob;
        CHECK(dup_prob == 0.05);
    }

    SUBCASE("Model with corruption") {
        wirebit::LinkModel model(0, 0, 0.0, 0.0, 0.02, 0, 42);
        CHECK(model.can_corrupt());

        double corrupt_prob = model.corrupt_prob;
        CHECK(corrupt_prob == 0.02);
    }

    SUBCASE("Model with bandwidth limit") {
        wirebit::LinkModel model(0, 0, 0.0, 0.0, 0.0, 1000000, 42);
        CHECK(model.has_bandwidth_limit());

        uint64_t bw = model.bandwidth_bps;
        CHECK(bw == 1000000);
    }
}

TEST_CASE("compute_deliver_at_ns") {
    SUBCASE("Zero latency, no bandwidth limit") {
        wirebit::LinkModel model(0, 0, 0.0, 0.0, 0.0, 0, 42);
        wirebit::DeterministicRNG rng(42);
        uint64_t next_send_time = 0;

        uint64_t now = 1000000000;
        uint64_t deliver_at = wirebit::compute_deliver_at_ns(model, now, 100, next_send_time, rng);

        CHECK(deliver_at == now); // No latency, immediate delivery
    }

    SUBCASE("Fixed latency") {
        wirebit::LinkModel model(5000000, 0, 0.0, 0.0, 0.0, 0, 42);
        wirebit::DeterministicRNG rng(42);
        uint64_t next_send_time = 0;

        uint64_t now = 1000000000;
        uint64_t deliver_at = wirebit::compute_deliver_at_ns(model, now, 100, next_send_time, rng);

        uint64_t expected = now + 5000000;
        CHECK(deliver_at == expected);
    }

    SUBCASE("Latency with jitter") {
        wirebit::LinkModel model(5000000, 1000000, 0.0, 0.0, 0.0, 0, 42);
        wirebit::DeterministicRNG rng(42);
        uint64_t next_send_time = 0;

        uint64_t now = 1000000000;
        uint64_t deliver_at = wirebit::compute_deliver_at_ns(model, now, 100, next_send_time, rng);

        // Should be base_latency + some jitter
        uint64_t min_latency = model.base_latency_ns;
        uint64_t max_latency = model.base_latency_ns + model.jitter_ns;

        CHECK(deliver_at >= now + min_latency);
        CHECK(deliver_at <= now + max_latency);
    }

    SUBCASE("Bandwidth limiting") {
        // 1 Mbps = 1,000,000 bits/sec
        wirebit::LinkModel model(0, 0, 0.0, 0.0, 0.0, 1000000, 42);
        wirebit::DeterministicRNG rng(42);
        uint64_t next_send_time = 0;

        uint64_t now = 1000000000;
        uint32_t payload_len = 1000; // 1000 bytes = 8000 bits

        uint64_t deliver_at = wirebit::compute_deliver_at_ns(model, now, payload_len, next_send_time, rng);

        // Transmission time = 8000 bits / 1,000,000 bps = 0.008 sec = 8,000,000 ns
        uint64_t expected_transmit_time = 8000000;

        CHECK(deliver_at == now);
        CHECK(next_send_time == now + expected_transmit_time);
    }

    SUBCASE("Sequential frames respect bandwidth") {
        wirebit::LinkModel model(0, 0, 0.0, 0.0, 0.0, 1000000, 42);
        wirebit::DeterministicRNG rng(42);
        uint64_t next_send_time = 0;

        uint64_t now = 1000000000;
        uint32_t payload_len = 500; // 500 bytes = 4000 bits

        // First frame
        uint64_t deliver1 = wirebit::compute_deliver_at_ns(model, now, payload_len, next_send_time, rng);
        CHECK(deliver1 == now);

        uint64_t first_next_send = next_send_time;

        // Second frame immediately after
        uint64_t deliver2 = wirebit::compute_deliver_at_ns(model, now, payload_len, next_send_time, rng);

        // Second frame must wait for first to finish transmitting
        CHECK(deliver2 == first_next_send);
    }

    SUBCASE("Deterministic with same seed") {
        wirebit::LinkModel model(1000000, 500000, 0.0, 0.0, 0.0, 0, 42);

        wirebit::DeterministicRNG rng1(100);
        wirebit::DeterministicRNG rng2(100);
        uint64_t next_send1 = 0;
        uint64_t next_send2 = 0;

        uint64_t now = 1000000000;

        uint64_t deliver1 = wirebit::compute_deliver_at_ns(model, now, 100, next_send1, rng1);
        uint64_t deliver2 = wirebit::compute_deliver_at_ns(model, now, 100, next_send2, rng2);

        CHECK(deliver1 == deliver2);
    }
}

TEST_CASE("determine_frame_action") {
    SUBCASE("No impairments - always deliver") {
        wirebit::LinkModel model(0, 0, 0.0, 0.0, 0.0, 0, 42);
        wirebit::DeterministicRNG rng(42);

        for (int i = 0; i < 100; ++i) {
            auto action = wirebit::determine_frame_action(model, rng);
            CHECK(action == wirebit::FrameAction::DELIVER);
        }
    }

    SUBCASE("100% drop rate") {
        wirebit::LinkModel model(0, 0, 1.0, 0.0, 0.0, 0, 42);
        wirebit::DeterministicRNG rng(42);

        for (int i = 0; i < 100; ++i) {
            auto action = wirebit::determine_frame_action(model, rng);
            CHECK(action == wirebit::FrameAction::DROP);
        }
    }

    SUBCASE("Drop probability distribution") {
        wirebit::LinkModel model(0, 0, 0.3, 0.0, 0.0, 0, 42);
        wirebit::DeterministicRNG rng(42);

        int drops = 0;
        int delivers = 0;
        int trials = 10000;

        for (int i = 0; i < trials; ++i) {
            auto action = wirebit::determine_frame_action(model, rng);
            if (action == wirebit::FrameAction::DROP) {
                drops++;
            } else {
                delivers++;
            }
        }

        // Should be approximately 30% drops
        double drop_rate = static_cast<double>(drops) / trials;
        CHECK(drop_rate > 0.25);
        CHECK(drop_rate < 0.35);
    }

    SUBCASE("Duplication probability") {
        wirebit::LinkModel model(0, 0, 0.0, 0.2, 0.0, 0, 42);
        wirebit::DeterministicRNG rng(42);

        int duplicates = 0;
        int trials = 10000;

        for (int i = 0; i < trials; ++i) {
            auto action = wirebit::determine_frame_action(model, rng);
            if (action == wirebit::FrameAction::DUPLICATE) {
                duplicates++;
            }
        }

        // Should be approximately 20% duplicates
        double dup_rate = static_cast<double>(duplicates) / trials;
        CHECK(dup_rate > 0.15);
        CHECK(dup_rate < 0.25);
    }

    SUBCASE("Corruption probability") {
        wirebit::LinkModel model(0, 0, 0.0, 0.0, 0.1, 0, 42);
        wirebit::DeterministicRNG rng(42);

        int corruptions = 0;
        int trials = 10000;

        for (int i = 0; i < trials; ++i) {
            auto action = wirebit::determine_frame_action(model, rng);
            if (action == wirebit::FrameAction::CORRUPT) {
                corruptions++;
            }
        }

        // Should be approximately 10% corruptions
        double corrupt_rate = static_cast<double>(corruptions) / trials;
        CHECK(corrupt_rate > 0.08);
        CHECK(corrupt_rate < 0.12);
    }

    SUBCASE("Deterministic with same seed") {
        wirebit::LinkModel model(0, 0, 0.3, 0.2, 0.1, 0, 42);

        wirebit::DeterministicRNG rng1(999);
        wirebit::DeterministicRNG rng2(999);

        for (int i = 0; i < 100; ++i) {
            auto action1 = wirebit::determine_frame_action(model, rng1);
            auto action2 = wirebit::determine_frame_action(model, rng2);
            CHECK(action1 == action2);
        }
    }
}

TEST_CASE("corrupt_payload") {
    SUBCASE("Empty payload") {
        wirebit::Bytes payload;
        wirebit::DeterministicRNG rng(42);

        wirebit::corrupt_payload(payload, rng);
        CHECK(payload.empty());
    }

    SUBCASE("Corruption changes data") {
        wirebit::Bytes payload = {0x00, 0x00, 0x00, 0x00};
        wirebit::Bytes original = payload;
        wirebit::DeterministicRNG rng(42);

        wirebit::corrupt_payload(payload, rng);

        // At least one byte should be different
        bool changed = false;
        for (size_t i = 0; i < payload.size(); ++i) {
            if (payload[i] != original[i]) {
                changed = true;
                break;
            }
        }
        CHECK(changed);
    }

    SUBCASE("Corruption is deterministic") {
        wirebit::Bytes payload1 = {0xFF, 0xFF, 0xFF, 0xFF};
        wirebit::Bytes payload2 = {0xFF, 0xFF, 0xFF, 0xFF};

        wirebit::DeterministicRNG rng1(123);
        wirebit::DeterministicRNG rng2(123);

        wirebit::corrupt_payload(payload1, rng1);
        wirebit::corrupt_payload(payload2, rng2);

        CHECK(payload1.size() == payload2.size());
        for (size_t i = 0; i < payload1.size(); ++i) {
            CHECK(payload1[i] == payload2[i]);
        }
    }

    SUBCASE("Multiple corruptions") {
        wirebit::Bytes payload = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
        wirebit::DeterministicRNG rng(42);

        int changes = 0;
        for (int trial = 0; trial < 100; ++trial) {
            wirebit::Bytes test = payload;
            wirebit::corrupt_payload(test, rng);

            for (size_t i = 0; i < test.size(); ++i) {
                if (test[i] != payload[i]) {
                    changes++;
                }
            }
        }

        // Should have some corruptions
        CHECK(changes > 0);
    }
}

TEST_CASE("compute_transmission_delay") {
    SUBCASE("No bandwidth limit") {
        wirebit::LinkModel model(0, 0, 0.0, 0.0, 0.0, 0, 42);
        auto delay = wirebit::compute_transmission_delay(model, 1000);
        CHECK(delay == 0);
    }

    SUBCASE("1 Mbps bandwidth") {
        wirebit::LinkModel model(0, 0, 0.0, 0.0, 0.0, 1000000, 42);

        // 1000 bytes = 8000 bits
        // 8000 bits / 1,000,000 bps = 0.008 sec = 8,000,000 ns
        auto delay = wirebit::compute_transmission_delay(model, 1000);
        CHECK(delay == 8000000);
    }

    SUBCASE("10 Mbps bandwidth") {
        wirebit::LinkModel model(0, 0, 0.0, 0.0, 0.0, 10000000, 42);

        // 100 bytes = 800 bits
        // 800 bits / 10,000,000 bps = 0.00008 sec = 80,000 ns
        auto delay = wirebit::compute_transmission_delay(model, 100);
        CHECK(delay == 80000);
    }

    SUBCASE("1 Gbps bandwidth") {
        wirebit::LinkModel model(0, 0, 0.0, 0.0, 0.0, 1000000000, 42);

        // 1500 bytes (typical MTU) = 12000 bits
        // 12000 bits / 1,000,000,000 bps = 0.000012 sec = 12,000 ns
        auto delay = wirebit::compute_transmission_delay(model, 1500);
        CHECK(delay == 12000);
    }
}

TEST_CASE("LinkModel realistic scenarios") {
    SUBCASE("LAN scenario - low latency, high bandwidth") {
        wirebit::LinkModel lan(100000,     // 100 µs base latency
                               50000,      // 50 µs jitter
                               0.0001,     // 0.01% packet loss
                               0.0,        // No duplication
                               0.00001,    // 0.001% corruption
                               1000000000, // 1 Gbps
                               42);

        CHECK_FALSE(lan.is_deterministic());
        CHECK(lan.has_bandwidth_limit());

        uint64_t base_lat = lan.base_latency_ns;
        CHECK(base_lat == 100000);
    }

    SUBCASE("WAN scenario - higher latency, packet loss") {
        wirebit::LinkModel wan(50000000,  // 50 ms base latency
                               10000000,  // 10 ms jitter
                               0.01,      // 1% packet loss
                               0.001,     // 0.1% duplication
                               0.0001,    // 0.01% corruption
                               100000000, // 100 Mbps
                               42);

        CHECK(wan.can_drop());
        CHECK(wan.can_duplicate());
        CHECK(wan.can_corrupt());

        uint64_t base_lat = wan.base_latency_ns;
        CHECK(base_lat == 50000000);
    }

    SUBCASE("Satellite link - very high latency") {
        wirebit::LinkModel satellite(250000000, // 250 ms base latency (GEO satellite)
                                     20000000,  // 20 ms jitter
                                     0.05,      // 5% packet loss
                                     0.0,       // No duplication
                                     0.001,     // 0.1% corruption
                                     10000000,  // 10 Mbps
                                     42);

        uint64_t base_lat = satellite.base_latency_ns;
        CHECK(base_lat == 250000000);

        double drop = satellite.drop_prob;
        CHECK(drop == 0.05);
    }
}
