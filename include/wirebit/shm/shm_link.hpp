#pragma once

#include <cstdio>
#include <echo/echo.hpp>
#include <memory>
#include <wirebit/common/types.hpp>
#include <wirebit/link.hpp>
#include <wirebit/model.hpp>
#include <wirebit/shm/handshake.hpp>
#include <wirebit/shm/ring.hpp>

namespace wirebit {

    /// Statistics for ShmLink
    struct ShmLinkStats {
        uint64_t frames_sent = 0;
        uint64_t frames_received = 0;
        uint64_t frames_dropped = 0;
        uint64_t frames_duplicated = 0;
        uint64_t frames_corrupted = 0;
        uint64_t bytes_sent = 0;
        uint64_t bytes_received = 0;

        inline void reset() {
            frames_sent = 0;
            frames_received = 0;
            frames_dropped = 0;
            frames_duplicated = 0;
            frames_corrupted = 0;
            bytes_sent = 0;
            bytes_received = 0;
        }
    };

    /// Bidirectional shared memory link using two SPSC ring buffers
    /// Supports optional link simulation with LinkModel
    class ShmLink : public Link {
      public:
        /// Create a new shared memory link (server side)
        /// @param name Link name
        /// @param capacity_bytes Capacity of each ring buffer in bytes
        /// @param model Optional link model for simulation (nullptr = no simulation)
        /// @return Result containing ShmLink or error
        static Result<ShmLink, Error> create(const String &name, size_t capacity_bytes,
                                             const LinkModel *model = nullptr) {
            echo::info("Creating ShmLink: ", name, " (capacity: ", capacity_bytes, " bytes)");

            char buf[256];
            snprintf(buf, sizeof(buf), "/%s_tx", name.c_str());
            String tx_name(buf);
            snprintf(buf, sizeof(buf), "/%s_rx", name.c_str());
            String rx_name(buf);

            auto tx_result = FrameRing::create_shm(tx_name, capacity_bytes);
            if (!tx_result.is_ok()) {
                echo::error("Failed to create TX ring: ", tx_name).red();
                return Result<ShmLink, Error>::err(tx_result.error());
            }

            auto rx_result = FrameRing::create_shm(rx_name, capacity_bytes);
            if (!rx_result.is_ok()) {
                echo::error("Failed to create RX ring: ", rx_name).red();
                return Result<ShmLink, Error>::err(rx_result.error());
            }

            echo::debug("ShmLink created successfully: ", name).green();

            ShmLink link(name, std::move(tx_result.value()), std::move(rx_result.value()));

            // Set link model if provided
            if (model != nullptr) {
                link.set_model(*model);
            }

            return Result<ShmLink, Error>::ok(std::move(link));
        }

        /// Attach to an existing shared memory link (client side)
        /// @param name Link name
        /// @param model Optional link model for simulation (nullptr = no simulation)
        /// @return Result containing ShmLink or error
        static Result<ShmLink, Error> attach(const String &name, const LinkModel *model = nullptr) {
            echo::info("Attaching to ShmLink: ", name);

            // Note: TX/RX are swapped for client (client's TX is server's RX)
            char buf[256];
            snprintf(buf, sizeof(buf), "/%s_rx", name.c_str());
            String tx_name(buf);
            snprintf(buf, sizeof(buf), "/%s_tx", name.c_str());
            String rx_name(buf);

            auto tx_result = FrameRing::attach_shm(tx_name);
            if (!tx_result.is_ok()) {
                echo::error("Failed to attach TX ring: ", tx_name).red();
                return Result<ShmLink, Error>::err(tx_result.error());
            }

            auto rx_result = FrameRing::attach_shm(rx_name);
            if (!rx_result.is_ok()) {
                echo::error("Failed to attach RX ring: ", rx_name).red();
                return Result<ShmLink, Error>::err(rx_result.error());
            }

            echo::debug("ShmLink attached successfully: ", name).green();

            ShmLink link(name, std::move(tx_result.value()), std::move(rx_result.value()));

            // Set link model if provided
            if (model != nullptr) {
                link.set_model(*model);
            }

            return Result<ShmLink, Error>::ok(std::move(link));
        }

        /// Send a frame through the link
        /// Applies link model simulation if configured
        Result<Unit, Error> send(const Frame &frame) override {
            echo::trace("ShmLink::send: ", name_, " (src: ", frame.header.src_endpoint_id,
                        ", dst: ", frame.header.dst_endpoint_id, ")");

            stats_.frames_sent++;
            stats_.bytes_sent += frame.total_size();

            // Apply link model if configured
            if (has_model_) {
                Frame simulated_frame = frame;

                // Determine frame action (drop/duplicate/corrupt/deliver)
                auto action = determine_frame_action(model_, rng_);

                switch (action) {
                case FrameAction::DROP:
                    stats_.frames_dropped++;
                    echo::warn("Frame dropped by link model").yellow();
                    return Result<Unit, Error>::ok(Unit{});

                case FrameAction::DUPLICATE:
                    stats_.frames_duplicated++;
                    echo::warn("Frame duplicated by link model").yellow();
                    // Send original
                    {
                        auto result = tx_ring_.push_frame(simulated_frame);
                        if (!result.is_ok()) {
                            return result;
                        }
                    }
                    // Send duplicate (fall through to deliver)
                    break;

                case FrameAction::CORRUPT:
                    stats_.frames_corrupted++;
                    echo::warn("Frame corrupted by link model").yellow();
                    corrupt_payload(simulated_frame.payload, rng_);
                    break;

                case FrameAction::DELIVER:
                    // Normal delivery
                    break;
                }

                // Compute delivery time with latency and bandwidth
                uint64_t now = now_ns();
                uint64_t deliver_at =
                    compute_deliver_at_ns(model_, now, simulated_frame.payload.size(), next_send_time_, rng_);

                // Update frame's delivery timestamp
                simulated_frame.header.deliver_at_ns = deliver_at;

                return tx_ring_.push_frame(simulated_frame);
            }

            // No simulation - direct send
            return tx_ring_.push_frame(frame);
        }

        /// Receive a frame from the link
        Result<Frame, Error> recv() override {
            auto result = rx_ring_.pop_frame();
            if (result.is_ok()) {
                stats_.frames_received++;
                stats_.bytes_received += result.value().total_size();

                echo::trace("ShmLink::recv: ", name_, " (src: ", result.value().header.src_endpoint_id,
                            ", dst: ", result.value().header.dst_endpoint_id, ")");

                // Check if frame should be delayed (simulation)
                if (has_model_) {
                    uint64_t deliver_at = result.value().header.deliver_at_ns;
                    if (deliver_at > 0) {
                        uint64_t now = now_ns();
                        if (now < deliver_at) {
                            // Frame not ready yet - put it back and return timeout
                            // Note: This is a simplified approach. A real implementation
                            // would use a priority queue or timer wheel.
                            echo::trace("Frame not ready for delivery yet (delayed by simulation)");
                            return Result<Frame, Error>::err(Error::timeout("Frame delayed by simulation"));
                        }
                    }
                }
            }
            return result;
        }

        /// Check if link can send (TX ring not full)
        bool can_send() const override { return !tx_ring_.full(); }

        /// Check if link can receive (RX ring not empty)
        bool can_recv() const override { return !rx_ring_.empty(); }

        /// Get link name
        String name() const override { return name_; }

        /// Set link model for simulation
        inline void set_model(const LinkModel &model) {
            model_ = model;
            has_model_ = true;
            rng_.seed(model.seed);
            next_send_time_ = 0;
            echo::info("Link model enabled for: ", name_);
        }

        /// Clear link model (disable simulation)
        inline void clear_model() {
            has_model_ = false;
            echo::info("Link model disabled for: ", name_);
        }

        /// Check if link has model enabled
        inline bool has_model() const { return has_model_; }

        /// Get link statistics
        inline const ShmLinkStats &stats() const { return stats_; }

        /// Reset statistics
        inline void reset_stats() {
            stats_.reset();
            echo::debug("Statistics reset for: ", name_);
        }

        /// Get TX ring usage
        inline float tx_usage() const { return tx_ring_.usage(); }

        /// Get RX ring usage
        inline float rx_usage() const { return rx_ring_.usage(); }

        /// Get TX ring capacity
        inline size_t tx_capacity() const { return tx_ring_.capacity(); }

        /// Get RX ring capacity
        inline size_t rx_capacity() const { return rx_ring_.capacity(); }

      private:
        String name_;
        FrameRing tx_ring_; ///< Transmit ring (this -> other)
        FrameRing rx_ring_; ///< Receive ring (other -> this)

        // Link simulation
        bool has_model_ = false;
        LinkModel model_;
        DeterministicRNG rng_;
        uint64_t next_send_time_ = 0;

        // Statistics
        ShmLinkStats stats_;

        ShmLink(const String &name, FrameRing &&tx, FrameRing &&rx)
            : name_(name), tx_ring_(std::move(tx)), rx_ring_(std::move(rx)), rng_(0) {}
    };

} // namespace wirebit
