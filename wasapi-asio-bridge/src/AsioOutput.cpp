#include "AsioOutput.h"
#include <stdexcept>
#include <algorithm>
#include <cstring>

#pragma comment(lib, "avrt.lib")

namespace Bridge {

AsioOutput::AsioOutput(std::shared_ptr<RingBuffer> ringBuffer)
    : m_ringBuffer(std::move(ringBuffer))
{
}

AsioOutput::~AsioOutput() {
    stop();
    if (m_audioClient)  { m_audioClient->Release();  m_audioClient  = nullptr; }
    if (m_renderClient) { m_renderClient->Release(); m_renderClient = nullptr; }
    if (m_eventHandle)  { CloseHandle(m_eventHandle); m_eventHandle = nullptr; }
}

bool AsioOutput::open(const std::wstring& deviceId, uint32_t sampleRate, uint32_t channels) {
    IMMDeviceEnumerator* enumerator = nullptr;
    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr,
                                  CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
                                  (void**)&enumerator);
    if (FAILED(hr)) { LOG_ERROR("Output: DeviceEnum failed: " + HrToString(hr)); return false; }

    IMMDevice* device = nullptr;
    if (deviceId.empty()) {
        hr = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device);
    } else {
        hr = enumerator->GetDevice(deviceId.c_str(), &device);
    }
    enumerator->Release();

    if (FAILED(hr) || !device) {
        LOG_ERROR("Output: failed to get render device: " + HrToString(hr));
        return false;
    }

    hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**)&m_audioClient);
    device->Release();
    if (FAILED(hr)) { LOG_ERROR("Output: AudioClient activate failed: " + HrToString(hr)); return false; }

    // Get mix format
    WAVEFORMATEX* wfx = nullptr;
    hr = m_audioClient->GetMixFormat(&wfx);
    if (FAILED(hr)) { LOG_ERROR("Output: GetMixFormat failed: " + HrToString(hr)); return false; }

    // Force our target rate/channels if possible; fall back to device defaults
    wfx->nSamplesPerSec = sampleRate > 0 ? sampleRate : wfx->nSamplesPerSec;
    wfx->nChannels      = channels > 0 ? (WORD)channels : wfx->nChannels;
    wfx->nBlockAlign    = wfx->nChannels * (wfx->wBitsPerSample / 8);
    wfx->nAvgBytesPerSec = wfx->nSamplesPerSec * wfx->nBlockAlign;

    m_outputFormat.sampleRate  = wfx->nSamplesPerSec;
    m_outputFormat.channels    = wfx->nChannels;
    m_outputFormat.format      = SampleFormat::Float32;
    m_outputFormat.bitsPerSample = 32;

    m_eventHandle = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!m_eventHandle) { CoTaskMemFree(wfx); return false; }

    REFERENCE_TIME bufDuration = 100000; // 10ms
    hr = m_audioClient->Initialize(
        AUDCLNT_SHAREMODE_SHARED,
        AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
        bufDuration, 0, wfx, nullptr);
    CoTaskMemFree(wfx);

    if (FAILED(hr)) { LOG_ERROR("Output: Initialize failed: " + HrToString(hr)); return false; }

    hr = m_audioClient->SetEventHandle(m_eventHandle);
    if (FAILED(hr)) { LOG_ERROR("Output: SetEventHandle failed: " + HrToString(hr)); return false; }

    hr = m_audioClient->GetBufferSize(&m_bufferFrames);
    if (FAILED(hr)) { LOG_ERROR("Output: GetBufferSize failed: " + HrToString(hr)); return false; }

    hr = m_audioClient->GetService(__uuidof(IAudioRenderClient), (void**)&m_renderClient);
    if (FAILED(hr)) { LOG_ERROR("Output: GetService RenderClient failed: " + HrToString(hr)); return false; }

    m_writeBuf.resize(m_bufferFrames * m_outputFormat.channels, 0.0f);

    LOG_INFO("Output render device: " + m_outputFormat.toString() +
             ", buffer=" + std::to_string(m_bufferFrames) + " frames");
    return true;
}

void AsioOutput::start() {
    if (m_running.load()) return;
    m_stopFlag.store(false);
    m_running.store(true);

    HRESULT hr = m_audioClient->Start();
    if (FAILED(hr)) {
        LOG_ERROR("Output: AudioClient Start failed: " + HrToString(hr));
        m_running.store(false);
        return;
    }

    m_thread = std::thread(&AsioOutput::renderThread, this);
    LOG_INFO("Render thread started");
}

void AsioOutput::stop() {
    if (!m_running.load()) return;
    m_stopFlag.store(true);
    if (m_eventHandle) SetEvent(m_eventHandle);
    if (m_thread.joinable()) m_thread.join();
    if (m_audioClient) m_audioClient->Stop();
    m_running.store(false);
    LOG_INFO("Render stopped");
}

void AsioOutput::renderThread() {
    DWORD taskIdx = 0;
    HANDLE hTask = AvSetMmThreadCharacteristicsW(L"Pro Audio", &taskIdx);
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);

    while (!m_stopFlag.load(std::memory_order_relaxed)) {
        DWORD waitResult = WaitForSingleObject(m_eventHandle, 200);
        if (waitResult == WAIT_TIMEOUT) continue;
        if (waitResult != WAIT_OBJECT_0) break;
        if (m_stopFlag.load(std::memory_order_relaxed)) break;

        UINT32 paddingFrames = 0;
        HRESULT hr = m_audioClient->GetCurrentPadding(&paddingFrames);
        if (FAILED(hr)) { LOG_WARN("Output: GetCurrentPadding failed"); continue; }

        UINT32 availFrames = m_bufferFrames - paddingFrames;
        if (availFrames == 0) continue;

        BYTE* buf = nullptr;
        hr = m_renderClient->GetBuffer(availFrames, &buf);
        if (FAILED(hr)) { LOG_WARN("Output: GetBuffer failed"); continue; }

        // Read from ring buffer (fills with silence on underrun)
        if (m_writeBuf.size() < (size_t)availFrames * m_outputFormat.channels)
            m_writeBuf.resize((size_t)availFrames * m_outputFormat.channels * 2);

        size_t read = m_ringBuffer->read(m_writeBuf.data(), availFrames);
        std::memcpy(buf, m_writeBuf.data(), availFrames * m_outputFormat.channels * sizeof(float));

        hr = m_renderClient->ReleaseBuffer(availFrames, 0);
        if (FAILED(hr)) LOG_WARN("Output: ReleaseBuffer failed");

        // Log underruns periodically
        static uint64_t lastUnderrun = 0;
        uint64_t now = m_ringBuffer->underruns();
        if (now != lastUnderrun) {
            LOG_WARN("Ring buffer underrun #" + std::to_string(now) +
                     " (DAW reading faster than capture)");
            lastUnderrun = now;
        }
    }

    if (hTask) AvRevertMmThreadCharacteristics(hTask);
}

} // namespace Bridge
