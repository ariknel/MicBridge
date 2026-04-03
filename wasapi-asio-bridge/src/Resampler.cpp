#include "Resampler.h"
#include <cstring>
#include <stdexcept>
#include <cmath>

namespace Bridge {

void Resampler::configure(uint32_t srcRate, uint32_t dstRate, uint32_t channels) {
    if (srcRate  == 0) srcRate  = 48000;
    if (dstRate  == 0) dstRate  = 48000;
    if (channels == 0) channels = 2;
    m_srcRate    = srcRate;
    m_dstRate    = dstRate;
    m_channels   = channels;
    m_passthrough = (srcRate == dstRate);
    m_ratio      = static_cast<double>(dstRate) / static_cast<double>(srcRate);
    m_phase      = 0.0;
    m_lastFrame.assign(channels, 0.0f);
}

size_t Resampler::process(const float* input,  size_t inputFrames,
                           float*       output, size_t maxOutputFrames) {
    if (m_passthrough) {
        size_t frames = std::min(inputFrames, maxOutputFrames);
        std::memcpy(output, input, frames * m_channels * sizeof(float));
        return frames;
    }
    if (inputFrames == 0) return 0;

    size_t outFrames = 0;

    while (outFrames < maxOutputFrames) {
        // Source position
        double srcPos = m_phase;
        size_t srcIdx = static_cast<size_t>(srcPos);
        float  frac   = static_cast<float>(srcPos - srcIdx);

        if (srcIdx >= inputFrames) {
            // Ran out of input; update phase for next block
            m_phase = srcPos - static_cast<double>(inputFrames);
            // Carry last frame
            for (uint32_t c = 0; c < m_channels; ++c)
                m_lastFrame[c] = input[(inputFrames - 1) * m_channels + c];
            break;
        }

        // Linear interpolation between srcIdx and srcIdx+1
        for (uint32_t c = 0; c < m_channels; ++c) {
            float s0 = (srcIdx == 0) ? m_lastFrame[c]
                                     : input[(srcIdx - 1) * m_channels + c];
            // Wait: standard interp is between srcIdx and srcIdx+1
            float a = input[srcIdx * m_channels + c];
            float b = (srcIdx + 1 < inputFrames)
                        ? input[(srcIdx + 1) * m_channels + c]
                        : a;
            output[outFrames * m_channels + c] = a + frac * (b - a);
        }
        ++outFrames;
        m_phase += 1.0 / m_ratio;
    }

    // Store last frame for next block boundary
    if (inputFrames > 0) {
        for (uint32_t c = 0; c < m_channels; ++c)
            m_lastFrame[c] = input[(inputFrames - 1) * m_channels + c];
    }

    return outFrames;
}

} // namespace Bridge
