#pragma once
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <avrt.h>

#include "AudioFormat.h"
#include "RingBuffer.h"
#include "Resampler.h"
#include "Logger.h"
#include "Common.h"

#include <atomic>
#include <thread>
#include <functional>
#include <string>
#include <memory>

namespace Bridge {

// -----------------------------------------------------------------------------
// WasapiCapture: captures audio from an input device in WASAPI shared mode.
// Runs a dedicated high-priority capture thread.
// Converts captured audio to float32 and writes to a RingBuffer.
// -----------------------------------------------------------------------------
class WasapiCapture {
public:
    explicit WasapiCapture(std::shared_ptr<RingBuffer> ringBuffer);
    ~WasapiCapture();

    // Open a device (by ID). If deviceId is empty, uses default.
    // targetRate: desired sample rate (will resample if device differs).
    bool open(const std::wstring& deviceId, uint32_t targetRate, uint32_t targetChannels);

    void start();
    void stop();

    bool isRunning() const { return m_running.load(); }

    AudioFormat captureFormat() const { return m_captureFormat; }

    // RMS levels for metering (updated every callback)
    float levelL() const { return m_levelL.load(std::memory_order_relaxed); }
    float levelR() const { return m_levelR.load(std::memory_order_relaxed); }

    uint64_t totalFramesCaptured() const { return m_totalFrames.load(std::memory_order_relaxed); }

    void setErrorCallback(std::function<void(const std::string&)> cb) { m_errorCb = cb; }

private:
    void captureThread();
    bool setupWasapi(IMMDevice* device, uint32_t targetRate, uint32_t targetChannels);
    void processBuffer(const BYTE* data, uint32_t frames, bool silent);
    void convertToFloat32(const BYTE* src, float* dst, uint32_t frames);
    void computeLevels(const float* data, uint32_t frames, uint32_t channels);

    // COM objects
    IAudioClient*        m_audioClient   = nullptr;
    IAudioCaptureClient* m_captureClient = nullptr;

    std::shared_ptr<RingBuffer> m_ringBuffer;
    Resampler                   m_resampler;
    AudioFormat                 m_captureFormat;    // actual device format
    AudioFormat                 m_outputFormat;     // target format

    std::thread          m_thread;
    std::atomic<bool>    m_running   { false };
    std::atomic<bool>    m_stopFlag  { false };
    HANDLE               m_eventHandle = nullptr;

    std::atomic<float>   m_levelL { 0.0f };
    std::atomic<float>   m_levelR { 0.0f };
    std::atomic<uint64_t> m_totalFrames { 0 };

    // Temp conversion buffer
    std::vector<float>   m_convertBuf;
    std::vector<float>   m_resampleBuf;

    std::function<void(const std::string&)> m_errorCb;
};

} // namespace Bridge
