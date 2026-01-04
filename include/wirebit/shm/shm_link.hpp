#pragma once

#include <echo/echo.hpp>
#include <wirebit/common/types.hpp>
#include <wirebit/link.hpp>
#include <wirebit/shm/ring.hpp>

namespace wirebit {

    /// Bidirectional shared memory link using two SPSC ring buffers
    /// TODO: Full implementation in wirebit-p3e.7
    class ShmLink : public Link {
      public:
        /// Create a new shared memory link (server side)
        /// @param name Link name
        /// @param capacity_bytes Capacity of each ring buffer in bytes
        /// @return Result containing ShmLink or error
        static Result<ShmLink, Error> create(const String &name, size_t capacity_bytes) {
            echo::info("Creating ShmLink: ", name, " (capacity: ", capacity_bytes, " bytes)");

            String tx_name = name + "_tx";
            String rx_name = name + "_rx";

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
            return Result<ShmLink, Error>::ok(
                ShmLink(name, std::move(tx_result.value()), std::move(rx_result.value())));
        }

        /// Attach to an existing shared memory link (client side)
        /// @param name Link name
        /// @return Result containing ShmLink or error
        static Result<ShmLink, Error> attach(const String &name) {
            echo::info("Attaching to ShmLink: ", name);

            // Note: TX/RX are swapped for client (client's TX is server's RX)
            String tx_name = name + "_rx";
            String rx_name = name + "_tx";

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
            return Result<ShmLink, Error>::ok(
                ShmLink(name, std::move(tx_result.value()), std::move(rx_result.value())));
        }

        Result<Unit, Error> send(const Frame &frame) override {
            echo::trace("ShmLink::send: ", name_, " (frame_id: ", frame.header.frame_id, ")");
            return tx_ring_.push_frame(frame);
        }

        Result<Frame, Error> recv() override {
            auto result = rx_ring_.pop_frame();
            if (result.is_ok()) {
                echo::trace("ShmLink::recv: ", name_, " (frame_id: ", result.value().header.frame_id, ")");
            }
            return result;
        }

        bool can_send() const override { return !tx_ring_.full(); }

        bool can_recv() const override { return !rx_ring_.empty(); }

        String name() const override { return name_; }

      private:
        String name_;
        FrameRing tx_ring_; ///< Transmit ring (this -> other)
        FrameRing rx_ring_; ///< Receive ring (other -> this)

        ShmLink(const String &name, FrameRing &&tx, FrameRing &&rx)
            : name_(name), tx_ring_(std::move(tx)), rx_ring_(std::move(rx)) {}
    };

} // namespace wirebit
