#pragma once

#include <cstring>
#include <echo/echo.hpp>
#include <iomanip>
#include <memory>
#include <sstream>
#include <wirebit/common/time.hpp>
#include <wirebit/common/types.hpp>
#include <wirebit/endpoint.hpp>
#include <wirebit/frame.hpp>
#include <wirebit/link.hpp>

#ifndef NO_HARDWARE
// Use actual Linux SocketCAN headers when hardware support is enabled
#include <linux/can.h>
#else
// Define our own can_frame when building without hardware support
// This matches the layout of struct can_frame from <linux/can.h>
struct can_frame {
    uint32_t can_id; ///< CAN ID + EFF/RTR/ERR flags
    uint8_t can_dlc; ///< Data length code (0-8)
    uint8_t __pad;   ///< Padding
    uint8_t __res0;  ///< Reserved
    uint8_t __res1;  ///< Reserved
    uint8_t data[8]; ///< CAN data bytes
} __attribute__((packed));

// CAN ID flags (compatible with Linux SocketCAN)
// Using inline constexpr when NO_HARDWARE is defined
inline constexpr uint32_t CAN_EFF_FLAG = 0x80000000U; ///< Extended frame format (29-bit ID)
inline constexpr uint32_t CAN_RTR_FLAG = 0x40000000U; ///< Remote transmission request
inline constexpr uint32_t CAN_ERR_FLAG = 0x20000000U; ///< Error frame
inline constexpr uint32_t CAN_SFF_MASK = 0x000007FFU; ///< Standard frame format mask (11-bit)
inline constexpr uint32_t CAN_EFF_MASK = 0x1FFFFFFFU; ///< Extended frame format mask (29-bit)
#endif

namespace wirebit {

    // Import can_frame into wirebit namespace for convenience
    using ::can_frame;

    // Import CAN flags into wirebit namespace
    inline constexpr uint32_t CAN_EFF_FLAG_V = CAN_EFF_FLAG;
    inline constexpr uint32_t CAN_RTR_FLAG_V = CAN_RTR_FLAG;
    inline constexpr uint32_t CAN_ERR_FLAG_V = CAN_ERR_FLAG;
    inline constexpr uint32_t CAN_SFF_MASK_V = CAN_SFF_MASK;
    inline constexpr uint32_t CAN_EFF_MASK_V = CAN_EFF_MASK;

    /// CAN bus configuration
    struct CanConfig {
        uint32_t bitrate = 500000;   ///< CAN bitrate in bits/second (default: 500 kbps)
        bool loopback = false;       ///< Enable loopback mode
        bool listen_only = false;    ///< Enable listen-only mode
        size_t rx_buffer_size = 100; ///< Receive buffer size (number of frames)
    };

    /// CAN endpoint for CAN bus communication
    /// Compatible with Linux SocketCAN for easy bridging
    class CanEndpoint : public Endpoint {
      public:
        /// Create a CAN endpoint
        /// @param link Shared pointer to the underlying link
        /// @param config CAN bus configuration
        /// @param endpoint_id Unique endpoint identifier
        inline CanEndpoint(std::shared_ptr<Link> link, const CanConfig &config, uint32_t endpoint_id)
            : link_(link), config_(config), endpoint_id_(endpoint_id) {
            echo::trace("CanEndpoint created: id=", endpoint_id_, " bitrate=", config_.bitrate, " bps");
        }

        /// Send a CAN frame
        /// @param cf CAN frame to send
        /// @return Result indicating success or error
        inline Result<Unit, Error> send_can(const can_frame &cf) {
            // Validate DLC
            if (cf.can_dlc > 8) {
                echo::error("Invalid CAN DLC: ", (int)cf.can_dlc, " (max 8)").red();
                return Result<Unit, Error>::err(Error::invalid_argument("CAN DLC must be 0-8"));
            }

            // Log CAN frame details
            echo::trace("CAN send: ID=0x", std::hex, std::setfill('0'), std::setw(cf.can_id & CAN_EFF_FLAG ? 8 : 3),
                        (cf.can_id & CAN_EFF_MASK), std::dec, " DLC=", (int)cf.can_dlc);

            // Log data bytes
            if (cf.can_dlc > 0) {
                std::stringstream ss;
                ss << "CAN data: ";
                for (int i = 0; i < cf.can_dlc; ++i) {
                    ss << std::hex << std::setfill('0') << std::setw(2) << (int)cf.data[i];
                    if (i < cf.can_dlc - 1)
                        ss << " ";
                }
                echo::debug(ss.str().c_str());
            }

            // Serialize CAN frame to payload
            Bytes payload(sizeof(can_frame));
            std::memcpy(payload.data(), &cf, sizeof(can_frame));

            // Create wirebit frame
            Frame frame = make_frame(FrameType::CAN, payload, endpoint_id_, 0); // 0 = broadcast

            // Calculate transmission time based on bitrate
            // CAN frame overhead: SOF(1) + ID(11/29) + RTR(1) + IDE(1) + r0(1) + DLC(4) + CRC(15) + ACK(2) + EOF(7) +
            // IFS(3) Standard frame: ~47 bits overhead + data bits Extended frame: ~67 bits overhead + data bits
            uint32_t overhead_bits = (cf.can_id & CAN_EFF_FLAG) ? 67 : 47;
            uint32_t data_bits = cf.can_dlc * 8;
            uint32_t total_bits = overhead_bits + data_bits;

            // Add bit stuffing overhead (worst case: 20% more bits)
            total_bits = total_bits + (total_bits / 5);

            uint64_t frame_time_ns = (total_bits * 1000000000ULL) / config_.bitrate;

            echo::debug("CAN frame time: ", frame_time_ns, "ns (", total_bits, " bits at ", config_.bitrate, " bps)");

            // Set delivery time for bandwidth shaping
            uint64_t now = now_ns();
            last_tx_deliver_at_ns_ = std::max(now, last_tx_deliver_at_ns_) + frame_time_ns;
            frame.header.deliver_at_ns = last_tx_deliver_at_ns_;

            // Send frame through link
            auto result = link_->send(frame);
            if (!result.is_ok()) {
                echo::error("CAN send failed: ", result.error().message.c_str()).red();
                return result;
            }

            echo::trace("CAN frame sent successfully");
            return Result<Unit, Error>::ok(Unit{});
        }

        /// Receive a CAN frame (non-blocking)
        /// @param cf Output CAN frame
        /// @return Result indicating success or error
        inline Result<Unit, Error> recv_can(can_frame &cf) {
            echo::trace("CanEndpoint::recv_can called");

            // Process incoming frames first
            auto process_result = process();
            if (!process_result.is_ok()) {
                echo::trace("Process returned: ", process_result.error().message.c_str());
            }

            // Return buffered frame if available
            if (!rx_buffer_.empty()) {
                cf = rx_buffer_[0];
                rx_buffer_.erase(rx_buffer_.begin(), rx_buffer_.begin() + 1);

                echo::debug("CAN recv: ID=0x", std::hex, (cf.can_id & CAN_EFF_MASK), std::dec, " DLC=", (int)cf.can_dlc,
                            " (", rx_buffer_.size(), " frames remaining)");
                return Result<Unit, Error>::ok(Unit{});
            }

            // No frames available
            echo::trace("CAN recv: no frames available");
            return Result<Unit, Error>::err(Error::timeout("No CAN frames available"));
        }

        /// Send data through the endpoint (Endpoint interface)
        /// @param data Serialized CAN frame
        /// @return Result indicating success or error
        inline Result<Unit, Error> send(const Bytes &data) override {
            if (data.size() != sizeof(can_frame)) {
                echo::error("Invalid CAN frame size: ", data.size(), " (expected ", sizeof(can_frame), ")").red();
                return Result<Unit, Error>::err(Error::invalid_argument("Invalid CAN frame size"));
            }

            can_frame cf;
            std::memcpy(&cf, data.data(), sizeof(can_frame));
            return send_can(cf);
        }

        /// Receive data from the endpoint (Endpoint interface)
        /// @return Result containing serialized CAN frame or error
        inline Result<Bytes, Error> recv() override {
            can_frame cf;
            auto result = recv_can(cf);
            if (!result.is_ok()) {
                return Result<Bytes, Error>::err(result.error());
            }

            Bytes data(sizeof(can_frame));
            std::memcpy(data.data(), &cf, sizeof(can_frame));
            return Result<Bytes, Error>::ok(std::move(data));
        }

        /// Process incoming frames from the link
        /// @return Result indicating success or error
        inline Result<Unit, Error> process() override {
            echo::trace("CanEndpoint::process");

            // Try to receive frames from the link
            while (rx_buffer_.size() < config_.rx_buffer_size) {
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
                if (frame.type() != FrameType::CAN) {
                    echo::warn("Received non-CAN frame, ignoring");
                    continue;
                }

                // Verify payload size
                if (frame.payload.size() != sizeof(can_frame)) {
                    echo::warn("Invalid CAN frame payload size: ", frame.payload.size());
                    continue;
                }

                // Enforce delivery timing
                uint64_t now = now_ns();
                if (frame.header.deliver_at_ns > 0 && now < frame.header.deliver_at_ns) {
                    uint64_t delay_ns = frame.header.deliver_at_ns - now;
                    echo::trace("Delaying CAN frame delivery by ", delay_ns, "ns");
                }

                // Deserialize CAN frame
                can_frame cf;
                std::memcpy(&cf, frame.payload.data(), sizeof(can_frame));

                // Add to receive buffer
                rx_buffer_.push_back(cf);
                echo::trace("CAN frame buffered: ID=0x", std::hex, (cf.can_id & CAN_EFF_MASK), std::dec,
                            " (buffer size: ", rx_buffer_.size(), ")");
            }

            return Result<Unit, Error>::ok(Unit{});
        }

        /// Get endpoint name
        /// @return Endpoint name string
        inline String name() const override {
            char buf[32];
            snprintf(buf, sizeof(buf), "can_%u", endpoint_id_);
            return String(buf);
        }

        /// Get the underlying link
        /// @return Pointer to the link
        inline Link *link() override { return link_.get(); }

        /// Get CAN configuration
        /// @return CAN configuration
        inline const CanConfig &config() const { return config_; }

        /// Get endpoint ID
        /// @return Endpoint ID
        inline uint32_t endpoint_id() const { return endpoint_id_; }

        /// Get number of frames in receive buffer
        /// @return Number of buffered frames
        inline size_t rx_buffer_size() const { return rx_buffer_.size(); }

        /// Clear receive buffer
        inline void clear_rx_buffer() {
            echo::debug("Clearing CAN RX buffer: ", rx_buffer_.size(), " frames discarded");
            rx_buffer_.clear();
        }

        /// Create a standard CAN frame (11-bit ID)
        /// @param id CAN identifier (0-0x7FF)
        /// @param data Data bytes
        /// @param dlc Data length code
        /// @return CAN frame
        static inline can_frame make_std_frame(uint32_t id, const uint8_t *data, uint8_t dlc) {
            can_frame cf = {};
            cf.can_id = id & CAN_SFF_MASK;
            cf.can_dlc = dlc > 8 ? 8 : dlc;
            if (data && dlc > 0) {
                std::memcpy(cf.data, data, cf.can_dlc);
            }
            return cf;
        }

        /// Create an extended CAN frame (29-bit ID)
        /// @param id CAN identifier (0-0x1FFFFFFF)
        /// @param data Data bytes
        /// @param dlc Data length code
        /// @return CAN frame
        static inline can_frame make_ext_frame(uint32_t id, const uint8_t *data, uint8_t dlc) {
            can_frame cf = {};
            cf.can_id = (id & CAN_EFF_MASK) | CAN_EFF_FLAG;
            cf.can_dlc = dlc > 8 ? 8 : dlc;
            if (data && dlc > 0) {
                std::memcpy(cf.data, data, cf.can_dlc);
            }
            return cf;
        }

        /// Create an RTR frame (Remote Transmission Request)
        /// @param id CAN identifier
        /// @param extended Use extended frame format
        /// @return CAN frame
        static inline can_frame make_rtr_frame(uint32_t id, bool extended = false) {
            can_frame cf = {};
            if (extended) {
                cf.can_id = (id & CAN_EFF_MASK) | CAN_EFF_FLAG | CAN_RTR_FLAG;
            } else {
                cf.can_id = (id & CAN_SFF_MASK) | CAN_RTR_FLAG;
            }
            cf.can_dlc = 0;
            return cf;
        }

      private:
        std::shared_ptr<Link> link_;         ///< Underlying communication link
        CanConfig config_;                   ///< CAN bus configuration
        Vector<can_frame> rx_buffer_;        ///< Receive buffer for incoming frames
        uint64_t last_tx_deliver_at_ns_ = 0; ///< Last transmission delivery time (for pacing)
        uint32_t endpoint_id_;               ///< Unique endpoint identifier
    };

} // namespace wirebit
