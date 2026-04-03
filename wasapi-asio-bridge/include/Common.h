#pragma once
#include <windows.h>
#include <cstdint>
#include <string>
#include <functional>
#include <atomic>
#include <chrono>

namespace Bridge {

// -- Version ------------------------------------------------------------------
constexpr int VERSION_MAJOR = 1;
constexpr int VERSION_MINOR = 0;
constexpr int VERSION_PATCH = 0;
constexpr const char* VERSION_STR = "1.0.0";

// -- Audio constants -----------------------------------------------------------
constexpr uint32_t DEFAULT_SAMPLE_RATE    = 48000;
constexpr uint32_t DEFAULT_BUFFER_FRAMES  = 480;   // 10ms @ 48kHz
constexpr uint32_t DEFAULT_RING_FRAMES    = 9600;  // 200ms safety buffer
constexpr uint32_t MAX_CHANNELS           = 8;
constexpr uint32_t TARGET_LATENCY_MS      = 30;

// Common sample rates
constexpr uint32_t SAMPLE_RATES[] = {44100, 48000, 88200, 96000, 192000};
constexpr int NUM_SAMPLE_RATES = 5;

// -- Bridge status -------------------------------------------------------------
enum class BridgeStatus {
    Idle,
    Starting,
    Running,
    Stopping,
    Error,
    NoDevice,
    Underrun
};

inline const char* StatusToString(BridgeStatus s) {
    switch (s) {
        case BridgeStatus::Idle:     return "Idle";
        case BridgeStatus::Starting: return "Starting";
        case BridgeStatus::Running:  return "Running";
        case BridgeStatus::Stopping: return "Stopping";
        case BridgeStatus::Error:    return "Error";
        case BridgeStatus::NoDevice: return "No Device";
        case BridgeStatus::Underrun: return "Underrun";
        default:                     return "Unknown";
    }
}

// -- Callbacks -----------------------------------------------------------------
using StatusCallback = std::function<void(BridgeStatus, const std::string&)>;
using LevelCallback  = std::function<void(float leftRMS, float rightRMS)>;
using ErrorCallback  = std::function<void(const std::string&)>;

// -- HRESULT helper ------------------------------------------------------------
inline std::string HrToString(HRESULT hr) {
    char buf[64];
    snprintf(buf, sizeof(buf), "HRESULT 0x%08X", (unsigned)hr);
    return buf;
}

#define CHECK_HR(hr, msg) \
    do { if (FAILED(hr)) { \
        throw std::runtime_error(std::string(msg) + ": " + HrToString(hr)); \
    }} while(0)

} // namespace Bridge
