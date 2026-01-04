#pragma once

#include <datapod/temporal/stamp.hpp>
#include <wirebit/common/types.hpp>

namespace wirebit {

    /// Frame type enumeration
    enum class FrameType : uint8_t {
        SERIAL = 0, ///< Serial data frame
        CAN = 1,    ///< CAN bus frame
        ETH = 2,    ///< Ethernet frame
    };

    /// Frame data structure (the value part of Stamp<FrameData>)
    struct FrameData {
        FrameType frame_type; ///< Type of frame
        FrameId frame_id;     ///< Unique frame identifier
        Bytes payload;        ///< Frame payload data

        FrameData() : frame_type(FrameType::SERIAL), frame_id(0), payload() {}

        FrameData(FrameType type, FrameId id, const Bytes &data) : frame_type(type), frame_id(id), payload(data) {}

        FrameData(FrameType type, FrameId id, Bytes &&data)
            : frame_type(type), frame_id(id), payload(std::move(data)) {}

        /// Get payload size
        inline size_t size() const { return payload.size(); }

        /// Check if payload is empty
        inline bool empty() const { return payload.empty(); }

        /// Reflection support for serialization
        auto members() { return std::tie(frame_type, frame_id, payload); }
        auto members() const { return std::tie(frame_type, frame_id, payload); }
    };

    /// Frame = timestamped frame data (uses datapod::Stamp)
    using Frame = datapod::Stamp<FrameData>;

    /// Helper: Create a frame with current timestamp
    inline Frame make_frame(FrameType type, const Bytes &payload, FrameId id = 0) {
        return Frame{Frame::now(), FrameData{type, id, payload}};
    }

    /// Helper: Create a frame with current timestamp (move payload)
    inline Frame make_frame(FrameType type, Bytes &&payload, FrameId id = 0) {
        return Frame{Frame::now(), FrameData{type, id, std::move(payload)}};
    }

    /// Helper: Create a frame with explicit timestamp
    inline Frame make_frame(FrameType type, const Bytes &payload, TimeNs timestamp, FrameId id = 0) {
        return Frame{timestamp, FrameData{type, id, payload}};
    }

    /// Serialize frame to bytes
    /// Format: [int64_t timestamp][uint8_t frame_type][uint64_t frame_id][uint32_t payload_len][payload bytes]
    inline Bytes serialize_frame(const Frame &frame) {
        Bytes result;
        size_t total_size = sizeof(int64_t) +           // timestamp
                            sizeof(uint8_t) +           // frame_type
                            sizeof(uint64_t) +          // frame_id
                            sizeof(uint32_t) +          // payload_len
                            frame.value.payload.size(); // payload
        result.reserve(total_size);

        // Serialize timestamp
        const auto *ts_bytes = reinterpret_cast<const Byte *>(&frame.timestamp);
        result.insert(result.end(), ts_bytes, ts_bytes + sizeof(int64_t));

        // Serialize frame_type
        result.push_back(static_cast<Byte>(frame.value.frame_type));

        // Serialize frame_id
        const auto *id_bytes = reinterpret_cast<const Byte *>(&frame.value.frame_id);
        result.insert(result.end(), id_bytes, id_bytes + sizeof(uint64_t));

        // Serialize payload_len
        uint32_t payload_len = static_cast<uint32_t>(frame.value.payload.size());
        const auto *len_bytes = reinterpret_cast<const Byte *>(&payload_len);
        result.insert(result.end(), len_bytes, len_bytes + sizeof(uint32_t));

        // Serialize payload
        result.insert(result.end(), frame.value.payload.begin(), frame.value.payload.end());

        return result;
    }

    /// Deserialize frame from bytes
    inline Result<Frame, Error> deserialize_frame(const Bytes &data) {
        size_t min_size = sizeof(int64_t) + sizeof(uint8_t) + sizeof(uint64_t) + sizeof(uint32_t);
        if (data.size() < min_size) {
            return Result<Frame, Error>::err(Error::invalid_argument("Data too small for frame"));
        }

        size_t offset = 0;

        // Deserialize timestamp
        int64_t timestamp;
        std::memcpy(&timestamp, data.data() + offset, sizeof(int64_t));
        offset += sizeof(int64_t);

        // Deserialize frame_type
        FrameType frame_type = static_cast<FrameType>(data[offset]);
        offset += sizeof(uint8_t);

        // Deserialize frame_id
        FrameId frame_id;
        std::memcpy(&frame_id, data.data() + offset, sizeof(uint64_t));
        offset += sizeof(uint64_t);

        // Deserialize payload_len
        uint32_t payload_len;
        std::memcpy(&payload_len, data.data() + offset, sizeof(uint32_t));
        offset += sizeof(uint32_t);

        // Check if we have enough data for payload
        if (data.size() < offset + payload_len) {
            return Result<Frame, Error>::err(Error::invalid_argument("Data too small for frame payload"));
        }

        // Deserialize payload
        Bytes payload(data.begin() + offset, data.begin() + offset + payload_len);

        // Create frame
        Frame frame{timestamp, FrameData{frame_type, frame_id, std::move(payload)}};

        return Result<Frame, Error>::ok(std::move(frame));
    }

} // namespace wirebit
