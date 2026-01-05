#pragma once

#include <cstring>
#include <echo/echo.hpp>
#include <wirebit/common/time.hpp>
#include <wirebit/common/types.hpp>

namespace wirebit {

    /// Frame type enumeration
    enum class FrameType : uint16_t {
        SERIAL = 1,   ///< Serial data frame
        CAN = 2,      ///< CAN bus frame
        ETHERNET = 3, ///< Ethernet L2 frame (TAP)
        IP = 4,       ///< IP L3 packet (TUN)
    };

    /// Frame header structure (packed for stable wire format)
    struct FrameHeader {
        uint32_t magic = 0x57424954; ///< Magic number 'WBIT'
        uint16_t version = 1;        ///< Protocol version
        uint16_t frame_type;         ///< FrameType enum value
        uint32_t flags = 0;          ///< Frame flags (reserved)
        uint64_t tx_timestamp_ns;    ///< Transmission timestamp (nanoseconds)
        uint64_t deliver_at_ns;      ///< Delivery timestamp for simulation (0 = immediate)
        uint32_t src_endpoint_id;    ///< Source endpoint ID
        uint32_t dst_endpoint_id;    ///< Destination endpoint ID (0 = broadcast)
        uint32_t payload_len;        ///< Payload length in bytes
        uint32_t meta_len;           ///< Metadata length in bytes

        FrameHeader()
            : magic(0x57424954), version(1), frame_type(0), flags(0), tx_timestamp_ns(0), deliver_at_ns(0),
              src_endpoint_id(0), dst_endpoint_id(0), payload_len(0), meta_len(0) {}
    } __attribute__((packed));

    static_assert(sizeof(FrameHeader) == 44, "FrameHeader must be 44 bytes");

    /// Frame structure with header, payload, and metadata
    struct Frame {
        FrameHeader header; ///< Frame header
        Bytes payload;      ///< Frame payload data
        Bytes meta;         ///< Frame metadata (optional)

        Frame() = default;

        Frame(FrameType type, const Bytes &payload_data, uint32_t src_id = 0, uint32_t dst_id = 0, uint64_t tx_ts = 0,
              uint64_t deliver_ts = 0)
            : payload(payload_data) {
            header.frame_type = static_cast<uint16_t>(type);
            header.src_endpoint_id = src_id;
            header.dst_endpoint_id = dst_id;
            header.tx_timestamp_ns = tx_ts;
            header.deliver_at_ns = deliver_ts;
            header.payload_len = static_cast<uint32_t>(payload.size());
            header.meta_len = 0;
        }

        Frame(FrameType type, Bytes &&payload_data, uint32_t src_id = 0, uint32_t dst_id = 0, uint64_t tx_ts = 0,
              uint64_t deliver_ts = 0)
            : payload(std::move(payload_data)) {
            header.frame_type = static_cast<uint16_t>(type);
            header.src_endpoint_id = src_id;
            header.dst_endpoint_id = dst_id;
            header.tx_timestamp_ns = tx_ts;
            header.deliver_at_ns = deliver_ts;
            header.payload_len = static_cast<uint32_t>(payload.size());
            header.meta_len = 0;
        }

        /// Get frame type
        inline FrameType type() const { return static_cast<FrameType>(header.frame_type); }

        /// Get total frame size (header + payload + meta)
        inline size_t total_size() const { return sizeof(FrameHeader) + header.payload_len + header.meta_len; }

        /// Check if frame is broadcast
        inline bool is_broadcast() const { return header.dst_endpoint_id == 0; }

        /// Set metadata
        inline void set_meta(const Bytes &meta_data) {
            meta = meta_data;
            header.meta_len = static_cast<uint32_t>(meta.size());
        }

        /// Set metadata (move)
        inline void set_meta(Bytes &&meta_data) {
            meta = std::move(meta_data);
            header.meta_len = static_cast<uint32_t>(meta.size());
        }
    };

    /// Helper: Create a frame with current timestamp
    inline Frame make_frame(FrameType type, const Bytes &payload, uint32_t src_id = 0, uint32_t dst_id = 0) {
        uint64_t ts = now_ns();
        return Frame(type, payload, src_id, dst_id, ts, 0);
    }

    /// Helper: Create a frame with current timestamp (move payload)
    inline Frame make_frame(FrameType type, Bytes &&payload, uint32_t src_id = 0, uint32_t dst_id = 0) {
        uint64_t ts = now_ns();
        return Frame(type, std::move(payload), src_id, dst_id, ts, 0);
    }

    /// Helper: Create a frame with explicit timestamps
    inline Frame make_frame_with_timestamps(FrameType type, const Bytes &payload, uint64_t tx_timestamp_ns,
                                            uint64_t deliver_at_ns = 0, uint32_t src_id = 0, uint32_t dst_id = 0) {
        return Frame(type, payload, src_id, dst_id, tx_timestamp_ns, deliver_at_ns);
    }

    /// Encode frame to bytes
    /// Format: [FrameHeader][payload bytes][meta bytes]
    inline Bytes encode_frame(const Frame &frame) {
        echo::trace("Encoding frame: type=", frame.header.frame_type, " payload=", frame.header.payload_len,
                    " meta=", frame.header.meta_len);

        Bytes result;
        size_t total_size = sizeof(FrameHeader) + frame.payload.size() + frame.meta.size();
        result.reserve(total_size);

        // Encode header
        const auto *header_bytes = reinterpret_cast<const Byte *>(&frame.header);
        result.insert(result.end(), header_bytes, header_bytes + sizeof(FrameHeader));

        // Encode payload
        result.insert(result.end(), frame.payload.begin(), frame.payload.end());

        // Encode metadata
        if (!frame.meta.empty()) {
            result.insert(result.end(), frame.meta.begin(), frame.meta.end());
        }

        echo::trace("Frame encoded: ", result.size(), " bytes");
        return result;
    }

    /// Decode frame from bytes
    inline Result<Frame, Error> decode_frame(const Bytes &data) {
        echo::trace("Decoding frame, size: ", data.size());

        // Check minimum size
        if (data.size() < sizeof(FrameHeader)) {
            echo::error("Frame too small: ", data.size(), " < ", sizeof(FrameHeader)).red();
            return Result<Frame, Error>::err(Error::invalid_argument("Frame data too small for header"));
        }

        // Decode header
        Frame frame;
        std::memcpy(&frame.header, data.data(), sizeof(FrameHeader));

        // Validate magic number
        if (frame.header.magic != 0x57424954) {
            echo::error("Invalid frame magic: 0x", std::hex, frame.header.magic).red();
            return Result<Frame, Error>::err(Error::invalid_argument("Invalid frame magic number"));
        }

        // Validate version
        if (frame.header.version != 1) {
            echo::error("Unsupported frame version: ", frame.header.version).red();
            return Result<Frame, Error>::err(Error::invalid_argument("Unsupported frame version"));
        }

        // Check total size
        size_t expected_size = sizeof(FrameHeader) + frame.header.payload_len + frame.header.meta_len;
        if (data.size() < expected_size) {
            echo::error("Frame data incomplete: ", data.size(), " < ", expected_size).red();
            return Result<Frame, Error>::err(Error::invalid_argument("Frame data incomplete"));
        }

        // Decode payload
        size_t offset = sizeof(FrameHeader);
        if (frame.header.payload_len > 0) {
            frame.payload.assign(data.begin() + offset, data.begin() + offset + frame.header.payload_len);
            offset += frame.header.payload_len;
        }

        // Decode metadata
        if (frame.header.meta_len > 0) {
            frame.meta.assign(data.begin() + offset, data.begin() + offset + frame.header.meta_len);
        }

        echo::debug("Frame decoded: type=", frame.header.frame_type, " src=", frame.header.src_endpoint_id,
                    " dst=", frame.header.dst_endpoint_id, " payload=", frame.header.payload_len,
                    " meta=", frame.header.meta_len);

        return Result<Frame, Error>::ok(std::move(frame));
    }

    /// Validate frame header (without decoding payload)
    inline Result<Unit, Error> validate_frame_header(const Bytes &data) {
        if (data.size() < sizeof(FrameHeader)) {
            return Result<Unit, Error>::err(Error::invalid_argument("Data too small for frame header"));
        }

        FrameHeader header;
        std::memcpy(&header, data.data(), sizeof(FrameHeader));

        if (header.magic != 0x57424954) {
            return Result<Unit, Error>::err(Error::invalid_argument("Invalid frame magic number"));
        }

        if (header.version != 1) {
            return Result<Unit, Error>::err(Error::invalid_argument("Unsupported frame version"));
        }

        return Result<Unit, Error>::ok(Unit{});
    }

    /// Get frame type from encoded data (without full decode)
    inline Result<FrameType, Error> peek_frame_type(const Bytes &data) {
        if (data.size() < sizeof(FrameHeader)) {
            return Result<FrameType, Error>::err(Error::invalid_argument("Data too small for frame header"));
        }

        FrameHeader header;
        std::memcpy(&header, data.data(), sizeof(FrameHeader));

        return Result<FrameType, Error>::ok(static_cast<FrameType>(header.frame_type));
    }

} // namespace wirebit
