#pragma once

#include <datapod/lockfree/ring_buffer.hpp>
#include <echo/echo.hpp>
#include <wirebit/frame.hpp>

namespace wirebit {

    /// Wrapper around datapod::RingBuffer for frame encoding/decoding
    /// This class provides frame-level operations on top of the byte-level SPSC ring buffer
    /// Frame format: [u32 record_len][FrameHeader][payload][padding to 8B alignment]
    class FrameRing {
      public:
        /// Create a new frame ring with specified capacity
        /// @param capacity_bytes Total capacity in bytes
        static Result<FrameRing, Error> create(size_t capacity_bytes) {
            echo::debug("Creating FrameRing with capacity: ", capacity_bytes, " bytes");
            return Result<FrameRing, Error>::ok(FrameRing(capacity_bytes));
        }

        /// Create a new frame ring in shared memory
        /// @param shm_name Shared memory name (must start with '/')
        /// @param capacity_bytes Total capacity in bytes
        static Result<FrameRing, Error> create_shm(const String &shm_name, size_t capacity_bytes) {
            echo::debug("Creating FrameRing in SHM: ", shm_name.c_str(), " (capacity: ", capacity_bytes, " bytes)");

            auto ring_result = datapod::RingBuffer<datapod::SPSC, Byte>::create_shm(shm_name, capacity_bytes);
            if (!ring_result.is_ok()) {
                echo::error("Failed to create SHM ring: ", ring_result.error().message.c_str()).red();
                return Result<FrameRing, Error>::err(ring_result.error());
            }

            echo::debug("FrameRing SHM created successfully").green();
            return Result<FrameRing, Error>::ok(FrameRing(std::move(ring_result.value())));
        }

        /// Attach to an existing frame ring in shared memory
        /// @param shm_name Shared memory name (must start with '/')
        static Result<FrameRing, Error> attach_shm(const String &shm_name) {
            echo::debug("Attaching to FrameRing SHM: ", shm_name.c_str());

            auto ring_result = datapod::RingBuffer<datapod::SPSC, Byte>::attach_shm(shm_name);
            if (!ring_result.is_ok()) {
                echo::error("Failed to attach to SHM ring: ", ring_result.error().message.c_str()).red();
                return Result<FrameRing, Error>::err(ring_result.error());
            }

            echo::debug("FrameRing SHM attached successfully").green();
            return Result<FrameRing, Error>::ok(FrameRing(std::move(ring_result.value())));
        }

        /// Push a frame into the ring buffer
        /// Frame format: [u32 record_len][FrameHeader][payload][padding to 8B alignment]
        /// @param frame Frame to push
        /// @return Result indicating success or error
        Result<Unit, Error> push_frame(const Frame &frame) {
            echo::trace("FrameRing::push_frame: payload_size=", frame.payload.size());

            // Calculate total record size (header + payload + padding)
            size_t payload_size = frame.payload.size();
            size_t record_size = sizeof(uint32_t) + sizeof(FrameHeader) + payload_size;
            size_t aligned_size = (record_size + 7) & ~7; // Align to 8 bytes
            size_t padding = aligned_size - record_size;

            echo::trace("Record size: ", record_size, " bytes, aligned: ", aligned_size, " bytes, padding: ", padding);

            // Check if we have enough space
            size_t available = capacity() - size();
            if (available < aligned_size) {
                echo::warn("FrameRing full: need ", aligned_size, " bytes, have ", available).yellow();
                return Result<Unit, Error>::err(Error::timeout("Ring buffer full"));
            }

            // Check if ring is >80% full
            float usage = static_cast<float>(size()) / static_cast<float>(capacity());
            if (usage > 0.8f) {
                echo::warn("FrameRing usage: ", static_cast<int>(usage * 100), "%").yellow();
            }

            // Write record length
            uint32_t record_len = static_cast<uint32_t>(aligned_size);
            auto result = push_bytes(reinterpret_cast<const Byte *>(&record_len), sizeof(uint32_t));
            if (!result.is_ok()) {
                return result;
            }

            // Write frame header
            result = push_bytes(reinterpret_cast<const Byte *>(&frame.header), sizeof(FrameHeader));
            if (!result.is_ok()) {
                echo::error("Failed to push frame header").red();
                return result;
            }

            // Write payload
            if (!frame.payload.empty()) {
                result = push_bytes(frame.payload.data(), frame.payload.size());
                if (!result.is_ok()) {
                    echo::error("Failed to push frame payload").red();
                    return result;
                }
            }

            // Write padding
            for (size_t i = 0; i < padding; ++i) {
                auto pad_result = ring_.push(static_cast<Byte>(0));
                if (!pad_result.is_ok()) {
                    echo::error("Failed to push padding").red();
                    return Result<Unit, Error>::err(pad_result.error());
                }
            }

            echo::trace("FrameRing::push_frame complete");
            return Result<Unit, Error>::ok(Unit{});
        }

        /// Pop a frame from the ring buffer
        /// @return Result containing frame if available, or error
        Result<Frame, Error> pop_frame() {
            echo::trace("FrameRing::pop_frame");

            // Read record length
            uint32_t record_len = 0;
            auto result = pop_bytes(reinterpret_cast<Byte *>(&record_len), sizeof(uint32_t));
            if (!result.is_ok()) {
                return Result<Frame, Error>::err(result.error());
            }

            echo::trace("Record length: ", record_len, " bytes");

            // Validate record length
            if (record_len == 0) {
                echo::error("Invalid record length: 0").red();
                return Result<Frame, Error>::err(Error::invalid_argument("Invalid record length"));
            }

            if (record_len > capacity()) {
                echo::error("Record length ", record_len, " exceeds capacity ", capacity()).red();
                return Result<Frame, Error>::err(Error::invalid_argument("Record too large"));
            }

            // Read frame header
            Frame frame;
            result = pop_bytes(reinterpret_cast<Byte *>(&frame.header), sizeof(FrameHeader));
            if (!result.is_ok()) {
                echo::error("Failed to pop frame header").red();
                return Result<Frame, Error>::err(result.error());
            }

            // Read payload
            size_t payload_len = frame.header.payload_len;
            if (payload_len > 0) {
                frame.payload.resize(payload_len);
                result = pop_bytes(frame.payload.data(), payload_len);
                if (!result.is_ok()) {
                    echo::error("Failed to pop frame payload").red();
                    return Result<Frame, Error>::err(result.error());
                }
            }

            // Calculate and skip padding
            size_t record_size = sizeof(uint32_t) + sizeof(FrameHeader) + payload_len;
            size_t aligned_size = (record_size + 7) & ~7;
            size_t padding = aligned_size - record_size;

            for (size_t i = 0; i < padding; ++i) {
                auto pad_result = ring_.pop();
                if (!pad_result.is_ok()) {
                    echo::error("Failed to pop padding").red();
                    return Result<Frame, Error>::err(pad_result.error());
                }
            }

            // Validate frame
            if (!frame.is_valid()) {
                echo::error("Invalid frame: payload_len mismatch").red();
                return Result<Frame, Error>::err(Error::invalid_argument("Invalid frame"));
            }

            echo::trace("FrameRing::pop_frame complete: payload_size=", frame.payload.size());
            return Result<Frame, Error>::ok(std::move(frame));
        }

        /// Check if ring buffer is empty
        inline bool empty() const { return ring_.empty(); }

        /// Check if ring buffer is full
        inline bool full() const { return ring_.full(); }

        /// Get capacity in bytes
        inline size_t capacity() const { return ring_.capacity(); }

        /// Get current size in bytes
        inline size_t size() const { return ring_.size(); }

        /// Get available space in bytes
        inline size_t available() const { return capacity() - size(); }

        /// Get usage percentage (0.0 to 1.0)
        inline float usage() const { return static_cast<float>(size()) / static_cast<float>(capacity()); }

      private:
        datapod::RingBuffer<datapod::SPSC, Byte> ring_;

        explicit FrameRing(size_t capacity) : ring_(capacity) {}

        explicit FrameRing(datapod::RingBuffer<datapod::SPSC, Byte> &&ring) : ring_(std::move(ring)) {}

        /// Helper: Push multiple bytes to the ring buffer
        inline Result<Unit, Error> push_bytes(const Byte *data, size_t len) {
            for (size_t i = 0; i < len; ++i) {
                auto result = ring_.push(data[i]);
                if (!result.is_ok()) {
                    return Result<Unit, Error>::err(result.error());
                }
            }
            return Result<Unit, Error>::ok(Unit{});
        }

        /// Helper: Pop multiple bytes from the ring buffer
        inline Result<Unit, Error> pop_bytes(Byte *data, size_t len) {
            for (size_t i = 0; i < len; ++i) {
                auto result = ring_.pop();
                if (!result.is_ok()) {
                    return Result<Unit, Error>::err(result.error());
                }
                data[i] = result.value();
            }
            return Result<Unit, Error>::ok(Unit{});
        }
    };

} // namespace wirebit
