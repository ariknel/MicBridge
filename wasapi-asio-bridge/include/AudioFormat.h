#pragma once
#include <cstdint>
#include <string>

namespace Bridge {

enum class SampleFormat {
    Float32,   // IEEE float, most common for WASAPI shared
    Int16,
    Int24,
    Int32,
};

inline const char* SampleFormatToString(SampleFormat f) {
    switch (f) {
        case SampleFormat::Float32: return "Float32";
        case SampleFormat::Int16:   return "Int16";
        case SampleFormat::Int24:   return "Int24";
        case SampleFormat::Int32:   return "Int32";
        default:                    return "Unknown";
    }
}

inline uint32_t SampleFormatBytes(SampleFormat f) {
    switch (f) {
        case SampleFormat::Float32: return 4;
        case SampleFormat::Int16:   return 2;
        case SampleFormat::Int24:   return 3;
        case SampleFormat::Int32:   return 4;
        default:                    return 4;
    }
}

struct AudioFormat {
    uint32_t     sampleRate  = 48000;
    uint32_t     channels    = 2;
    SampleFormat format      = SampleFormat::Float32;
    uint32_t     bitsPerSample = 32;

    uint32_t frameBytes() const {
        return channels * SampleFormatBytes(format);
    }

    std::string toString() const {
        return std::to_string(sampleRate) + "Hz/"
             + std::to_string(channels) + "ch/"
             + SampleFormatToString(format);
    }

    bool operator==(const AudioFormat& o) const {
        return sampleRate == o.sampleRate
            && channels   == o.channels
            && format     == o.format;
    }
    bool operator!=(const AudioFormat& o) const { return !(*this == o); }
};

} // namespace Bridge
