#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>
#include <cassert>

namespace Bridge {

// -----------------------------------------------------------------------------
// Lock-free single-producer / single-consumer ring buffer
// Uses power-of-2 capacity for fast modulo via bitmask.
// Safe across two threads (capture thread writes, output thread reads).
// -----------------------------------------------------------------------------
class RingBuffer {
public:
    explicit RingBuffer(size_t capacityFrames, size_t frameBytes);
    ~RingBuffer() = default;

    // Non-copyable
    RingBuffer(const RingBuffer&) = delete;
    RingBuffer& operator=(const RingBuffer&) = delete;

    // Write `frames` frames from `src`. Returns frames actually written.
    size_t write(const void* src, size_t frames);

    // Read `frames` frames into `dst`. Returns frames actually read.
    // If insufficient data, fills rest with silence (zeros).
    size_t read(void* dst, size_t frames);

    // Read without consuming (for monitoring)
    size_t peek(void* dst, size_t frames) const;

    size_t availableRead()  const noexcept;
    size_t availableWrite() const noexcept;
    size_t capacityFrames() const noexcept { return m_capacityFrames; }

    void reset();

    // Underrun/overrun counters
    uint64_t underruns() const noexcept { return m_underruns.load(std::memory_order_relaxed); }
    uint64_t overruns()  const noexcept { return m_overruns.load(std::memory_order_relaxed); }

private:
    size_t                   m_frameBytes;
    size_t                   m_capacityFrames;  // power of 2
    size_t                   m_mask;            // capacityFrames - 1
    std::vector<uint8_t>     m_buf;

    alignas(64) std::atomic<size_t> m_writePos { 0 };
    alignas(64) std::atomic<size_t> m_readPos  { 0 };

    std::atomic<uint64_t>    m_underruns { 0 };
    std::atomic<uint64_t>    m_overruns  { 0 };

};

} // namespace Bridge
