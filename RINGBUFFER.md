# RingBuffer<Policy, T> Implementation Specification

## Overview

Implement a **policy-based lock-free ring buffer** for datapod using template specialization.

**Target**: `datapod/include/datapod/lockfree/ring_buffer.hpp`

**Usage**: `datapod::RingBuffer<datapod::SPSC, int> ring(1024);`

---

## Design Philosophy

- **Policy-based design**: `RingBuffer<Policy, T>` where Policy is a tag type
- **Template specialization**: Each policy gets its own specialized implementation
- **No type aliases**: Users must write `RingBuffer<SPSC, T>` explicitly
- **Lock-free**: Uses atomic operations, no mutexes
- **Zero-copy**: Direct memory access in shared memory
- **POD-compatible**: Works with trivially copyable types

---

## File Structure

```
datapod/include/datapod/lockfree/
├── ring_buffer.hpp         # All code here (header-only)
└── README.md              # Documentation
```

---

## Complete Header Template

```cpp
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <tuple>
#include <type_traits>
#include <utility>

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <datapod/adapters/error.hpp>
#include <datapod/adapters/result.hpp>
#include <datapod/sequential/string.hpp>

namespace datapod {

// ============================================================================
// POLICY TAGS
// ============================================================================

/**
 * @brief Single Producer Single Consumer policy
 * 
 * Lock-free ring buffer for exactly one writer thread/process and one reader.
 * Uses simple atomic operations with acquire/release memory ordering.
 * 
 * Guarantees:
 * - Lock-free (no mutexes, no syscalls in hot path)
 * - Wait-free for producer when not full
 * - Wait-free for consumer when not empty
 * - ~10-50ns latency for push/pop
 * 
 * Constraints:
 * - MUST have exactly ONE producer thread/process
 * - MUST have exactly ONE consumer thread/process
 * - Violating this causes undefined behavior
 */
struct SPSC {};

/**
 * @brief Multi Producer Multi Consumer policy (FUTURE - not implemented yet)
 * 
 * Lock-free ring buffer for multiple writers and readers.
 * Uses CAS (Compare-And-Swap) operations with sequential consistency.
 */
struct MPMC {};

/**
 * @brief Single Producer Multi Consumer policy (FUTURE - not implemented yet)
 * 
 * Lock-free ring buffer for one writer and multiple readers.
 */
struct SPMC {};

// ============================================================================
// PRIMARY TEMPLATE (UNDEFINED - forces specialization)
// ============================================================================

/**
 * @brief Lock-free ring buffer with policy-based design
 * 
 * Primary template is intentionally undefined. You must use a specialized
 * policy (SPSC, MPMC, etc.).
 * 
 * @tparam Policy Concurrency policy (SPSC, MPMC, SPMC)
 * @tparam T Element type (must be trivially copyable for shared memory)
 * 
 * Example:
 * @code
 * RingBuffer<SPSC, int> ring(1024);  // SPSC ring buffer
 * @endcode
 */
template <typename Policy, typename T>
class RingBuffer;

// ============================================================================
// SPSC SPECIALIZATION
// ============================================================================

/**
 * @brief Lock-free SPSC (Single Producer Single Consumer) ring buffer
 * 
 * High-performance lock-free ring buffer for IPC and concurrent programming.
 * Supports both in-memory and shared-memory modes.
 * 
 * Features:
 * - Lock-free (no mutexes)
 * - Cache-line aligned to prevent false sharing
 * - Proper memory ordering (acquire/release semantics)
 * - Shared memory support for IPC
 * - ~10-50ns latency for push/pop
 * 
 * Memory Layout (in shared memory):
 * @code
 * [Header: 128 bytes, cache-line aligned]
 *   - write_pos: atomic<uint64_t> (64 bytes with padding)
 *   - read_pos:  atomic<uint64_t> (64 bytes with padding)
 *   - capacity:  uint64_t
 *   - magic:     uint32_t (0x53505343 = 'SPSC')
 *   - version:   uint32_t
 * [Buffer: capacity * sizeof(T)]
 * @endcode
 * 
 * Thread Safety:
 * - Producer (push) can be called from ONE thread/process only
 * - Consumer (pop) can be called from ONE thread/process only
 * - Query methods (empty, full, size) are safe from both sides
 * 
 * Example (in-memory):
 * @code
 * RingBuffer<SPSC, int> ring(1024);
 * 
 * // Producer thread
 * auto result = ring.push(42);
 * if (!result.is_ok()) {
 *     // Ring full
 * }
 * 
 * // Consumer thread
 * auto result = ring.pop();
 * if (result.is_ok()) {
 *     int value = result.value();
 * }
 * @endcode
 * 
 * Example (shared memory):
 * @code
 * // Process A (creator)
 * auto ring_result = RingBuffer<SPSC, int>::create_shm("/my_ring", 1024);
 * if (ring_result.is_ok()) {
 *     auto& ring = ring_result.value();
 *     ring.push(42);
 * }
 * 
 * // Process B (attacher)
 * auto ring_result = RingBuffer<SPSC, int>::attach_shm("/my_ring");
 * if (ring_result.is_ok()) {
 *     auto& ring = ring_result.value();
 *     auto value = ring.pop();
 * }
 * @endcode
 * 
 * @tparam T Element type (must be trivially copyable for shared memory mode)
 */
template <typename T>
class RingBuffer<SPSC, T> {
    static_assert(std::is_trivially_copyable_v<T>, 
                  "T must be trivially copyable for RingBuffer");

public:
    // ========================================================================
    // CONSTRUCTION & DESTRUCTION
    // ========================================================================
    
    /**
     * @brief Create in-memory ring buffer
     * 
     * Allocates memory on the heap for the ring buffer.
     * Use this for single-process, multi-threaded scenarios.
     * 
     * @param capacity Number of elements (must be > 0)
     * 
     * @note Capacity should ideally be a power of 2 for best performance,
     *       but this is not required.
     */
    explicit RingBuffer(size_t capacity);
    
    /**
     * @brief Create shared memory ring buffer
     * 
     * Creates a new shared memory region and initializes the ring buffer.
     * Use this for multi-process IPC.
     * 
     * @param name Shared memory name (e.g., "/my_ring")
     *             Must start with '/' and contain only valid filename chars
     * @param capacity Number of elements (must be > 0)
     * @return Result with RingBuffer or Error
     * 
     * Errors:
     * - Error::ALREADY_EXISTS if shared memory already exists
     * - Error::IO_ERROR if shm_open, ftruncate, or mmap fails
     * - Error::INVALID_ARGUMENT if name is invalid or capacity is 0
     * 
     * Example:
     * @code
     * auto result = RingBuffer<SPSC, int>::create_shm("/my_ring", 1024);
     * if (!result.is_ok()) {
     *     echo::error("Failed to create ring: ", result.error().message.c_str());
     *     return;
     * }
     * auto& ring = result.value();
     * @endcode
     */
    static Result<RingBuffer, Error> create_shm(const String& name, size_t capacity);
    
    /**
     * @brief Attach to existing shared memory ring buffer
     * 
     * Opens an existing shared memory region created by create_shm().
     * 
     * @param name Shared memory name (must match create_shm name)
     * @return Result with RingBuffer or Error
     * 
     * Errors:
     * - Error::NOT_FOUND if shared memory doesn't exist
     * - Error::IO_ERROR if shm_open or mmap fails
     * - Error::INVALID_ARGUMENT if magic number doesn't match
     * 
     * Example:
     * @code
     * auto result = RingBuffer<SPSC, int>::attach_shm("/my_ring");
     * if (!result.is_ok()) {
     *     echo::error("Failed to attach: ", result.error().message.c_str());
     *     return;
     * }
     * auto& ring = result.value();
     * @endcode
     */
    static Result<RingBuffer, Error> attach_shm(const String& name);
    
    /**
     * @brief Destructor
     * 
     * Cleans up resources:
     * - In-memory mode: frees allocated memory
     * - Shared memory mode (creator): unmaps and unlinks shared memory
     * - Shared memory mode (attacher): only unmaps (doesn't unlink)
     */
    ~RingBuffer();
    
    // Move-only (no copy)
    RingBuffer(RingBuffer&& other) noexcept;
    RingBuffer& operator=(RingBuffer&& other) noexcept;
    RingBuffer(const RingBuffer&) = delete;
    RingBuffer& operator=(const RingBuffer&) = delete;
    
    // ========================================================================
    // PRODUCER API (call from ONE thread/process only)
    // ========================================================================
    
    /**
     * @brief Push element (non-blocking)
     * 
     * Attempts to push an element into the ring buffer.
     * Returns immediately if the ring is full.
     * 
     * @param item Element to push
     * @return Result::ok() if pushed, Error::TIMEOUT if full
     * 
     * Thread Safety: MUST be called from producer thread/process only
     * 
     * Memory Ordering:
     * 1. Write item to buffer[write_pos % capacity]
     * 2. Release fence (ensures write completes)
     * 3. Update write_pos with memory_order_release
     * 
     * Example:
     * @code
     * auto result = ring.push(42);
     * if (!result.is_ok()) {
     *     echo::warn("Ring full, dropping data");
     * }
     * @endcode
     */
    inline Result<void, Error> push(const T& item);
    
    /**
     * @brief Push element (move)
     * 
     * Same as push(const T&) but moves the item.
     * 
     * @param item Element to push (will be moved)
     * @return Result::ok() if pushed, Error::TIMEOUT if full
     */
    inline Result<void, Error> push(T&& item);
    
    /**
     * @brief Emplace element
     * 
     * Constructs element in-place in the ring buffer.
     * 
     * @param args Constructor arguments for T
     * @return Result::ok() if emplaced, Error::TIMEOUT if full
     * 
     * Example:
     * @code
     * struct Point { int x, y; };
     * RingBuffer<SPSC, Point> ring(1024);
     * ring.emplace(10, 20);  // Constructs Point{10, 20} in-place
     * @endcode
     */
    template <typename... Args>
    inline Result<void, Error> emplace(Args&&... args);
    
    // ========================================================================
    // CONSUMER API (call from ONE thread/process only)
    // ========================================================================
    
    /**
     * @brief Pop element (non-blocking)
     * 
     * Attempts to pop an element from the ring buffer.
     * Returns immediately if the ring is empty.
     * 
     * @return Result with element or Error::TIMEOUT if empty
     * 
     * Thread Safety: MUST be called from consumer thread/process only
     * 
     * Memory Ordering:
     * 1. Load write_pos with memory_order_acquire
     * 2. Read item from buffer[read_pos % capacity]
     * 3. Update read_pos with memory_order_release
     * 
     * Example:
     * @code
     * auto result = ring.pop();
     * if (result.is_ok()) {
     *     int value = result.value();
     *     process(value);
     * } else {
     *     echo::trace("Ring empty");
     * }
     * @endcode
     */
    inline Result<T, Error> pop();
    
    /**
     * @brief Peek at front element without removing
     * 
     * Returns a reference to the front element without removing it.
     * The reference is valid until the next pop() call.
     * 
     * @return Result with const reference or Error::TIMEOUT if empty
     * 
     * Thread Safety: MUST be called from consumer thread/process only
     * 
     * Example:
     * @code
     * auto result = ring.peek();
     * if (result.is_ok()) {
     *     const int& value = result.value();
     *     if (should_process(value)) {
     *         ring.pop();  // Now remove it
     *     }
     * }
     * @endcode
     */
    inline Result<const T&, Error> peek() const;
    
    // ========================================================================
    // QUERY (safe from both producer and consumer)
    // ========================================================================
    
    /**
     * @brief Check if empty
     * 
     * @return true if no elements in ring
     * 
     * Thread Safety: Safe to call from both producer and consumer
     * 
     * Note: Result may be stale immediately after return in concurrent scenarios.
     *       Use for hints/metrics, not for synchronization.
     */
    inline bool empty() const noexcept;
    
    /**
     * @brief Check if full
     * 
     * @return true if ring is at capacity
     * 
     * Thread Safety: Safe to call from both producer and consumer
     * 
     * Note: Result may be stale immediately after return in concurrent scenarios.
     */
    inline bool full() const noexcept;
    
    /**
     * @brief Get current number of elements
     * 
     * @return Number of elements currently in ring (0 to capacity)
     * 
     * Thread Safety: Safe to call from both producer and consumer
     * 
     * Note: Result may be stale immediately after return in concurrent scenarios.
     *       The value is approximate in concurrent scenarios.
     */
    inline size_t size() const noexcept;
    
    /**
     * @brief Get capacity
     * 
     * @return Maximum number of elements
     * 
     * Thread Safety: Safe to call from anywhere (immutable after construction)
     */
    inline size_t capacity() const noexcept;
    
    // ========================================================================
    // SERIALIZATION (datapod compatibility)
    // ========================================================================
    
    /**
     * @brief Serializable snapshot of ring buffer state
     * 
     * Since std::atomic is not serializable, we provide a snapshot struct
     * that captures the current state in a serializable format.
     * 
     * Note: This is a SNAPSHOT - the actual ring buffer state may change
     *       immediately after this is created in concurrent scenarios.
     * 
     * Use for:
     * - Debugging/logging
     * - Metrics/monitoring
     * - Checkpointing (with care - data in buffer is not included)
     * 
     * NOT for:
     * - Synchronization
     * - Exact state queries in concurrent code
     */
    struct Snapshot {
        uint64_t write_pos;   ///< Producer position (snapshot)
        uint64_t read_pos;    ///< Consumer position (snapshot)
        uint64_t capacity;    ///< Ring capacity
        uint32_t magic;       ///< Magic number (0x53505343 = 'SPSC')
        uint32_t version;     ///< Version number
        
        /// Reflection support for datapod serialization
        auto members() noexcept { 
            return std::tie(write_pos, read_pos, capacity, magic, version); 
        }
        auto members() const noexcept { 
            return std::tie(write_pos, read_pos, capacity, magic, version); 
        }
    };
    
    /**
     * @brief Get serializable snapshot of ring buffer state
     * 
     * Returns a snapshot of the current ring buffer state that can be
     * serialized using datapod's serialization system.
     * 
     * @return Snapshot struct with current state
     * 
     * Thread Safety: Safe to call from any thread
     * 
     * Note: The snapshot may be stale immediately after return in
     *       concurrent scenarios. Use for monitoring/debugging only.
     * 
     * Example:
     * @code
     * auto snap = ring.snapshot();
     * echo::info("Ring state: write=", snap.write_pos, 
     *            " read=", snap.read_pos,
     *            " size=", snap.write_pos - snap.read_pos);
     * 
     * // Can serialize with datapod
     * auto serialized = datapod::serialize(snap);
     * @endcode
     */
    inline Snapshot snapshot() const noexcept {
        return Snapshot{
            header_->write_pos.load(std::memory_order_acquire),
            header_->read_pos.load(std::memory_order_acquire),
            header_->capacity,
            header_->magic,
            header_->version
        };
    }
    
    /**
     * @brief Get members for datapod reflection/serialization
     * 
     * Returns a tuple of serializable members. Since atomics cannot be
     * serialized directly, this returns a snapshot's members.
     * 
     * @return Tuple of (write_pos, read_pos, capacity, magic, version)
     * 
     * Note: Creates a snapshot on each call. The returned tuple references
     *       a temporary snapshot, so use immediately.
     * 
     * Example:
     * @code
     * // Datapod serialization will call this automatically
     * auto data = datapod::serialize(ring);
     * @endcode
     */
    inline auto members() const noexcept {
        // Create snapshot and return its members
        // Note: This is safe because we're returning a tuple of values (copies)
        auto snap = snapshot();
        return std::make_tuple(
            snap.write_pos, 
            snap.read_pos, 
            snap.capacity, 
            snap.magic, 
            snap.version
        );
    }
    
    /**
     * @brief Serializable snapshot with ring buffer data
     * 
     * Captures both metadata AND all valid elements in the ring buffer.
     * Unlike Snapshot (metadata only), this includes a copy of all data
     * currently in the ring (from read_pos to write_pos).
     * 
     * Use cases:
     * - Forwarding data to another process/network
     * - Checkpointing ring buffer state to disk
     * - Debugging (capture exact state at error)
     * - Replay (send captured data to test systems)
     * - Migration (move data between shared memory regions)
     * 
     * Performance:
     * - Time: O(n) where n = number of elements in ring
     * - Space: O(n) - allocates Vector<T> and copies all elements
     * 
     * Thread Safety:
     * - Safe to call from any thread
     * - Snapshot may be slightly stale in concurrent scenarios
     * - Captures consistent state (all elements between read_pos and write_pos)
     * 
     * Note: Only valid elements are included (garbage/unused slots are skipped)
     * 
     * @tparam T Element type (same as ring buffer)
     */
    struct SnapshotWithData {
        uint64_t write_pos;   ///< Producer position (snapshot)
        uint64_t read_pos;    ///< Consumer position (snapshot)
        uint64_t capacity;    ///< Ring capacity
        uint32_t magic;       ///< Magic number (0x53505343 = 'SPSC')
        uint32_t version;     ///< Version number
        datapod::Vector<T> data;  ///< Valid elements (read_pos to write_pos)
        
        /// Reflection support for datapod serialization
        auto members() noexcept { 
            return std::tie(write_pos, read_pos, capacity, magic, version, data); 
        }
        auto members() const noexcept { 
            return std::tie(write_pos, read_pos, capacity, magic, version, data); 
        }
    };
    
    /**
     * @brief Get snapshot with all valid data
     * 
     * Captures current ring buffer state including all valid elements.
     * This is a heavyweight operation that copies all data.
     * 
     * @return SnapshotWithData containing metadata and all valid elements
     * 
     * Thread Safety: Safe to call from any thread
     * 
     * Performance: O(n) time and space where n = number of elements
     * 
     * Note: In concurrent scenarios, snapshot may not include elements
     *       written after the initial position load. This is acceptable
     *       for debugging, monitoring, and checkpointing use cases.
     * 
     * Example:
     * @code
     * RingBuffer<SPSC, Frame> ring(1024);
     * // ... producer fills ring ...
     * 
     * // Capture snapshot with data
     * auto snap = ring.snapshot_with_data();
     * echo::info("Captured ", snap.data.size(), " frames");
     * 
     * // Serialize and forward to another process
     * auto serialized = datapod::serialize(snap);
     * send_to_network(serialized);
     * 
     * // Or save to disk for checkpointing
     * write_to_file("checkpoint.bin", serialized);
     * @endcode
     */
    inline SnapshotWithData snapshot_with_data() const {
        // Load positions atomically
        uint64_t r = header_->read_pos.load(std::memory_order_acquire);
        uint64_t w = header_->write_pos.load(std::memory_order_acquire);
        
        SnapshotWithData snap;
        snap.write_pos = w;
        snap.read_pos = r;
        snap.capacity = header_->capacity;
        snap.magic = header_->magic;
        snap.version = header_->version;
        
        // Copy all valid elements (from read_pos to write_pos)
        size_t count = w - r;
        snap.data.reserve(count);
        
        for (uint64_t pos = r; pos < w; pos++) {
            size_t idx = pos % header_->capacity;
            snap.data.push_back(buffer_[idx]);
        }
        
        return snap;
    }
    
    /**
     * @brief Create ring buffer from snapshot
     * 
     * Restores a ring buffer from a previously captured snapshot.
     * Creates a new ring buffer with the same capacity and pushes
     * all data from the snapshot.
     * 
     * @param snap Snapshot to restore from
     * @return Result with new RingBuffer or Error
     * 
     * Use cases:
     * - Restore from checkpoint
     * - Deserialize ring buffer sent over network
     * - Replay captured data
     * 
     * Example:
     * @code
     * // Save snapshot
     * auto snap = ring.snapshot_with_data();
     * auto data = datapod::serialize(snap);
     * write_to_file("checkpoint.bin", data);
     * 
     * // Later: restore from snapshot
     * auto data = read_from_file("checkpoint.bin");
     * auto snap = datapod::deserialize<SnapshotWithData<Frame>>(data);
     * auto result = RingBuffer<SPSC, Frame>::from_snapshot(snap);
     * 
     * if (result.is_ok()) {
     *     auto& restored_ring = result.value();
     *     echo::info("Restored ring with ", restored_ring.size(), " elements");
     * }
     * @endcode
     */
    static Result<RingBuffer, Error> from_snapshot(const SnapshotWithData& snap) {
        // Validate snapshot
        if (snap.magic != 0x53505343) {
            return Result<RingBuffer, Error>::err(
                Error::invalid_argument("Invalid snapshot magic number")
            );
        }
        
        if (snap.capacity == 0) {
            return Result<RingBuffer, Error>::err(
                Error::invalid_argument("Invalid snapshot capacity")
            );
        }
        
        // Create new ring buffer with same capacity
        RingBuffer ring(snap.capacity);
        
        // Push all data from snapshot
        for (const auto& item : snap.data) {
            auto result = ring.push(item);
            if (!result.is_ok()) {
                return Result<RingBuffer, Error>::err(
                    Error::io_error("Failed to restore snapshot: ring buffer full")
                );
            }
        }
        
        return Result<RingBuffer, Error>::ok(std::move(ring));
    }
    
    /**
     * @brief Drain all elements from ring buffer
     * 
     * Pops all elements from the ring buffer into a vector.
     * This is a DESTRUCTIVE operation - the ring buffer will be empty after.
     * 
     * @return Vector containing all elements that were in the ring
     * 
     * Thread Safety: MUST be called from consumer thread only
     * 
     * Use cases:
     * - Batch processing (drain all, process batch, repeat)
     * - Shutdown (drain remaining data before closing)
     * - Migration (drain from one ring, push to another)
     * 
     * Example:
     * @code
     * RingBuffer<SPSC, Frame> ring(1024);
     * // ... producer fills ring ...
     * 
     * // Drain all data for batch processing
     * auto all_frames = ring.drain();
     * echo::info("Drained ", all_frames.size(), " frames");
     * 
     * // Ring is now empty
     * assert(ring.empty());
     * 
     * // Process batch
     * process_batch(all_frames);
     * @endcode
     */
    inline datapod::Vector<T> drain() {
        datapod::Vector<T> result;
        
        // Pop all elements until empty
        while (true) {
            auto item = pop();
            if (!item.is_ok()) {
                break;  // Ring is empty
            }
            result.push_back(std::move(item.value()));
        }
        
        return result;
    }



private:
    // ========================================================================
    // INTERNAL STRUCTURES
    // ========================================================================
    
    /**
     * @brief Ring buffer header (in shared memory)
     * 
     * Layout is carefully designed to prevent false sharing:
     * - write_pos on its own cache line (64 bytes)
     * - read_pos on its own cache line (64 bytes)
     * 
     * This ensures producer and consumer don't invalidate each other's cache.
     */
    struct alignas(64) Header {
        // Producer's cache line (64 bytes)
        std::atomic<uint64_t> write_pos;
        uint8_t padding1[64 - sizeof(std::atomic<uint64_t>)];
        
        // Consumer's cache line (64 bytes)
        std::atomic<uint64_t> read_pos;
        uint8_t padding2[64 - sizeof(std::atomic<uint64_t>)];
        
        // Metadata (shared, rarely accessed)
        uint64_t capacity;
        uint32_t magic;    // 0x53505343 = 'SPSC'
        uint32_t version;  // 1
        
        Header() : write_pos(0), read_pos(0), capacity(0), magic(0x53505343), version(1) {}
    };
    
    // ========================================================================
    // MEMBER VARIABLES
    // ========================================================================
    
    Header* header_;        ///< Pointer to header (in heap or shared memory)
    T* buffer_;             ///< Pointer to buffer (follows header)
    bool owns_memory_;      ///< true if we created the shm (should unlink on destroy)
    bool is_shm_;           ///< true if using shared memory, false if heap
    int shm_fd_;            ///< File descriptor for shared memory (-1 if not shm)
    size_t shm_size_;       ///< Total size of mapped region (0 if not shm)
    String shm_name_;       ///< Shared memory name (empty if not shm)
    
    // ========================================================================
    // INTERNAL HELPERS
    // ========================================================================
    
    /**
     * @brief Calculate total shared memory size
     * 
     * @param capacity Number of elements
     * @return Total bytes needed (header + buffer)
     */
    static inline size_t calculate_shm_size(size_t capacity) noexcept {
        return sizeof(Header) + capacity * sizeof(T);
    }
    
    /**
     * @brief Initialize header
     * 
     * Called by constructor and create_shm.
     * 
     * @param capacity Number of elements
     */
    inline void init_header(size_t capacity) noexcept {
        header_->write_pos.store(0, std::memory_order_relaxed);
        header_->read_pos.store(0, std::memory_order_relaxed);
        header_->capacity = capacity;
        header_->magic = 0x53505343;  // 'SPSC'
        header_->version = 1;
    }
    
    /**
     * @brief Verify header magic and version
     * 
     * Called by attach_shm to validate the shared memory.
     * 
     * @return true if valid
     */
    inline bool verify_header() const noexcept {
        return header_->magic == 0x53505343 && header_->version == 1;
    }
};

// ============================================================================
// IMPLEMENTATION
// ============================================================================

// Constructor (in-memory)
template <typename T>
RingBuffer<SPSC, T>::RingBuffer(size_t capacity)
    : owns_memory_(true)
    , is_shm_(false)
    , shm_fd_(-1)
    , shm_size_(0)
{
    if (capacity == 0) {
        // Handle error - for now just set to 1
        capacity = 1;
    }
    
    // Allocate header + buffer in one block
    size_t total_size = calculate_shm_size(capacity);
    void* mem = std::aligned_alloc(64, total_size);  // 64-byte aligned
    if (!mem) {
        // Handle allocation failure
        // For now, just set to nullptr (will crash on use)
        header_ = nullptr;
        buffer_ = nullptr;
        return;
    }
    
    // Placement new for header
    header_ = new (mem) Header();
    
    // Buffer follows header
    buffer_ = reinterpret_cast<T*>(static_cast<uint8_t*>(mem) + sizeof(Header));
    
    // Initialize header
    init_header(capacity);
}

// create_shm (static factory)
template <typename T>
Result<RingBuffer<SPSC, T>, Error> RingBuffer<SPSC, T>::create_shm(
    const String& name, 
    size_t capacity
) {
    if (capacity == 0) {
        return Result<RingBuffer, Error>::err(
            Error::invalid_argument("Capacity must be > 0")
        );
    }
    
    if (name.empty() || name[0] != '/') {
        return Result<RingBuffer, Error>::err(
            Error::invalid_argument("Shared memory name must start with '/'")
        );
    }
    
    // Calculate total size
    size_t total_size = calculate_shm_size(capacity);
    
    // Create shared memory
    int fd = shm_open(name.c_str(), O_CREAT | O_EXCL | O_RDWR, 0666);
    if (fd < 0) {
        if (errno == EEXIST) {
            return Result<RingBuffer, Error>::err(
                Error::already_exists("Shared memory already exists")
            );
        }
        return Result<RingBuffer, Error>::err(
            Error::io_error("shm_open failed")
        );
    }
    
    // Set size
    if (ftruncate(fd, total_size) < 0) {
        close(fd);
        shm_unlink(name.c_str());
        return Result<RingBuffer, Error>::err(
            Error::io_error("ftruncate failed")
        );
    }
    
    // Map into memory
    void* addr = mmap(nullptr, total_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (addr == MAP_FAILED) {
        close(fd);
        shm_unlink(name.c_str());
        return Result<RingBuffer, Error>::err(
            Error::io_error("mmap failed")
        );
    }
    
    // Construct RingBuffer
    RingBuffer ring;
    ring.header_ = new (addr) Header();  // Placement new
    ring.buffer_ = reinterpret_cast<T*>(static_cast<uint8_t*>(addr) + sizeof(Header));
    ring.owns_memory_ = true;
    ring.is_shm_ = true;
    ring.shm_fd_ = fd;
    ring.shm_size_ = total_size;
    ring.shm_name_ = name;
    
    // Initialize header
    ring.init_header(capacity);
    
    return Result<RingBuffer, Error>::ok(std::move(ring));
}

// attach_shm (static factory)
template <typename T>
Result<RingBuffer<SPSC, T>, Error> RingBuffer<SPSC, T>::attach_shm(const String& name) {
    if (name.empty() || name[0] != '/') {
        return Result<RingBuffer, Error>::err(
            Error::invalid_argument("Shared memory name must start with '/'")
        );
    }
    
    // Open existing shared memory
    int fd = shm_open(name.c_str(), O_RDWR, 0666);
    if (fd < 0) {
        return Result<RingBuffer, Error>::err(
            Error::not_found("Shared memory not found")
        );
    }
    
    // Get size
    struct stat st;
    if (fstat(fd, &st) < 0) {
        close(fd);
        return Result<RingBuffer, Error>::err(
            Error::io_error("fstat failed")
        );
    }
    size_t total_size = st.st_size;
    
    // Map into memory
    void* addr = mmap(nullptr, total_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (addr == MAP_FAILED) {
        close(fd);
        return Result<RingBuffer, Error>::err(
            Error::io_error("mmap failed")
        );
    }
    
    // Construct RingBuffer
    RingBuffer ring;
    ring.header_ = static_cast<Header*>(addr);
    ring.buffer_ = reinterpret_cast<T*>(static_cast<uint8_t*>(addr) + sizeof(Header));
    ring.owns_memory_ = false;  // We didn't create it, don't unlink
    ring.is_shm_ = true;
    ring.shm_fd_ = fd;
    ring.shm_size_ = total_size;
    ring.shm_name_ = name;
    
    // Verify header
    if (!ring.verify_header()) {
        munmap(addr, total_size);
        close(fd);
        return Result<RingBuffer, Error>::err(
            Error::invalid_argument("Invalid ring buffer header (magic mismatch)")
        );
    }
    
    return Result<RingBuffer, Error>::ok(std::move(ring));
}

// Destructor
template <typename T>
RingBuffer<SPSC, T>::~RingBuffer() {
    if (is_shm_) {
        // Unmap shared memory
        if (header_) {
            munmap(header_, shm_size_);
        }
        
        // Close fd
        if (shm_fd_ >= 0) {
            close(shm_fd_);
        }
        
        // Unlink if we own it
        if (owns_memory_ && !shm_name_.empty()) {
            shm_unlink(shm_name_.c_str());
        }
    } else {
        // Free heap memory
        if (header_) {
            std::free(header_);
        }
    }
}

// Move constructor
template <typename T>
RingBuffer<SPSC, T>::RingBuffer(RingBuffer&& other) noexcept
    : header_(other.header_)
    , buffer_(other.buffer_)
    , owns_memory_(other.owns_memory_)
    , is_shm_(other.is_shm_)
    , shm_fd_(other.shm_fd_)
    , shm_size_(other.shm_size_)
    , shm_name_(std::move(other.shm_name_))
{
    other.header_ = nullptr;
    other.buffer_ = nullptr;
    other.shm_fd_ = -1;
}

// Move assignment
template <typename T>
RingBuffer<SPSC, T>& RingBuffer<SPSC, T>::operator=(RingBuffer&& other) noexcept {
    if (this != &other) {
        // Clean up current resources
        this->~RingBuffer();
        
        // Move from other
        header_ = other.header_;
        buffer_ = other.buffer_;
        owns_memory_ = other.owns_memory_;
        is_shm_ = other.is_shm_;
        shm_fd_ = other.shm_fd_;
        shm_size_ = other.shm_size_;
        shm_name_ = std::move(other.shm_name_);
        
        // Nullify other
        other.header_ = nullptr;
        other.buffer_ = nullptr;
        other.shm_fd_ = -1;
    }
    return *this;
}

// push (const T&)
template <typename T>
inline Result<void, Error> RingBuffer<SPSC, T>::push(const T& item) {
    // Load positions
    uint64_t w = header_->write_pos.load(std::memory_order_relaxed);
    uint64_t r = header_->read_pos.load(std::memory_order_acquire);
    
    // Check if full
    if (w - r >= header_->capacity) {
        return Result<void, Error>::err(Error::timeout("Ring buffer full"));
    }
    
    // Write data
    size_t idx = w % header_->capacity;
    buffer_[idx] = item;
    
    // Release fence: ensure data write completes before updating write_pos
    std::atomic_thread_fence(std::memory_order_release);
    
    // Update write position (visible to consumer)
    header_->write_pos.store(w + 1, std::memory_order_release);
    
    return Result<void, Error>::ok();
}

// push (T&&)
template <typename T>
inline Result<void, Error> RingBuffer<SPSC, T>::push(T&& item) {
    uint64_t w = header_->write_pos.load(std::memory_order_relaxed);
    uint64_t r = header_->read_pos.load(std::memory_order_acquire);
    
    if (w - r >= header_->capacity) {
        return Result<void, Error>::err(Error::timeout("Ring buffer full"));
    }
    
    size_t idx = w % header_->capacity;
    buffer_[idx] = std::move(item);
    
    std::atomic_thread_fence(std::memory_order_release);
    header_->write_pos.store(w + 1, std::memory_order_release);
    
    return Result<void, Error>::ok();
}

// emplace
template <typename T>
template <typename... Args>
inline Result<void, Error> RingBuffer<SPSC, T>::emplace(Args&&... args) {
    uint64_t w = header_->write_pos.load(std::memory_order_relaxed);
    uint64_t r = header_->read_pos.load(std::memory_order_acquire);
    
    if (w - r >= header_->capacity) {
        return Result<void, Error>::err(Error::timeout("Ring buffer full"));
    }
    
    size_t idx = w % header_->capacity;
    new (&buffer_[idx]) T(std::forward<Args>(args)...);  // Placement new
    
    std::atomic_thread_fence(std::memory_order_release);
    header_->write_pos.store(w + 1, std::memory_order_release);
    
    return Result<void, Error>::ok();
}

// pop
template <typename T>
inline Result<T, Error> RingBuffer<SPSC, T>::pop() {
    // Load positions
    uint64_t r = header_->read_pos.load(std::memory_order_relaxed);
    uint64_t w = header_->write_pos.load(std::memory_order_acquire);
    
    // Check if empty
    if (w == r) {
        return Result<T, Error>::err(Error::timeout("Ring buffer empty"));
    }
    
    // Read data
    size_t idx = r % header_->capacity;
    T item = buffer_[idx];
    
    // Update read position (visible to producer)
    header_->read_pos.store(r + 1, std::memory_order_release);
    
    return Result<T, Error>::ok(std::move(item));
}

// peek
template <typename T>
inline Result<const T&, Error> RingBuffer<SPSC, T>::peek() const {
    uint64_t r = header_->read_pos.load(std::memory_order_relaxed);
    uint64_t w = header_->write_pos.load(std::memory_order_acquire);
    
    if (w == r) {
        return Result<const T&, Error>::err(Error::timeout("Ring buffer empty"));
    }
    
    size_t idx = r % header_->capacity;
    return Result<const T&, Error>::ok(buffer_[idx]);
}

// empty
template <typename T>
inline bool RingBuffer<SPSC, T>::empty() const noexcept {
    uint64_t r = header_->read_pos.load(std::memory_order_relaxed);
    uint64_t w = header_->write_pos.load(std::memory_order_acquire);
    return w == r;
}

// full
template <typename T>
inline bool RingBuffer<SPSC, T>::full() const noexcept {
    uint64_t w = header_->write_pos.load(std::memory_order_relaxed);
    uint64_t r = header_->read_pos.load(std::memory_order_acquire);
    return (w - r) >= header_->capacity;
}

// size
template <typename T>
inline size_t RingBuffer<SPSC, T>::size() const noexcept {
    uint64_t w = header_->write_pos.load(std::memory_order_acquire);
    uint64_t r = header_->read_pos.load(std::memory_order_acquire);
    return w - r;
}

// capacity
template <typename T>
inline size_t RingBuffer<SPSC, T>::capacity() const noexcept {
    return header_->capacity;
}

} // namespace datapod
```

---

## Key Implementation Details

### 1. Memory Ordering

**Producer (push)**:
```cpp
// 1. Write data
buffer_[idx] = item;

// 2. Release fence (ensures write completes)
std::atomic_thread_fence(std::memory_order_release);

// 3. Update write_pos (makes data visible to consumer)
write_pos.store(w + 1, std::memory_order_release);
```

**Consumer (pop)**:
```cpp
// 1. Load write_pos (synchronizes with producer's release)
uint64_t w = write_pos.load(std::memory_order_acquire);

// 2. Read data (guaranteed to see producer's write)
T item = buffer_[idx];

// 3. Update read_pos
read_pos.store(r + 1, std::memory_order_release);
```

### 2. Cache-Line Alignment

```cpp
struct alignas(64) Header {
    std::atomic<uint64_t> write_pos;
    uint8_t padding1[64 - sizeof(std::atomic<uint64_t>)];  // Pad to 64 bytes
    
    std::atomic<uint64_t> read_pos;
    uint8_t padding2[64 - sizeof(std::atomic<uint64_t>)];  // Pad to 64 bytes
    
    // ... metadata
};
```

This prevents **false sharing**: producer and consumer update different cache lines.

### 3. Wrapping Logic

Uses modulo arithmetic for wrapping:
```cpp
size_t idx = write_pos % capacity;
```

The positions (`write_pos`, `read_pos`) are **monotonically increasing** 64-bit counters.
They never wrap (would take millions of years at GHz rates).

### 4. Full/Empty Detection

```cpp
// Empty: write_pos == read_pos
bool empty = (write_pos == read_pos);

// Full: write_pos - read_pos == capacity
bool full = (write_pos - read_pos >= capacity);
```

This works because positions are monotonic counters.

### 5. Shared Memory Lifecycle

**Creator** (owns_memory_ = true):
- Creates shm with `O_CREAT | O_EXCL`
- Initializes header
- Destructor calls `shm_unlink()` to delete

**Attacher** (owns_memory_ = false):
- Opens existing shm
- Verifies header magic
- Destructor only unmaps (doesn't unlink)

---

## Testing Checklist

### Unit Tests

```cpp
// Test 1: Basic push/pop
void test_basic() {
    RingBuffer<SPSC, int> ring(16);
    assert(ring.empty());
    
    auto r1 = ring.push(42);
    assert(r1.is_ok());
    assert(!ring.empty());
    
    auto r2 = ring.pop();
    assert(r2.is_ok());
    assert(r2.value() == 42);
    assert(ring.empty());
}

// Test 2: Full detection
void test_full() {
    RingBuffer<SPSC, int> ring(4);
    
    for (int i = 0; i < 4; i++) {
        assert(ring.push(i).is_ok());
    }
    assert(ring.full());
    
    auto r = ring.push(99);
    assert(!r.is_ok());  // Should fail
}

// Test 3: Wrapping
void test_wrapping() {
    RingBuffer<SPSC, int> ring(4);
    
    // Fill and drain 10 times
    for (int round = 0; round < 10; round++) {
        for (int i = 0; i < 4; i++) {
            ring.push(i);
        }
        for (int i = 0; i < 4; i++) {
            auto val = ring.pop();
            assert(val.value() == i);
        }
    }
}

// Test 4: Shared memory
void test_shm() {
    // Create
    auto r1 = RingBuffer<SPSC, int>::create_shm("/test_ring", 16);
    assert(r1.is_ok());
    auto& ring1 = r1.value();
    ring1.push(123);
    
    // Attach
    auto r2 = RingBuffer<SPSC, int>::attach_shm("/test_ring");
    assert(r2.is_ok());
    auto& ring2 = r2.value();
    
    auto val = ring2.pop();
    assert(val.value() == 123);
}

// Test 5: Concurrent (multi-threaded)
void test_concurrent() {
    auto r = RingBuffer<SPSC, int>::create_shm("/stress_ring", 1024);
    auto& ring = r.value();
    
    std::thread producer([&]() {
        for (int i = 0; i < 1000000; i++) {
            while (!ring.push(i).is_ok()) {
                // Spin
            }
        }
    });
    
    std::thread consumer([&]() {
        for (int i = 0; i < 1000000; i++) {
            Result<int, Error> val;
            while (!(val = ring.pop()).is_ok()) {
                // Spin
            }
            assert(val.value() == i);
        }
    });
    
    producer.join();
    consumer.join();
}
```

---

## Usage Examples

### Example 1: In-Memory (Multi-Threaded)

```cpp
#include <datapod/concurrent/ring_buffer.hpp>
#include <thread>

void producer_thread(datapod::RingBuffer<datapod::SPSC, int>& ring) {
    for (int i = 0; i < 1000; i++) {
        while (!ring.push(i).is_ok()) {
            // Spin until space available
        }
    }
}

void consumer_thread(datapod::RingBuffer<datapod::SPSC, int>& ring) {
    for (int i = 0; i < 1000; i++) {
        datapod::Result<int, datapod::Error> result;
        while (!(result = ring.pop()).is_ok()) {
            // Spin until data available
        }
        process(result.value());
    }
}

int main() {
    datapod::RingBuffer<datapod::SPSC, int> ring(256);
    
    std::thread producer(producer_thread, std::ref(ring));
    std::thread consumer(consumer_thread, std::ref(ring));
    
    producer.join();
    consumer.join();
}
```

### Example 2: Shared Memory (Multi-Process)

```cpp
// Process A (producer)
#include <datapod/concurrent/ring_buffer.hpp>

int main() {
    auto result = datapod::RingBuffer<datapod::SPSC, int>::create_shm(
        "/sensor_data", 1024
    );
    
    if (!result.is_ok()) {
        return 1;
    }
    
    auto& ring = result.value();
    
    while (true) {
        int sensor_value = read_sensor();
        while (!ring.push(sensor_value).is_ok()) {
            // Wait for space
        }
    }
}

// Process B (consumer)
#include <datapod/concurrent/ring_buffer.hpp>

int main() {
    auto result = datapod::RingBuffer<datapod::SPSC, int>::attach_shm(
        "/sensor_data"
    );
    
    if (!result.is_ok()) {
        return 1;
    }
    
    auto& ring = result.value();
    
    while (true) {
        auto val = ring.pop();
        if (val.is_ok()) {
            process_sensor_data(val.value());
        }
    }
}
```

### Example 3: With echo Logging

```cpp
#include <datapod/concurrent/ring_buffer.hpp>
#include <echo/echo.hpp>

int main() {
    echo::info("Creating ring buffer").green();
    
    auto result = datapod::RingBuffer<datapod::SPSC, int>::create_shm(
        "/my_ring", 1024
    );
    
    if (!result.is_ok()) {
        echo::error("Failed to create ring: ", 
                    result.error().message.c_str()).red();
        return 1;
    }
    
    auto& ring = result.value();
    echo::info("Ring created, capacity: ", ring.capacity()).green();
    
    // Push data
    for (int i = 0; i < 100; i++) {
        auto r = ring.push(i);
        if (!r.is_ok()) {
            echo::warn("Ring full at i=", i).yellow();
            break;
        }
        echo::trace("Pushed: ", i);
    }
    
    echo::info("Ring size: ", ring.size());
}
```

---

## Performance Notes

- **Latency**: ~10-50ns for push/pop on modern CPUs
- **Throughput**: Can sustain millions of ops/sec
- **Cache efficiency**: Cache-line alignment prevents false sharing
- **Memory ordering**: Acquire/release is faster than seq_cst
- **No syscalls**: Hot path is pure atomic operations

---

## Common Pitfalls

1. **Using from multiple producers/consumers**: UNDEFINED BEHAVIOR
   - SPSC means exactly ONE producer and ONE consumer
   
2. **Not checking Result**: Always check `is_ok()` before using value
   ```cpp
   auto result = ring.pop();
   int val = result.value();  // ❌ WRONG - might throw if empty
   
   if (result.is_ok()) {      // ✅ CORRECT
       int val = result.value();
   }
   ```

3. **Shared memory name conflicts**: Use unique names per ring
   ```cpp
   // ❌ WRONG - same name in two places
   RingBuffer<SPSC, int>::create_shm("/ring", 1024);
   RingBuffer<SPSC, int>::create_shm("/ring", 1024);  // Fails with ALREADY_EXISTS
   ```

4. **Forgetting to unlink**: Creator process should run to completion
   - If creator crashes, shm persists: `rm /dev/shm/ring_name`

5. **Type mismatch**: Creator and attacher must use same T
   ```cpp
   // Process A
   RingBuffer<SPSC, int>::create_shm("/ring", 1024);
   
   // Process B
   RingBuffer<SPSC, float>::attach_shm("/ring");  // ❌ WRONG - type mismatch
   ```

---

## Future Extensions

### MPMC (Multi-Producer Multi-Consumer)

```cpp
template <typename T>
class RingBuffer<MPMC, T> {
    // Uses CAS (Compare-And-Swap) loops
    // More complex, slower than SPSC
    // Needs sequence numbers to handle ABA problem
};
```

### SPMC (Single-Producer Multi-Consumer)

```cpp
template <typename T>
class RingBuffer<SPMC, T> {
    // Producer uses simple atomic increment
    // Consumers use CAS to claim slots
};
```

---

## References

1. **LWN: Ring buffers** - https://lwn.net/Articles/976836/
2. **Dmitry Vyukov's MPMC queue** - http://www.1024cores.net/home/lock-free-algorithms/queues
3. **Linux kernel kfifo** - https://www.kernel.org/doc/html/latest/core-api/kfifo.html
4. **C++ memory ordering** - https://en.cppreference.com/w/cpp/atomic/memory_order
5. **False sharing** - https://en.wikipedia.org/wiki/False_sharing

---

## Implementation Checklist

- [ ] Create `datapod/include/datapod/lockfree/ring_buffer.hpp`
- [ ] Implement SPSC specialization
- [ ] Add comprehensive comments
- [ ] Write unit tests
- [ ] Write concurrent stress tests
- [ ] Test shared memory mode
- [ ] Add to datapod CMakeLists.txt
- [ ] Update datapod README
- [ ] Create examples
- [ ] Run benchmarks
- [ ] Update wirebit to use `datapod::RingBuffer<datapod::SPSC, uint8_t>`

---

**Ready to implement!** 🚀

---

## Wirebit-Specific Usage Examples

### Example 1: Forward Frames to Another Process

```cpp
#include <datapod/lockfree/ring_buffer.hpp>
#include <datapod/serialization/serialize.hpp>
#include <echo/echo.hpp>

// Process A: Capture and forward frames
void forward_frames() {
    RingBuffer<SPSC, Frame> ring(1024);
    
    // ... producer fills ring with frames ...
    
    // Capture snapshot with all frame data
    auto snap = ring.snapshot_with_data();
    echo::info("Captured ", snap.data.size(), " frames for forwarding").green();
    
    // Serialize
    auto serialized = datapod::serialize(snap);
    echo::debug("Serialized to ", serialized.size(), " bytes");
    
    // Send over network/IPC
    send_to_remote_process(serialized);
}

// Process B: Receive and process frames
void receive_frames(const Vector<uint8_t>& data) {
    // Deserialize
    auto snap = datapod::deserialize<SnapshotWithData<Frame>>(data);
    
    echo::info("Received ", snap.data.size(), " frames").green();
    
    // Process each frame
    for (const auto& frame : snap.data) {
        echo::trace("Processing frame: type=", frame.header.frame_type);
        process_frame(frame);
    }
}
```

### Example 2: Checkpoint Ring Buffer State

```cpp
#include <datapod/lockfree/ring_buffer.hpp>
#include <fstream>

// Save checkpoint
void save_checkpoint(const RingBuffer<SPSC, Frame>& ring) {
    echo::info("Saving checkpoint...").yellow();
    
    // Capture snapshot with data
    auto snap = ring.snapshot_with_data();
    
    // Serialize
    auto data = datapod::serialize(snap);
    
    // Write to file
    std::ofstream file("ring_checkpoint.bin", std::ios::binary);
    file.write(reinterpret_cast<const char*>(data.data()), data.size());
    
    echo::info("Checkpoint saved: ", snap.data.size(), " frames").green();
}

// Restore from checkpoint
Result<RingBuffer<SPSC, Frame>, Error> restore_checkpoint() {
    echo::info("Restoring from checkpoint...").yellow();
    
    // Read from file
    std::ifstream file("ring_checkpoint.bin", std::ios::binary);
    Vector<uint8_t> data((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());
    
    // Deserialize
    auto snap = datapod::deserialize<SnapshotWithData<Frame>>(data);
    
    // Restore ring buffer
    auto result = RingBuffer<SPSC, Frame>::from_snapshot(snap);
    
    if (result.is_ok()) {
        echo::info("Restored ", result.value().size(), " frames").green();
    } else {
        echo::error("Failed to restore: ", result.error().message.c_str()).red();
    }
    
    return result;
}
```

### Example 3: Debug Frame Transmission Issues

```cpp
#include <datapod/lockfree/ring_buffer.hpp>
#include <echo/echo.hpp>

void process_with_error_capture(RingBuffer<SPSC, Frame>& ring) {
    try {
        // Process frames
        while (true) {
            auto result = ring.pop();
            if (!result.is_ok()) break;
            
            process_frame(result.value());
        }
    } catch (const std::exception& e) {
        echo::error("Error during processing: ", e.what()).red().bold();
        
        // Capture ring buffer state for debugging
        auto snap = ring.snapshot_with_data();
        
        echo::error("Ring buffer state at error:").red();
        echo::error("  Capacity: ", snap.capacity);
        echo::error("  Write pos: ", snap.write_pos);
        echo::error("  Read pos: ", snap.read_pos);
        echo::error("  Remaining frames: ", snap.data.size());
        
        // Save debug snapshot
        auto data = datapod::serialize(snap);
        std::ofstream debug_file("error_snapshot.bin", std::ios::binary);
        debug_file.write(reinterpret_cast<const char*>(data.data()), data.size());
        
        echo::info("Debug snapshot saved to error_snapshot.bin").yellow();
    }
}
```

### Example 4: Batch Processing with Drain

```cpp
#include <datapod/lockfree/ring_buffer.hpp>
#include <echo/echo.hpp>

void batch_processor(RingBuffer<SPSC, Frame>& ring) {
    while (true) {
        // Wait for ring to have some data
        while (ring.empty()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        
        // Drain all frames for batch processing
        auto frames = ring.drain();
        
        echo::info("Processing batch of ", frames.size(), " frames").cyan();
        
        // Process entire batch
        process_batch(frames);
        
        echo::debug("Batch complete, ring is now empty");
    }
}
```

### Example 5: Replay Captured Frames

```cpp
#include <datapod/lockfree/ring_buffer.hpp>
#include <echo/echo.hpp>

// Capture frames during normal operation
void capture_for_replay(RingBuffer<SPSC, Frame>& ring) {
    auto snap = ring.snapshot_with_data();
    
    echo::info("Captured ", snap.data.size(), " frames for replay").green();
    
    // Save for later replay
    auto data = datapod::serialize(snap);
    std::ofstream file("replay_data.bin", std::ios::binary);
    file.write(reinterpret_cast<const char*>(data.data()), data.size());
}

// Replay captured frames in test environment
void replay_frames() {
    echo::info("Replaying captured frames...").cyan().bold();
    
    // Load captured data
    std::ifstream file("replay_data.bin", std::ios::binary);
    Vector<uint8_t> data((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());
    
    auto snap = datapod::deserialize<SnapshotWithData<Frame>>(data);
    
    echo::info("Replaying ", snap.data.size(), " frames").cyan();
    
    // Replay each frame with original timing
    for (const auto& frame : snap.data) {
        // Simulate original timing
        uint64_t delay_ns = frame.header.deliver_at_ns - frame.header.tx_timestamp_ns;
        std::this_thread::sleep_for(std::chrono::nanoseconds(delay_ns));
        
        echo::trace("Replaying frame: type=", frame.header.frame_type);
        inject_frame_to_test_system(frame);
    }
    
    echo::info("Replay complete").green();
}
```

### Example 6: Monitoring Ring Buffer Health

```cpp
#include <datapod/lockfree/ring_buffer.hpp>
#include <echo/echo.hpp>

void monitor_ring_health(const RingBuffer<SPSC, Frame>& ring) {
    // Lightweight snapshot (metadata only)
    auto snap = ring.snapshot();
    
    size_t used = snap.write_pos - snap.read_pos;
    double usage_percent = (used * 100.0) / snap.capacity;
    
    if (usage_percent > 80.0) {
        echo::warn("Ring buffer ", usage_percent, "% full!").yellow();
    } else {
        echo::debug("Ring buffer usage: ", usage_percent, "%");
    }
    
    // If high usage, capture full snapshot for analysis
    if (usage_percent > 90.0) {
        echo::error("Ring buffer critically full, capturing snapshot").red();
        auto full_snap = ring.snapshot_with_data();
        
        // Analyze frame types
        std::map<uint16_t, size_t> frame_counts;
        for (const auto& frame : full_snap.data) {
            frame_counts[frame.header.frame_type]++;
        }
        
        echo::error("Frame type distribution:");
        for (const auto& [type, count] : frame_counts) {
            echo::error("  Type ", type, ": ", count, " frames");
        }
    }
}
```

### Example 7: Migrate Data Between Shared Memory Regions

```cpp
#include <datapod/lockfree/ring_buffer.hpp>
#include <echo/echo.hpp>

void migrate_ring_buffer() {
    echo::info("Migrating ring buffer to new shared memory region...").cyan();
    
    // Open old ring buffer
    auto old_ring_result = RingBuffer<SPSC, Frame>::attach_shm("/old_ring");
    if (!old_ring_result.is_ok()) {
        echo::error("Failed to attach to old ring").red();
        return;
    }
    auto& old_ring = old_ring_result.value();
    
    // Capture all data
    auto snap = old_ring.snapshot_with_data();
    echo::info("Captured ", snap.data.size(), " frames from old ring");
    
    // Create new ring buffer
    auto new_ring_result = RingBuffer<SPSC, Frame>::create_shm("/new_ring", snap.capacity);
    if (!new_ring_result.is_ok()) {
        echo::error("Failed to create new ring").red();
        return;
    }
    auto& new_ring = new_ring_result.value();
    
    // Restore data to new ring
    auto restore_result = RingBuffer<SPSC, Frame>::from_snapshot(snap);
    if (!restore_result.is_ok()) {
        echo::error("Failed to restore snapshot").red();
        return;
    }
    
    echo::info("Migration complete: ", snap.data.size(), " frames migrated").green();
}
```

---

## Performance Comparison

| Operation | Time Complexity | Space Complexity | Use Case |
|-----------|----------------|------------------|----------|
| `snapshot()` | O(1) | O(1) | Frequent monitoring, metrics |
| `snapshot_with_data()` | O(n) | O(n) | Forwarding, checkpointing, debugging |
| `from_snapshot()` | O(n) | O(n) | Restore from checkpoint |
| `drain()` | O(n) | O(n) | Batch processing, shutdown |
| `push()` | O(1) | O(1) | Normal operation |
| `pop()` | O(1) | O(1) | Normal operation |

**Recommendation**:
- Use `snapshot()` for frequent health checks (every second)
- Use `snapshot_with_data()` sparingly (on errors, periodic checkpoints)
- Use `drain()` for batch processing or graceful shutdown

---

## Implementation Checklist

- [ ] Create `datapod/include/datapod/lockfree/ring_buffer.hpp`
- [ ] Implement `RingBuffer<SPSC, T>` specialization
- [ ] Implement `Snapshot` struct with `members()`
- [ ] Implement `SnapshotWithData` struct with `members()`
- [ ] Implement `snapshot()` method
- [ ] Implement `snapshot_with_data()` method
- [ ] Implement `from_snapshot()` static factory
- [ ] Implement `drain()` method
- [ ] Add comprehensive comments
- [ ] Write unit tests (basic push/pop, full/empty, wrapping)
- [ ] Write concurrent stress tests
- [ ] Test shared memory mode
- [ ] Test snapshot serialization/deserialization
- [ ] Test from_snapshot() restoration
- [ ] Add to datapod CMakeLists.txt
- [ ] Update datapod README
- [ ] Create examples
- [ ] Run benchmarks
- [ ] Update wirebit to use `datapod::RingBuffer<datapod::SPSC, uint8_t>`

---

**Ready to implement!** 🚀

The specification is now complete with full data capture and forwarding capabilities!
