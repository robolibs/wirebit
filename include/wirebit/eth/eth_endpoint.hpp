#pragma once

#include <array>
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

// Undefine system macros that conflict with our constants
// (from linux/if_ether.h when HAS_HARDWARE is defined)
#ifdef ETH_ALEN
#undef ETH_ALEN
#endif
#ifdef ETH_HLEN
#undef ETH_HLEN
#endif
#ifdef ETH_ZLEN
#undef ETH_ZLEN
#endif
#ifdef ETH_DATA_LEN
#undef ETH_DATA_LEN
#endif
#ifdef ETH_FRAME_LEN
#undef ETH_FRAME_LEN
#endif
#ifdef ETH_FCS_LEN
#undef ETH_FCS_LEN
#endif
#ifdef ETH_P_IP
#undef ETH_P_IP
#endif
#ifdef ETH_P_ARP
#undef ETH_P_ARP
#endif
#ifdef ETH_P_IPV6
#undef ETH_P_IPV6
#endif
#ifdef ETH_P_8021Q
#undef ETH_P_8021Q
#endif

namespace wirebit {

    /// Ethernet frame size constants
    constexpr size_t ETH_ALEN = 6;         ///< MAC address length
    constexpr size_t ETH_HLEN = 14;        ///< Ethernet header length (dst + src + type)
    constexpr size_t ETH_ZLEN = 60;        ///< Minimum frame size (without FCS)
    constexpr size_t ETH_DATA_LEN = 1500;  ///< Maximum payload size (MTU)
    constexpr size_t ETH_FRAME_LEN = 1514; ///< Maximum frame size (without FCS)
    constexpr size_t ETH_FCS_LEN = 4;      ///< Frame check sequence length

    /// Common EtherType values
    constexpr uint16_t ETH_P_IP = 0x0800;    ///< IPv4
    constexpr uint16_t ETH_P_ARP = 0x0806;   ///< ARP
    constexpr uint16_t ETH_P_IPV6 = 0x86DD;  ///< IPv6
    constexpr uint16_t ETH_P_8021Q = 0x8100; ///< 802.1Q VLAN

    /// Ethernet MAC address type
    using MacAddr = std::array<uint8_t, ETH_ALEN>;

    /// Broadcast MAC address
    constexpr MacAddr MAC_BROADCAST = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

    /// Ethernet configuration
    struct EthConfig {
        uint64_t bandwidth_bps = 1000000000; ///< Bandwidth in bits/second (default: 1 Gbps)
        bool promiscuous = false;            ///< Promiscuous mode (receive all frames)
        size_t rx_buffer_size = 100;         ///< Receive buffer size (number of frames)
        bool calculate_fcs = false;          ///< Calculate and append FCS (normally done by hardware)
    };

    /// Ethernet L2 frame header
    struct eth_hdr {
        MacAddr dst_mac;    ///< Destination MAC address
        MacAddr src_mac;    ///< Source MAC address
        uint16_t ethertype; ///< EtherType (network byte order)
    } __attribute__((packed));

    /// Helper function to format MAC address as string
    inline String mac_to_string(const MacAddr &mac) {
        std::stringstream ss;
        ss << std::hex << std::setfill('0');
        for (size_t i = 0; i < ETH_ALEN; ++i) {
            ss << std::setw(2) << (int)mac[i];
            if (i < ETH_ALEN - 1)
                ss << ":";
        }
        return String(ss.str().c_str());
    }

    /// Helper function to parse MAC address from string
    inline Result<MacAddr, Error> string_to_mac(const String &str) {
        MacAddr mac;
        unsigned int values[ETH_ALEN];
        int count = std::sscanf(str.c_str(), "%x:%x:%x:%x:%x:%x", &values[0], &values[1], &values[2], &values[3],
                                &values[4], &values[5]);

        if (count != ETH_ALEN) {
            return Result<MacAddr, Error>::err(Error::invalid_argument("Invalid MAC address format"));
        }

        for (size_t i = 0; i < ETH_ALEN; ++i) {
            mac[i] = static_cast<uint8_t>(values[i]);
        }

        return Result<MacAddr, Error>::ok(mac);
    }

    /// Helper function to create an Ethernet frame
    inline Bytes make_eth_frame(const MacAddr &dst_mac, const MacAddr &src_mac, uint16_t ethertype,
                                const Bytes &payload) {
        // Calculate total frame size (header + payload, padded to minimum)
        size_t payload_size = payload.size();
        size_t frame_size = ETH_HLEN + payload_size;

        // Pad to minimum frame size if needed
        if (frame_size < ETH_ZLEN) {
            frame_size = ETH_ZLEN;
        }

        Bytes frame(frame_size);

        // Copy destination MAC
        std::memcpy(frame.data(), dst_mac.data(), ETH_ALEN);

        // Copy source MAC
        std::memcpy(frame.data() + ETH_ALEN, src_mac.data(), ETH_ALEN);

        // Copy EtherType (network byte order)
        frame[12] = (ethertype >> 8) & 0xFF;
        frame[13] = ethertype & 0xFF;

        // Copy payload
        if (payload_size > 0) {
            std::memcpy(frame.data() + ETH_HLEN, payload.data(), payload_size);
        }

        // Zero-pad if needed
        if (frame_size > ETH_HLEN + payload_size) {
            std::memset(frame.data() + ETH_HLEN + payload_size, 0, frame_size - ETH_HLEN - payload_size);
        }

        return frame;
    }

    /// Helper function to parse an Ethernet frame
    inline Result<Unit, Error> parse_eth_frame(const Bytes &frame, MacAddr &dst_mac, MacAddr &src_mac,
                                               uint16_t &ethertype, Bytes &payload) {
        if (frame.size() < ETH_HLEN) {
            return Result<Unit, Error>::err(Error::invalid_argument("Frame too small for Ethernet header"));
        }

        // Extract destination MAC
        std::memcpy(dst_mac.data(), frame.data(), ETH_ALEN);

        // Extract source MAC
        std::memcpy(src_mac.data(), frame.data() + ETH_ALEN, ETH_ALEN);

        // Extract EtherType (network byte order)
        ethertype = (static_cast<uint16_t>(frame[12]) << 8) | frame[13];

        // Extract payload (everything after header)
        size_t payload_size = frame.size() - ETH_HLEN;
        payload.resize(payload_size);
        if (payload_size > 0) {
            std::memcpy(payload.data(), frame.data() + ETH_HLEN, payload_size);
        }

        return Result<Unit, Error>::ok(Unit{});
    }

    /// Ethernet endpoint for L2 frame communication
    /// TAP-ready design for easy bridging to real network interfaces
    class EthEndpoint : public Endpoint {
      public:
        /// Create an Ethernet endpoint
        /// @param link Shared pointer to the underlying link
        /// @param config Ethernet configuration
        /// @param endpoint_id Unique endpoint identifier
        /// @param mac_addr MAC address for this endpoint
        inline EthEndpoint(std::shared_ptr<Link> link, const EthConfig &config, uint32_t endpoint_id,
                           const MacAddr &mac_addr)
            : link_(link), config_(config), endpoint_id_(endpoint_id), mac_addr_(mac_addr) {
            echo::info("EthEndpoint created: id=", endpoint_id_, " MAC=", mac_to_string(mac_addr_).c_str(),
                       " bandwidth=", config_.bandwidth_bps / 1000000, " Mbps");
        }

        /// Send an Ethernet frame
        /// @param eth_frame Complete L2 Ethernet frame (dst MAC + src MAC + ethertype + payload)
        /// @return Result indicating success or error
        inline Result<Unit, Error> send_eth(const Bytes &eth_frame) {
            // Validate frame size
            if (eth_frame.size() < ETH_HLEN) {
                echo::error("Frame too small for Ethernet header: ", eth_frame.size(), " bytes").red();
                return Result<Unit, Error>::err(Error::invalid_argument("Frame too small"));
            }

            if (eth_frame.size() > ETH_FRAME_LEN) {
                echo::warn("Frame exceeds MTU: ", eth_frame.size(), " bytes (max ", ETH_FRAME_LEN, ")").yellow();
            }

            // Parse frame for logging
            MacAddr dst_mac, src_mac;
            uint16_t ethertype;
            Bytes payload;
            auto parse_result = parse_eth_frame(eth_frame, dst_mac, src_mac, ethertype, payload);
            if (!parse_result.is_ok()) {
                echo::error("Failed to parse Ethernet frame: ", parse_result.error().message.c_str()).red();
                return Result<Unit, Error>::err(parse_result.error());
            }

            echo::info("Ethernet send: ", eth_frame.size(), " bytes, dst=", mac_to_string(dst_mac).c_str(),
                       " src=", mac_to_string(src_mac).c_str(), " type=0x", std::hex, std::setfill('0'), std::setw(4),
                       ethertype, std::dec);

            // Log payload details
            if (payload.size() > 0) {
                echo::debug("Payload: ", payload.size(), " bytes");
                if (payload.size() <= 32) {
                    std::stringstream ss;
                    ss << "Data: ";
                    for (size_t i = 0; i < payload.size(); ++i) {
                        ss << std::hex << std::setfill('0') << std::setw(2) << (int)payload[i];
                        if (i < payload.size() - 1)
                            ss << " ";
                    }
                    echo::trace(ss.str().c_str());
                }
            }

            // Create wirebit frame
            Frame frame = make_frame(FrameType::ETHERNET, eth_frame, endpoint_id_, 0); // 0 = broadcast

            // Calculate transmission time based on bandwidth
            // Ethernet frame on wire: preamble(8) + frame + IFG(12) = 20 bytes overhead
            uint64_t wire_bytes = eth_frame.size() + 20;
            uint64_t wire_bits = wire_bytes * 8;
            uint64_t frame_time_ns = (wire_bits * 1000000000ULL) / config_.bandwidth_bps;

            echo::debug("Ethernet frame time: ", frame_time_ns, "ns (", wire_bits, " bits at ",
                        config_.bandwidth_bps / 1000000, " Mbps)");

            // Apply bandwidth shaping
            uint64_t now = now_ns();
            last_tx_deliver_at_ns_ = std::max(now, last_tx_deliver_at_ns_) + frame_time_ns;
            frame.header.deliver_at_ns = last_tx_deliver_at_ns_;

            echo::trace("Frame deliver_at: ", frame.header.deliver_at_ns, "ns");

            // Send frame through link
            auto result = link_->send(frame);
            if (!result.is_ok()) {
                echo::error("Failed to send frame: ", result.error().message.c_str()).red();
                return result;
            }

            return Result<Unit, Error>::ok(Unit{});
        }

        /// Send data using the generic Endpoint interface
        /// Creates an Ethernet frame with this endpoint's MAC as source
        /// @param data Payload data (will be wrapped in Ethernet frame)
        /// @return Result indicating success or error
        inline Result<Unit, Error> send(const Bytes &data) override {
            // Create Ethernet frame with broadcast destination and IPv4 ethertype
            Bytes eth_frame = make_eth_frame(MAC_BROADCAST, mac_addr_, ETH_P_IP, data);
            return send_eth(eth_frame);
        }

        /// Receive an Ethernet frame (non-blocking)
        /// @return Result containing received Ethernet frame or error
        inline Result<Bytes, Error> recv_eth() {
            echo::trace("EthEndpoint::recv_eth called");

            // Process incoming frames first
            auto process_result = process();
            if (!process_result.is_ok()) {
                echo::trace("Process returned: ", process_result.error().message.c_str());
            }

            // Return buffered frame if available
            if (!rx_buffer_.empty()) {
                Bytes frame = rx_buffer_[0];
                rx_buffer_.erase(rx_buffer_.begin());

                // Parse frame for logging
                MacAddr dst_mac, src_mac;
                uint16_t ethertype;
                Bytes payload;
                parse_eth_frame(frame, dst_mac, src_mac, ethertype, payload);

                echo::info("Ethernet recv: ", frame.size(), " bytes, dst=", mac_to_string(dst_mac).c_str(),
                           " src=", mac_to_string(src_mac).c_str(), " type=0x", std::hex, std::setfill('0'),
                           std::setw(4), ethertype, std::dec);

                return Result<Bytes, Error>::ok(frame);
            }

            return Result<Bytes, Error>::err(Error::timeout("No frames available"));
        }

        /// Receive data using the generic Endpoint interface
        /// @return Result containing received payload (Ethernet frame) or error
        inline Result<Bytes, Error> recv() override { return recv_eth(); }

        /// Process incoming frames from the link
        /// @return Result indicating success or error
        inline Result<Unit, Error> process() override {
            echo::trace("EthEndpoint::process called");

            // Receive frame from link
            auto result = link_->recv();
            if (!result.is_ok()) {
                return Result<Unit, Error>::err(result.error());
            }

            Frame frame = result.value();

            // Validate frame type
            if (frame.header.frame_type != static_cast<uint16_t>(FrameType::ETHERNET)) {
                echo::warn("Received non-Ethernet frame, ignoring").yellow();
                return Result<Unit, Error>::err(Error::invalid_argument("Wrong frame type"));
            }

            // Wait until frame is ready to be delivered
            uint64_t now = now_ns();
            if (now < frame.header.deliver_at_ns) {
                int64_t wait_ns = frame.header.deliver_at_ns - now;
                echo::trace("Waiting ", wait_ns, "ns for frame delivery");
                usleep(wait_ns / 1000);
            }

            // Extract Ethernet frame from payload
            Bytes eth_frame = frame.payload;

            // Parse frame for filtering
            MacAddr dst_mac, src_mac;
            uint16_t ethertype;
            Bytes payload;
            auto parse_result = parse_eth_frame(eth_frame, dst_mac, src_mac, ethertype, payload);
            if (!parse_result.is_ok()) {
                echo::warn("Failed to parse received frame: ", parse_result.error().message.c_str()).yellow();
                return Result<Unit, Error>::err(parse_result.error());
            }

            // Filter frames unless in promiscuous mode
            bool is_for_us = false;
            if (config_.promiscuous) {
                is_for_us = true;
                echo::trace("Promiscuous mode: accepting all frames");
            } else {
                // Accept if destination is our MAC or broadcast
                if (dst_mac == mac_addr_ || dst_mac == MAC_BROADCAST) {
                    is_for_us = true;
                    echo::trace("Frame is for us (dst=", mac_to_string(dst_mac).c_str(), ")");
                } else {
                    echo::trace("Frame not for us (dst=", mac_to_string(dst_mac).c_str(), "), dropping");
                }
            }

            if (!is_for_us) {
                return Result<Unit, Error>::err(Error::invalid_argument("Frame not for this endpoint"));
            }

            // Buffer the frame
            if (rx_buffer_.size() >= config_.rx_buffer_size) {
                echo::warn("RX buffer full, dropping oldest frame").yellow();
                rx_buffer_.erase(rx_buffer_.begin());
            }

            rx_buffer_.push_back(eth_frame);
            echo::debug("Frame buffered, rx_buffer size: ", rx_buffer_.size());

            return Result<Unit, Error>::ok(Unit{});
        }

        /// Get the MAC address of this endpoint
        /// @return MAC address
        inline const MacAddr &get_mac_addr() const { return mac_addr_; }

        /// Get the endpoint configuration
        /// @return Ethernet configuration
        inline const EthConfig &get_config() const { return config_; }

        /// Get endpoint name
        /// @return Endpoint name
        inline String name() const override {
            char buf[32];
            snprintf(buf, sizeof(buf), "eth_%u", endpoint_id_);
            return String(buf);
        }

        /// Get the underlying link
        /// @return Pointer to the link
        inline Link *link() override { return link_.get(); }

        /// Get endpoint ID
        /// @return Endpoint ID
        inline uint32_t endpoint_id() const { return endpoint_id_; }

        /// Get number of frames in receive buffer
        /// @return Number of buffered frames
        inline size_t rx_buffer_size() const { return rx_buffer_.size(); }

        /// Clear receive buffer
        inline void clear_rx_buffer() {
            echo::debug("Clearing RX buffer: ", rx_buffer_.size(), " frames discarded");
            rx_buffer_.clear();
        }

      private:
        std::shared_ptr<Link> link_;        ///< Underlying link
        EthConfig config_;                  ///< Ethernet configuration
        uint32_t endpoint_id_;              ///< Endpoint identifier
        MacAddr mac_addr_;                  ///< MAC address
        Vector<Bytes> rx_buffer_;           ///< Receive buffer
        uint64_t last_tx_deliver_at_ns_{0}; ///< Last transmission delivery time
    };

    /// Helper function to create a standard Ethernet endpoint with auto-generated MAC
    inline std::shared_ptr<EthEndpoint> make_eth_endpoint(std::shared_ptr<Link> link, uint32_t endpoint_id,
                                                          uint64_t bandwidth_bps = 1000000000) {
        // Generate MAC address from endpoint ID (locally administered)
        MacAddr mac = {0x02,
                       0x00,
                       0x00,
                       0x00,
                       static_cast<uint8_t>((endpoint_id >> 8) & 0xFF),
                       static_cast<uint8_t>(endpoint_id & 0xFF)};

        EthConfig config;
        config.bandwidth_bps = bandwidth_bps;

        return std::make_shared<EthEndpoint>(link, config, endpoint_id, mac);
    }

} // namespace wirebit
