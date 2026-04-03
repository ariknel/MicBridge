#pragma once
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <avrt.h>

#include "AudioFormat.h"
#include "RingBuffer.h"
#include "Common.h"
#include "Logger.h"

#include <atomic>
#include <thread>
#include <functional>
#include <string>
#include <memory>
#include <vector>

namespace Bridge {

// -----------------------------------------------------------------------------
// AsioOutput: plays captured audio to a WASAPI render device.
//
// Architecture note:
//   FlexASIO exposes a virtual ASIO layer that wraps Windows audio devices.
//   The most reliable way to bridge WASAPI->ASIO without native ASIO SDK is:
//     1. Output to a virtual audio cable render device (e.g. VB-Cable, CABLE Input)
//     2. FlexASIO or the DAW picks up the loopback from that device
//
//   Alternatively, FlexASIO with the "WASAPI" backend can directly present
//   a render device as ASIO input in the DAW. The user routes our output to
//   whichever virtual device FlexASIO is configured to use.
//
//   This class renders to any selected WASAPI render endpoint in shared mode.
// -----------------------------------------------------------------------------
class AsioOutput {
public:
    explicit AsioOutput(std::shared_ptr<RingBuffer> ringBuffer);
    ~AsioOutput();

    // Open a render device. If deviceId is empty, uses default output.
    bool open(const std::wstring& deviceId, uint32_t sampleRate, uint32_t channels);

    void start();
    void stop();

    bool isRunning() const { return m_running.load(); }

    AudioFormat outputFormat() const { return m_outputFormat; }

    uint64_t underruns() const { return m_ringBuffer->underruns(); }

    void setErrorCallback(std::function<void(const std::string&)> cb) { m_errorCb = cb; }

private:
    void renderThread();

    IAudioClient*       m_audioClient   = nullptr;
    IAudioRenderClient* m_renderClient  = nullptr;
    HANDLE              m_eventHandle   = nullptr;
    UINT32              m_bufferFrames  = 0;

    std::shared_ptr<RingBuffer> m_ringBuffer;
    AudioFormat                 m_outputFormat;

    std::thread       m_thread;
    std::atomic<bool> m_running  { false };
    std::atomic<bool> m_stopFlag { false };

    std::function<void(const std::string&)> m_errorCb;

    // Temp write buffer
    std::vector<float> m_writeBuf;
};

} // namespace Bridge
