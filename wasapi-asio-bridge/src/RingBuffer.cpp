#include "RingBuffer.h"
#include <stdexcept>
#include <algorithm>

namespace Bridge {

static size_t nextPow2(size_t n) {
    if (n == 0) return 1;
    n--;
    n |= n >> 1;  n |= n >> 2;  n |= n >> 4;
    n |= n >> 8;  n |= n >> 16; n |= n >> 32;
    return n + 1;
}

RingBuffer::RingBuffer(size_t capacityFrames, size_t frameBytes)
    : m_frameBytes(frameBytes)
    , m_capacityFrames(nextPow2(capacityFrames + 1))  // +1 so cap is usable
    , m_mask(m_capacityFrames - 1)
    , m_buf(m_capacityFrames * frameBytes, 0)
{
    if (frameBytes == 0) throw std::invalid_argument("frameBytes must be > 0");
}

size_t RingBuffer::availableRead() const noexcept {
    auto w = m_writePos.load(std::memory_order_acquire);
    auto r = m_readPos.load(std::memory_order_acquire);
    return w - r;  // monotonic counters: difference is always frames in buffer
}

size_t RingBuffer::availableWrite() const noexcept {
    auto w = m_writePos.load(std::memory_order_acquire);
    auto r = m_readPos.load(std::memory_order_acquire);
    return m_capacityFrames - 1 - (w - r);  // leave 1 slot to distinguish full vs empty
}

size_t RingBuffer::write(const void* src, size_t frames) {
    auto avail = availableWrite();
    if (frames > avail) {
        m_overruns.fetch_add(1, std::memory_order_relaxed);
        frames = avail;  // write what fits
    }
    if (frames == 0) return 0;

    auto w = m_writePos.load(std::memory_order_relaxed) & m_mask;
    const uint8_t* in = static_cast<const uint8_t*>(src);

    size_t first = std::min(frames, m_capacityFrames - w);
    std::memcpy(m_buf.data() + w * m_frameBytes, in, first * m_frameBytes);

    if (frames > first) {
        std::memcpy(m_buf.data(), in + first * m_frameBytes,
                    (frames - first) * m_frameBytes);
    }

    m_writePos.fetch_add(frames, std::memory_order_release);
    return frames;
}

size_t RingBuffer::read(void* dst, size_t frames) {
    auto avail = availableRead();
    size_t silence = 0;

    if (frames > avail) {
        m_underruns.fetch_add(1, std::memory_order_relaxed);
        silence = frames - avail;
        frames  = avail;
    }

    uint8_t* out = static_cast<uint8_t*>(dst);

    if (frames > 0) {
        auto r = m_readPos.load(std::memory_order_relaxed) & m_mask;
        size_t first = std::min(frames, m_capacityFrames - r);
        std::memcpy(out, m_buf.data() + r * m_frameBytes, first * m_frameBytes);
        if (frames > first) {
            std::memcpy(out + first * m_frameBytes, m_buf.data(),
                        (frames - first) * m_frameBytes);
        }
        m_readPos.fetch_add(frames, std::memory_order_release);
    }

    // Pad with silence
    if (silence > 0) {
        std::memset(out + frames * m_frameBytes, 0, silence * m_frameBytes);
    }

    return frames + silence;
}

size_t RingBuffer::peek(void* dst, size_t frames) const {
    auto avail = availableRead();
    frames = std::min(frames, avail);
    if (frames == 0) { return 0; }

    auto r = m_readPos.load(std::memory_order_relaxed) & m_mask;
    uint8_t* out = static_cast<uint8_t*>(dst);
    size_t first = std::min(frames, m_capacityFrames - r);
    std::memcpy(out, m_buf.data() + r * m_frameBytes, first * m_frameBytes);
    if (frames > first) {
        std::memcpy(out + first * m_frameBytes, m_buf.data(),
                    (frames - first) * m_frameBytes);
    }
    return frames;
}

void RingBuffer::reset() {
    m_writePos.store(0, std::memory_order_relaxed);
    m_readPos.store(0, std::memory_order_relaxed);
    std::fill(m_buf.begin(), m_buf.end(), 0);
    m_underruns.store(0, std::memory_order_relaxed);
    m_overruns.store(0, std::memory_order_relaxed);
}

} // namespace Bridge
