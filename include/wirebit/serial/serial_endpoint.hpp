#pragma once

#include <echo/echo.hpp>
#include <memory>
#include <wirebit/common/time.hpp>
#include <wirebit/common/types.hpp>
#include <wirebit/endpoint.hpp>
#include <wirebit/frame.hpp>
#include <wirebit/link.hpp>

namespace wirebit {

    /// Serial port configuration
    struct SerialConfig {
        uint32_t baud = 115200;      ///< Baud rate (bits per second)
        uint8_t data_bits = 8;       ///< Data bits (5-8)
        uint8_t stop_bits = 1;       ///< Stop bits (1 or 2)
        char parity = 'N';           ///< Parity: 'N' (none), 'E' (even), 'O' (odd)
        size_t max_chunk_read = 256; ///< Maximum bytes to read in one recv() call
    };

    /// Serial endpoint for byte-stream communication
    /// Simulates serial port timing and framing behavior
    class SerialEndpoint : public Endpoint {
      public:
        /// Create a serial endpoint
        /// @param link Shared pointer to the underlying link
        /// @param config Serial port configuration
        /// @param endpoint_id Unique endpoint identifier
        inline SerialEndpoint(std::shared_ptr<Link> link, const SerialConfig &config, uint32_t endpoint_id)
            : link_(link), config_(config), endpoint_id_(endpoint_id) {
            echo::trace("SerialEndpoint created: id=", endpoint_id_, " baud=", config_.baud,
                        " data=", (int)config_.data_bits, " stop=", (int)config_.stop_bits, " parity=", config_.parity);
        }

        /// Send data through the serial endpoint
        /// Converts bytes to frames with proper baud rate pacing
        /// @param data Bytes to send
        /// @return Result indicating success or error
        inline Result<Unit, Error> send(const Bytes &data) override {
            if (data.empty()) {
                echo::warn("SerialEndpoint::send called with empty data");
                return Result<Unit, Error>::ok(Unit{});
            }

            echo::trace("Serial send: ", data.size(), " bytes at ", config_.baud, " baud");

            // Calculate byte transmission time based on baud rate
            uint32_t bits_per_byte = 1 + config_.data_bits + config_.stop_bits; // Start + data + stop
            if (config_.parity != 'N') {
                bits_per_byte++; // Add parity bit
            }
            uint64_t byte_time_ns = (bits_per_byte * 1000000000ULL) / config_.baud;

            echo::debug("Byte time: ", byte_time_ns, "ns (", bits_per_byte, " bits/byte)");

            uint64_t now = now_ns();

            // Send each byte as a separate frame with proper timing
            for (size_t i = 0; i < data.size(); ++i) {
                Byte byte = data[i];
                echo::trace("Sending byte[", i, "]: 0x", std::hex, (int)byte, std::dec);

                // Create frame with single byte payload
                Bytes payload(1);
                payload[0] = byte;
                Frame frame = make_frame(FrameType::SERIAL, payload, endpoint_id_, 0);

                // Pace bytes according to baud rate
                // Each byte is sent after the previous one completes
                last_tx_deliver_at_ns_ = std::max(now, last_tx_deliver_at_ns_) + byte_time_ns;
                frame.header.deliver_at_ns = last_tx_deliver_at_ns_;

                echo::trace("Frame deliver_at: ", frame.header.deliver_at_ns, "ns");

                // Send frame through link
                auto result = link_->send(frame);
                if (!result.is_ok()) {
                    echo::error("Failed to send frame: ", result.error().message.c_str()).red();
                    return result;
                }
            }

            echo::trace("Serial send complete: ", data.size(), " bytes");
            return Result<Unit, Error>::ok(Unit{});
        }

        /// Receive data from the serial endpoint (non-blocking)
        /// @return Result containing received bytes or error
        inline Result<Bytes, Error> recv() override {
            echo::trace("SerialEndpoint::recv called");

            // Process incoming frames first
            auto process_result = process();
            if (!process_result.is_ok()) {
                // Process errors are non-fatal, just means no frames available
                echo::trace("Process returned: ", process_result.error().message.c_str());
            }

            // Return buffered data if available
            if (!rx_buffer_.empty()) {
                size_t to_copy = std::min(rx_buffer_.size(), config_.max_chunk_read);
                Bytes data(to_copy);
                for (size_t i = 0; i < to_copy; ++i) {
                    data[i] = rx_buffer_[i];
                }
                rx_buffer_.erase(rx_buffer_.begin(), rx_buffer_.begin() + to_copy);

                echo::debug("Serial recv: ", data.size(), " bytes (", rx_buffer_.size(), " remaining in buffer)");
                return Result<Bytes, Error>::ok(std::move(data));
            }

            // No data available
            echo::trace("Serial recv: no data available");
            return Result<Bytes, Error>::err(Error::timeout("No data available"));
        }

        /// Process incoming frames from the link
        /// Converts frames to bytes and buffers them
        /// @return Result indicating success or error
        inline Result<Unit, Error> process() override {
            echo::trace("SerialEndpoint::process");

            // Try to receive frames from the link
            while (true) {
                auto frame_result = link_->recv();
                if (!frame_result.is_ok()) {
                    // No more frames available
                    if (rx_buffer_.empty()) {
                        return Result<Unit, Error>::err(Error::timeout("No frames available"));
                    }
                    return Result<Unit, Error>::ok(Unit{});
                }

                Frame frame = std::move(frame_result.value());

                // Verify frame type
                if (frame.type() != FrameType::SERIAL) {
                    echo::warn("Received non-SERIAL frame, ignoring");
                    continue;
                }

                // Enforce delivery timing (simulate serial port timing)
                uint64_t now = now_ns();
                if (frame.header.deliver_at_ns > 0 && now < frame.header.deliver_at_ns) {
                    uint64_t delay_ns = frame.header.deliver_at_ns - now;
                    echo::trace("Delaying frame delivery by ", delay_ns, "ns");
                    // In a real implementation, we might want to queue this frame
                    // For now, we just note that it arrived early
                }

                // Add frame payload to receive buffer
                echo::trace("Processing frame: ", frame.payload.size(), " bytes");
                for (size_t i = 0; i < frame.payload.size(); ++i) {
                    rx_buffer_.push_back(frame.payload[i]);
                }

                echo::debug("RX buffer size: ", rx_buffer_.size(), " bytes");
            }

            return Result<Unit, Error>::ok(Unit{});
        }

        /// Get endpoint name
        /// @return Endpoint name string
        inline String name() const override {
            char buf[32];
            snprintf(buf, sizeof(buf), "serial_%u", endpoint_id_);
            return String(buf);
        }

        /// Get the underlying link
        /// @return Pointer to the link
        inline Link *link() override { return link_.get(); }

        /// Get serial configuration
        /// @return Serial configuration
        inline const SerialConfig &config() const { return config_; }

        /// Get endpoint ID
        /// @return Endpoint ID
        inline uint32_t endpoint_id() const { return endpoint_id_; }

        /// Get number of bytes in receive buffer
        /// @return Number of buffered bytes
        inline size_t rx_buffer_size() const { return rx_buffer_.size(); }

        /// Clear receive buffer
        inline void clear_rx_buffer() {
            echo::debug("Clearing RX buffer: ", rx_buffer_.size(), " bytes discarded");
            rx_buffer_.clear();
        }

      private:
        std::shared_ptr<Link> link_;         ///< Underlying communication link
        SerialConfig config_;                ///< Serial port configuration
        Vector<Byte> rx_buffer_;             ///< Receive buffer for incoming bytes
        uint64_t last_tx_deliver_at_ns_ = 0; ///< Last transmission delivery time (for pacing)
        uint32_t endpoint_id_;               ///< Unique endpoint identifier
    };

} // namespace wirebit
