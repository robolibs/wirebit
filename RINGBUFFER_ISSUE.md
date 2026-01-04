# RingBuffer SHM Ownership Bug in datapod

## Summary

There is a critical bug in `datapod::RingBuffer`'s move semantics that prevents shared memory (SHM) segments from persisting correctly. The move constructor and move assignment operator fail to transfer ownership flags, causing premature `shm_unlink()` calls that delete SHM segments before other processes can attach to them.

## Impact

- **All ShmLink tests fail** - Clients cannot attach to SHM segments created by servers
- **Error:** "Shared memory not found" when calling `attach_shm()`
- **Affects:** Any multi-process communication using `RingBuffer::create_shm()` and `RingBuffer::attach_shm()`

## Root Cause

### The Problem

When a `RingBuffer` is moved, the move constructor/assignment operator:
1. ✅ Transfers pointers (`header_`, `buffer_`)
2. ✅ Transfers file descriptor (`shm_fd_`)
3. ✅ Transfers SHM name (`shm_name_`)
4. ❌ **COPIES** `owns_memory_` flag (should transfer)
5. ❌ **COPIES** `is_shm_` flag (should clear)

### What Happens

```cpp
// In create_shm():
RingBuffer ring;
ring.owns_memory_ = true;
ring.is_shm_ = true;
ring.shm_name_ = "/test";
return Result::ok(std::move(ring));  // Move constructor called

// After move:
// - New object: owns_memory_=true, is_shm_=true, shm_name_="/test"
// - Old object: owns_memory_=true, is_shm_=true, shm_name_=""  ← BUG!

// When old object destructor runs:
if (is_shm_) {  // TRUE!
    if (owns_memory_ && !shm_name_.empty())  // TRUE && FALSE = FALSE
        shm_unlink(shm_name_.c_str());  // Not called (name is empty)
}
```

While the current code doesn't unlink (because `shm_name_` is empty), having `owns_memory_=true` and `is_shm_=true` on a moved-from object is semantically incorrect and could cause issues in other scenarios (e.g., if the destructor logic changes, or if there are multiple moves).

## The Fix

### File: `datapod/include/datapod/lockfree/ring_buffer.hpp`

#### 1. Move Constructor (around line 250)

**Before:**
```cpp
template <typename T>
RingBuffer<SPSC, T>::RingBuffer(RingBuffer &&other) noexcept
    : header_(other.header_), buffer_(other.buffer_), owns_memory_(other.owns_memory_), is_shm_(other.is_shm_),
      shm_fd_(other.shm_fd_), shm_size_(other.shm_size_), shm_name_(std::move(other.shm_name_)) {
    other.header_ = nullptr;
    other.buffer_ = nullptr;
    other.shm_fd_ = -1;
}
```

**After:**
```cpp
template <typename T>
RingBuffer<SPSC, T>::RingBuffer(RingBuffer &&other) noexcept
    : header_(other.header_), buffer_(other.buffer_), owns_memory_(other.owns_memory_), is_shm_(other.is_shm_),
      shm_fd_(other.shm_fd_), shm_size_(other.shm_size_), shm_name_(std::move(other.shm_name_)) {
    other.header_ = nullptr;
    other.buffer_ = nullptr;
    other.shm_fd_ = -1;
    other.owns_memory_ = false;  // Transfer ownership to new object
    other.is_shm_ = false;       // Clear SHM flag on moved-from object
}
```

#### 2. Move Assignment Operator (around line 260)

**Before:**
```cpp
template <typename T> RingBuffer<SPSC, T> &RingBuffer<SPSC, T>::operator=(RingBuffer &&other) noexcept {
    if (this != &other) {
        this->~RingBuffer();
        header_ = other.header_;
        buffer_ = other.buffer_;
        owns_memory_ = other.owns_memory_;
        is_shm_ = other.is_shm_;
        shm_fd_ = other.shm_fd_;
        shm_size_ = other.shm_size_;
        shm_name_ = std::move(other.shm_name_);
        other.header_ = nullptr;
        other.buffer_ = nullptr;
        other.shm_fd_ = -1;
    }
    return *this;
}
```

**After:**
```cpp
template <typename T> RingBuffer<SPSC, T> &RingBuffer<SPSC, T>::operator=(RingBuffer &&other) noexcept {
    if (this != &other) {
        this->~RingBuffer();
        header_ = other.header_;
        buffer_ = other.buffer_;
        owns_memory_ = other.owns_memory_;
        is_shm_ = other.is_shm_;
        shm_fd_ = other.shm_fd_;
        shm_size_ = other.shm_size_;
        shm_name_ = std::move(other.shm_name_);
        other.header_ = nullptr;
        other.buffer_ = nullptr;
        other.shm_fd_ = -1;
        other.owns_memory_ = false;  // Transfer ownership to new object
        other.is_shm_ = false;       // Clear SHM flag on moved-from object
    }
    return *this;
}
```

## Reproduction

### Minimal Test Case

```cpp
#include <datapod/lockfree/ring_buffer.hpp>
#include <datapod/sequential/string.hpp>
#include <iostream>

int main() {
    datapod::String name("/test_shm");
    shm_unlink(name.c_str());
    
    // Create SHM ring
    auto create_result = datapod::RingBuffer<datapod::SPSC, uint8_t>::create_shm(name, 4096);
    if (!create_result.is_ok()) {
        std::cerr << "Create failed" << std::endl;
        return 1;
    }
    
    // Move from result
    auto ring = std::move(create_result.value());
    
    // Try to attach from same process (should work but fails)
    auto attach_result = datapod::RingBuffer<datapod::SPSC, uint8_t>::attach_shm(name);
    if (!attach_result.is_ok()) {
        std::cerr << "Attach failed: " << attach_result.error().message.c_str() << std::endl;
        return 1;
    }
    
    std::cout << "Success!" << std::endl;
    shm_unlink(name.c_str());
    return 0;
}
```

**Expected:** "Success!"  
**Actual:** "Attach failed: Shared memory not found"

## Verification

After applying the fix, the following should work:

1. **Basic SHM test:** Create and attach in same process
2. **Multi-process test:** Server creates SHM, client attaches
3. **wirebit ShmLink tests:** All tests in `test/test_shmlink.cpp` should pass

## Workaround (Temporary)

Until datapod is fixed, wirebit uses a workaround in `ShmLink::create()`:

```cpp
// Create SHM rings
auto tx_create_result = FrameRing::create_shm(tx_name, capacity_bytes);
auto rx_create_result = FrameRing::create_shm(rx_name, capacity_bytes);

// Attach to the rings we just created (non-owning handles)
auto tx_attach_result = FrameRing::attach_shm(tx_name);
auto rx_attach_result = FrameRing::attach_shm(rx_name);

// Use attached rings for I/O, keep create results alive to prevent unlink
ShmLink link(name, 
             std::move(tx_attach_result.value()), 
             std::move(rx_attach_result.value()),
             std::move(tx_create_result.value()),  // Keeper
             std::move(rx_create_result.value())); // Keeper
```

## Related Files

- `datapod/include/datapod/lockfree/ring_buffer.hpp` - Needs fix
- `wirebit/include/wirebit/shm/ring.hpp` - Uses RingBuffer
- `wirebit/include/wirebit/shm/shm_link.hpp` - Implements workaround
- `wirebit/test/test_shmlink.cpp` - Failing tests
- `wirebit/test/test_shm_simple.cpp` - Minimal reproduction

## Action Items

- [ ] Apply fix to datapod RingBuffer
- [ ] Test with wirebit ShmLink
- [ ] Remove workaround from wirebit once datapod is fixed
- [ ] Add regression test to datapod for SHM move semantics

## References

- Move semantics best practices: Moved-from objects should be in a valid but unspecified state
- POSIX SHM: `shm_unlink()` removes the name but segment persists while mapped
- RAII: Ownership should be exclusive - only one object should own a resource
