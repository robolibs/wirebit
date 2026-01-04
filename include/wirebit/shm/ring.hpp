#pragma once

#include <datapod/lockfree/ring_buffer.hpp>
#include <wirebit/frame.hpp>

namespace wirebit {

    /// Wrapper around datapod::RingBuffer for frame encoding/decoding
    /// This class provides frame-level operations on top of the byte-level SPSC ring buffer
    /// TODO: Full implementation in wirebit-p3e.2
    class FrameRing {
      public:
        /// Create a new frame ring with specified capacity
        /// @param capacity_bytes Total capacity in bytes
        static Result<FrameRing, Error> create(size_t capacity_bytes) {
            return Result<FrameRing, Error>::ok(FrameRing(capacity_bytes));
        }

        /// Create a new frame ring in shared memory
        /// @param shm_name Shared memory name
        /// @param capacity_bytes Total capacity in bytes
        static Result<FrameRing, Error> create_shm(const String &shm_name, size_t capacity_bytes) {
            auto ring_result = datapod::RingBuffer<datapod::SPSC, Byte>::create_shm(shm_name, capacity_bytes);
            if (!ring_result.is_ok()) {
                return Result<FrameRing, Error>::err(ring_result.error());
            }
            return Result<FrameRing, Error>::ok(FrameRing(std::move(ring_result.value())));
        }

        /// Attach to an existing frame ring in shared memory
        /// @param shm_name Shared memory name
        static Result<FrameRing, Error> attach_shm(const String &shm_name) {
            auto ring_result = datapod::RingBuffer<datapod::SPSC, Byte>::attach_shm(shm_name);
            if (!ring_result.is_ok()) {
                return Result<FrameRing, Error>::err(ring_result.error());
            }
            return Result<FrameRing, Error>::ok(FrameRing(std::move(ring_result.value())));
        }

        /// Push a frame into the ring buffer
        /// @param frame Frame to push
        /// @return Result indicating success or error
        Result<Unit, Error> push_frame(const Frame &frame) {
            // TODO: Implement frame encoding and batch push
            (void)frame;
            return Result<Unit, Error>::err(Error{100, "FrameRing::push_frame not yet implemented"});
        }

        /// Pop a frame from the ring buffer
        /// @return Result containing frame if available, or error
        Result<Frame, Error> pop_frame() {
            // TODO: Implement frame decoding and batch pop
            return Result<Frame, Error>::err(Error{100, "FrameRing::pop_frame not yet implemented"});
        }

        /// Check if ring buffer is empty
        bool empty() const { return ring_.empty(); }

        /// Check if ring buffer is full
        bool full() const { return ring_.full(); }

        /// Get capacity in bytes
        size_t capacity() const { return ring_.capacity(); }

        /// Get current size in bytes
        size_t size() const { return ring_.size(); }

      private:
        datapod::RingBuffer<datapod::SPSC, Byte> ring_;

        explicit FrameRing(size_t capacity) : ring_(capacity) {}

        explicit FrameRing(datapod::RingBuffer<datapod::SPSC, Byte> &&ring) : ring_(std::move(ring)) {}
    };

} // namespace wirebit
