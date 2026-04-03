#pragma once
#include "Common.h"
#include "AudioFormat.h"
#include "WasapiCapture.h"
#include "AsioOutput.h"
#include "RingBuffer.h"
#include "DeviceEnumerator.h"

#include <memory>
#include <string>
#include <atomic>
#include <functional>
#include <chrono>

namespace Bridge {

struct EngineConfig {
    std::wstring captureDeviceId;   // empty = default
    std::wstring outputDeviceId;    // empty = default render
    uint32_t     sampleRate   = DEFAULT_SAMPLE_RATE;
    uint32_t     channels     = 2;
    uint32_t     bufferFrames = DEFAULT_BUFFER_FRAMES;
    uint32_t     ringFrames   = DEFAULT_RING_FRAMES;
    bool         debugLog     = false;
};

struct EngineStats {
    BridgeStatus status          = BridgeStatus::Idle;
    uint64_t     totalFrames     = 0;
    uint64_t     underruns       = 0;
    uint64_t     overruns        = 0;
    float        levelL          = 0.0f;
    float        levelR          = 0.0f;
    double       latencyMs       = 0.0;
    std::string  captureFormat;
    std::string  outputFormat;
    std::chrono::seconds uptime  { 0 };
};

class AudioEngine {
public:
    AudioEngine();
    ~AudioEngine();

    bool configure(const EngineConfig& config);
    bool start();
    void stop();

    BridgeStatus status() const { return m_status.load(); }
    EngineStats  getStats() const;

    void setStatusCallback(StatusCallback cb) { m_statusCb = cb; }
    void setLevelCallback(LevelCallback cb)   { m_levelCb  = cb; }
    void setErrorCallback(ErrorCallback cb)   { m_errorCb  = cb; }

    DeviceEnumerator& enumerator() { return m_enumerator; }

private:
    void onError(const std::string& msg);
    void updateStatus(BridgeStatus s, const std::string& msg = "");

    EngineConfig   m_config;
    DeviceEnumerator m_enumerator;

    std::shared_ptr<RingBuffer> m_ringBuffer;
    std::unique_ptr<WasapiCapture> m_capture;
    std::unique_ptr<AsioOutput>    m_output;

    std::atomic<BridgeStatus> m_status { BridgeStatus::Idle };
    std::chrono::steady_clock::time_point m_startTime;

    StatusCallback m_statusCb;
    LevelCallback  m_levelCb;
    ErrorCallback  m_errorCb;
};

} // namespace Bridge
