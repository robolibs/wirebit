#pragma once

#include <wirebit/common/time.hpp>
#include <wirebit/common/types.hpp>

namespace wirebit {

    /// Frame type enumeration
    enum class FrameType : uint8_t {
        SERIAL = 0, ///< Serial data frame
        CAN = 1,    ///< CAN bus frame
        ETH = 2,    ///< Ethernet frame
    };

    /// Frame header structure (fixed size)
    struct FrameHeader {
        FrameType frame_type; ///< Type of frame
        uint8_t reserved[3];  ///< Reserved for alignment (padding to 4 bytes)
        uint32_t payload_len; ///< Length of payload in bytes
        TimeNs timestamp;     ///< Timestamp when frame was created (nanoseconds)
        FrameId frame_id;     ///< Unique frame identifier

        FrameHeader() : frame_type(FrameType::SERIAL), reserved{0, 0, 0}, payload_len(0), timestamp(0), frame_id(0) {}

        FrameHeader(FrameType type, uint32_t len, TimeNs ts, FrameId id)
            : frame_type(type), reserved{0, 0, 0}, payload_len(len), timestamp(ts), frame_id(id) {}
    };

    static_assert(sizeof(FrameHeader) == 24, "FrameHeader must be 24 bytes");

    /// Frame structure containing header and payload
    struct Frame {
        FrameHeader header; ///< Frame header
        Bytes payload;      ///< Frame payload data

        Frame() = default;

        Frame(FrameType type, const Bytes &data, TimeNs ts = now_ns(), FrameId id = 0)
            : header(type, static_cast<uint32_t>(data.size()), ts, id), payload(data) {}

        Frame(FrameType type, Bytes &&data, TimeNs ts = now_ns(), FrameId id = 0)
            : header(type, static_cast<uint32_t>(data.size()), ts, id), payload(std::move(data)) {}

        /// Get total frame size (header + payload)
        inline size_t total_size() const { return sizeof(FrameHeader) + payload.size(); }

        /// Check if frame is valid
        inline bool is_valid() const { return header.payload_len == payload.size(); }
    };

    /// Serialize frame to bytes (header + payload)
    inline Bytes serialize_frame(const Frame &frame) {
        Bytes result;
        result.reserve(sizeof(FrameHeader) + frame.payload.size());

        // Serialize header
        const auto *header_bytes = reinterpret_cast<const Byte *>(&frame.header);
        result.insert(result.end(), header_bytes, header_bytes + sizeof(FrameHeader));

        // Append payload
        result.insert(result.end(), frame.payload.begin(), frame.payload.end());

        return result;
    }

    /// Deserialize frame from bytes
    inline Result<Frame, Error> deserialize_frame(const Bytes &data) {
        if (data.size() < sizeof(FrameHeader)) {
            return Result<Frame, Error>::err(Error::invalid_argument("Data too small for frame header"));
        }

        Frame frame;

        // Deserialize header
        std::memcpy(&frame.header, data.data(), sizeof(FrameHeader));

        // Check if we have enough data for payload
        if (data.size() < sizeof(FrameHeader) + frame.header.payload_len) {
            return Result<Frame, Error>::err(Error::invalid_argument("Data too small for frame payload"));
        }

        // Extract payload
        frame.payload.assign(data.begin() + sizeof(FrameHeader),
                             data.begin() + sizeof(FrameHeader) + frame.header.payload_len);

        if (!frame.is_valid()) {
            return Result<Frame, Error>::err(Error::invalid_argument("Invalid frame"));
        }

        return Result<Frame, Error>::ok(std::move(frame));
    }

} // namespace wirebit
