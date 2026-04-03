#pragma once
#include "AudioFormat.h"
#include <vector>
#include <cstddef>

namespace Bridge {

// -----------------------------------------------------------------------------
// Linear interpolation resampler (float32 stereo/mono).
// For small ratio differences (e.g. 44100->48000) this is sufficient.
// For larger ratios a sinc-based approach could be added later.
// -----------------------------------------------------------------------------
class Resampler {
public:
    Resampler() = default;

    void configure(uint32_t srcRate, uint32_t dstRate, uint32_t channels);

    // Process interleaved float32 input, produce interleaved float32 output.
    // Returns number of output frames written.
    size_t process(const float* input,  size_t inputFrames,
                   float*       output, size_t maxOutputFrames);

    bool isPassthrough() const { return m_passthrough; }

    uint32_t srcRate() const { return m_srcRate; }
    uint32_t dstRate() const { return m_dstRate; }

private:
    uint32_t m_srcRate   = 48000;
    uint32_t m_dstRate   = 48000;
    uint32_t m_channels  = 2;
    bool     m_passthrough = true;

    double   m_ratio     = 1.0;   // dstRate / srcRate
    double   m_phase     = 0.0;   // fractional position in src

    // Last frame from previous block (for interpolation across boundaries)
    std::vector<float> m_lastFrame;
};

} // namespace Bridge
