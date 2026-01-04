#pragma once

#include <cmath>
#include <random>
#include <wirebit/common/time.hpp>
#include <wirebit/common/types.hpp>

namespace wirebit {

    /// Link model parameters for simulating realistic communication behavior
    struct LinkModel {
        double latency_mean_ns = 0.0;   ///< Mean latency in nanoseconds
        double latency_stddev_ns = 0.0; ///< Latency standard deviation in nanoseconds
        double jitter_ns = 0.0;         ///< Jitter in nanoseconds
        double loss_rate = 0.0;         ///< Packet loss rate (0.0 to 1.0)
        double corruption_rate = 0.0;   ///< Data corruption rate (0.0 to 1.0)
        uint32_t bandwidth_bps = 0;     ///< Bandwidth in bits per second (0 = unlimited)

        LinkModel() = default;

        LinkModel(double latency_mean, double latency_stddev = 0.0, double jitter = 0.0, double loss = 0.0,
                  double corruption = 0.0, uint32_t bandwidth = 0)
            : latency_mean_ns(latency_mean), latency_stddev_ns(latency_stddev), jitter_ns(jitter), loss_rate(loss),
              corruption_rate(corruption), bandwidth_bps(bandwidth) {}

        /// Check if model is deterministic (no randomness)
        inline bool is_deterministic() const {
            return latency_stddev_ns == 0.0 && jitter_ns == 0.0 && loss_rate == 0.0 && corruption_rate == 0.0;
        }

        /// Check if model has bandwidth limit
        inline bool has_bandwidth_limit() const { return bandwidth_bps > 0; }
    };

    /// Compute transmission delay based on bandwidth and data size
    /// @param model Link model parameters
    /// @param data_size Size of data in bytes
    /// @return Transmission delay in nanoseconds
    inline TimeNs compute_transmission_delay(const LinkModel &model, size_t data_size) {
        if (!model.has_bandwidth_limit()) {
            return 0;
        }
        // delay = (data_size * 8 bits/byte) / (bandwidth bits/sec) * 1e9 ns/sec
        double delay_s = (static_cast<double>(data_size) * 8.0) / static_cast<double>(model.bandwidth_bps);
        return static_cast<TimeNs>(delay_s * 1e9);
    }

    /// Compute total latency with jitter and randomness
    /// @param model Link model parameters
    /// @param rng Random number generator
    /// @return Total latency in nanoseconds
    template <typename RNG> inline TimeNs compute_latency(const LinkModel &model, RNG &rng) {
        double latency = model.latency_mean_ns;

        // Add gaussian noise if stddev > 0
        if (model.latency_stddev_ns > 0.0) {
            std::normal_distribution<double> dist(0.0, model.latency_stddev_ns);
            latency += dist(rng);
        }

        // Add jitter if > 0
        if (model.jitter_ns > 0.0) {
            std::uniform_real_distribution<double> jitter_dist(-model.jitter_ns, model.jitter_ns);
            latency += jitter_dist(rng);
        }

        // Ensure non-negative
        return static_cast<TimeNs>(std::max(0.0, latency));
    }

    /// Check if frame should be dropped based on loss rate
    /// @param model Link model parameters
    /// @param rng Random number generator
    /// @return true if frame should be dropped
    template <typename RNG> inline bool should_drop_frame(const LinkModel &model, RNG &rng) {
        if (model.loss_rate <= 0.0) {
            return false;
        }
        std::uniform_real_distribution<double> dist(0.0, 1.0);
        return dist(rng) < model.loss_rate;
    }

    /// Check if frame should be corrupted based on corruption rate
    /// @param model Link model parameters
    /// @param rng Random number generator
    /// @return true if frame should be corrupted
    template <typename RNG> inline bool should_corrupt_frame(const LinkModel &model, RNG &rng) {
        if (model.corruption_rate <= 0.0) {
            return false;
        }
        std::uniform_real_distribution<double> dist(0.0, 1.0);
        return dist(rng) < model.corruption_rate;
    }

    /// Corrupt frame data by flipping random bits
    /// @param data Data to corrupt
    /// @param rng Random number generator
    template <typename RNG> inline void corrupt_data(Bytes &data, RNG &rng) {
        if (data.empty()) {
            return;
        }
        // Flip 1-3 random bits
        std::uniform_int_distribution<size_t> byte_dist(0, data.size() - 1);
        std::uniform_int_distribution<int> bit_dist(0, 7);
        std::uniform_int_distribution<int> count_dist(1, 3);

        int num_flips = count_dist(rng);
        for (int i = 0; i < num_flips; ++i) {
            size_t byte_idx = byte_dist(rng);
            int bit_idx = bit_dist(rng);
            data[byte_idx] ^= (1 << bit_idx);
        }
    }

} // namespace wirebit
