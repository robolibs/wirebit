#pragma once

#include <algorithm>
#include <cmath>
#include <echo/echo.hpp>
#include <wirebit/common/time.hpp>
#include <wirebit/common/types.hpp>

namespace wirebit {

    /// Deterministic PRNG using Linear Congruential Generator
    /// Uses constants from Knuth's MMIX (same as used in glibc)
    class DeterministicRNG {
        uint64_t state_;

      public:
        /// Construct with seed
        inline explicit DeterministicRNG(uint64_t seed = 0) : state_(seed) {
            if (seed != 0) {
                echo::trace("DeterministicRNG initialized with seed: ", seed);
            }
        }

        /// Get next random uint64_t
        inline uint64_t next() {
            state_ = state_ * 6364136223846793005ULL + 1442695040888963407ULL;
            return state_;
        }

        /// Get uniform random double in [0.0, 1.0)
        inline double uniform() { return (next() >> 11) * (1.0 / 9007199254740992.0); }

        /// Get random uint64_t in range [0, max)
        inline uint64_t range(uint64_t max) {
            if (max == 0)
                return 0;
            return next() % max;
        }

        /// Reset to specific seed
        inline void seed(uint64_t new_seed) {
            state_ = new_seed;
            echo::trace("DeterministicRNG reseeded: ", new_seed);
        }

        /// Get current state
        inline uint64_t state() const { return state_; }
    };

    /// Link model parameters for simulating realistic communication behavior
    struct LinkModel {
        uint64_t base_latency_ns = 0; ///< Base latency in nanoseconds
        uint64_t jitter_ns = 0;       ///< Jitter range in nanoseconds (uniform random)
        double drop_prob = 0.0;       ///< Frame drop probability [0.0, 1.0]
        double dup_prob = 0.0;        ///< Frame duplication probability [0.0, 1.0]
        double corrupt_prob = 0.0;    ///< Frame corruption probability [0.0, 1.0]
        uint64_t bandwidth_bps = 0;   ///< Bandwidth in bits per second (0 = unlimited)
        uint64_t seed = 0;            ///< PRNG seed for deterministic simulation

        LinkModel() = default;

        LinkModel(uint64_t latency, uint64_t jitter = 0, double drop = 0.0, double dup = 0.0, double corrupt = 0.0,
                  uint64_t bandwidth = 0, uint64_t prng_seed = 0)
            : base_latency_ns(latency), jitter_ns(jitter), drop_prob(drop), dup_prob(dup), corrupt_prob(corrupt),
              bandwidth_bps(bandwidth), seed(prng_seed) {
            echo::info("LinkModel created: latency=", latency, "ns jitter=", jitter, "ns drop=", drop, " dup=", dup,
                       " corrupt=", corrupt, " bw=", bandwidth, "bps");
        }

        /// Check if model is deterministic (no randomness)
        inline bool is_deterministic() const {
            return jitter_ns == 0 && drop_prob == 0.0 && dup_prob == 0.0 && corrupt_prob == 0.0;
        }

        /// Check if model has bandwidth limit
        inline bool has_bandwidth_limit() const { return bandwidth_bps > 0; }

        /// Check if model can drop frames
        inline bool can_drop() const { return drop_prob > 0.0; }

        /// Check if model can duplicate frames
        inline bool can_duplicate() const { return dup_prob > 0.0; }

        /// Check if model can corrupt frames
        inline bool can_corrupt() const { return corrupt_prob > 0.0; }
    };

    /// Frame action result from link model simulation
    enum class FrameAction {
        DELIVER,   ///< Deliver frame normally
        DROP,      ///< Drop frame (simulate packet loss)
        DUPLICATE, ///< Duplicate frame
        CORRUPT,   ///< Corrupt frame data
    };

    /// Compute delivery timestamp for a frame
    /// @param model Link model parameters
    /// @param now_ns Current time in nanoseconds
    /// @param payload_len Payload length in bytes
    /// @param next_send_time_ns Next available send time (updated by this function)
    /// @param rng Deterministic RNG
    /// @return Delivery timestamp in nanoseconds
    inline uint64_t compute_deliver_at_ns(const LinkModel &model, uint64_t now_ns, uint32_t payload_len,
                                          uint64_t &next_send_time_ns, DeterministicRNG &rng) {
        echo::trace("Computing delivery time: now=", now_ns, " payload=", payload_len, "B");

        // Compute latency with jitter
        uint64_t latency = model.base_latency_ns;
        if (model.jitter_ns > 0) {
            uint64_t jitter = rng.range(model.jitter_ns);
            latency += jitter;
            echo::trace("Added jitter: ", jitter, "ns (total latency: ", latency, "ns)");
        }

        // Compute transmission time based on bandwidth
        uint64_t transmit_time_ns = 0;
        if (model.bandwidth_bps > 0) {
            // transmit_time = (payload_len * 8 bits/byte) / (bandwidth bits/sec) * 1e9 ns/sec
            transmit_time_ns = (static_cast<uint64_t>(payload_len) * 8ULL * 1000000000ULL) / model.bandwidth_bps;
            echo::trace("Transmission time: ", transmit_time_ns, "ns (bandwidth: ", model.bandwidth_bps, "bps)");
        }

        // Enforce bandwidth limit: can't send before previous transmission finishes
        uint64_t send_time = std::max(now_ns, next_send_time_ns);
        next_send_time_ns = send_time + transmit_time_ns;

        uint64_t deliver_at = send_time + latency;
        echo::debug("Delivery scheduled at: ", deliver_at, "ns (send: ", send_time, "ns + latency: ", latency, "ns)");

        return deliver_at;
    }

    /// Determine frame action based on link model probabilities
    /// @param model Link model parameters
    /// @param rng Deterministic RNG
    /// @return Frame action to take
    inline FrameAction determine_frame_action(const LinkModel &model, DeterministicRNG &rng) {
        // Check drop first (highest priority)
        if (model.drop_prob > 0.0) {
            double roll = rng.uniform();
            if (roll < model.drop_prob) {
                echo::warn("Frame DROPPED by LinkModel (roll=", roll, " < drop_prob=", model.drop_prob, ")").yellow();
                return FrameAction::DROP;
            }
        }

        // Check duplicate
        if (model.dup_prob > 0.0) {
            double roll = rng.uniform();
            if (roll < model.dup_prob) {
                echo::warn("Frame DUPLICATED by LinkModel (roll=", roll, " < dup_prob=", model.dup_prob, ")").yellow();
                return FrameAction::DUPLICATE;
            }
        }

        // Check corrupt
        if (model.corrupt_prob > 0.0) {
            double roll = rng.uniform();
            if (roll < model.corrupt_prob) {
                echo::warn("Frame CORRUPTED by LinkModel (roll=", roll, " < corrupt_prob=", model.corrupt_prob, ")")
                    .yellow();
                return FrameAction::CORRUPT;
            }
        }

        return FrameAction::DELIVER;
    }

    /// Corrupt frame payload by flipping random bits
    /// @param payload Frame payload to corrupt
    /// @param rng Deterministic RNG
    inline void corrupt_payload(Bytes &payload, DeterministicRNG &rng) {
        if (payload.empty()) {
            echo::trace("Cannot corrupt empty payload");
            return;
        }

        // Flip 1-3 random bits
        uint64_t num_flips = 1 + rng.range(3);
        echo::trace("Corrupting payload: flipping ", num_flips, " bits");

        for (uint64_t i = 0; i < num_flips; ++i) {
            size_t byte_idx = rng.range(payload.size());
            uint8_t bit_idx = rng.range(8);
            uint8_t old_val = payload[byte_idx];
            payload[byte_idx] ^= (1 << bit_idx);
            echo::trace("Flipped bit ", static_cast<int>(bit_idx), " in byte ", byte_idx, ": 0x", std::hex,
                        static_cast<int>(old_val), " -> 0x", static_cast<int>(payload[byte_idx]));
        }
    }

    /// Compute transmission delay based on bandwidth and data size
    /// @param model Link model parameters
    /// @param data_size Size of data in bytes
    /// @return Transmission delay in nanoseconds
    inline TimeNs compute_transmission_delay(const LinkModel &model, size_t data_size) {
        if (!model.has_bandwidth_limit()) {
            return 0;
        }
        // delay = (data_size * 8 bits/byte) / (bandwidth bits/sec) * 1e9 ns/sec
        return (static_cast<uint64_t>(data_size) * 8ULL * 1000000000ULL) / model.bandwidth_bps;
    }

} // namespace wirebit
