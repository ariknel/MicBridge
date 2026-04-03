#include "WasapiCapture.h"
#include <stdexcept>
#include <cmath>
#include <algorithm>
#include <initguid.h>
#include <mmdeviceapi.h>
#include <functiondiscoverykeys_devpkey.h>
#include <ksmedia.h>

#pragma comment(lib, "avrt.lib")
#pragma comment(lib, "ole32.lib")

namespace Bridge {

WasapiCapture::WasapiCapture(std::shared_ptr<RingBuffer> ringBuffer)
    : m_ringBuffer(std::move(ringBuffer))
{
}

WasapiCapture::~WasapiCapture() {
    stop();
    if (m_audioClient)   { m_audioClient->Release();   m_audioClient   = nullptr; }
    if (m_captureClient) { m_captureClient->Release();  m_captureClient = nullptr; }
    if (m_eventHandle)   { CloseHandle(m_eventHandle);  m_eventHandle   = nullptr; }
}

bool WasapiCapture::open(const std::wstring& deviceId, uint32_t targetRate, uint32_t targetChannels) {
    // Enumerate to find the device
    IMMDeviceEnumerator* enumerator = nullptr;
    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr,
                                  CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
                                  (void**)&enumerator);
    if (FAILED(hr)) { LOG_ERROR("DeviceEnum failed: " + HrToString(hr)); return false; }

    IMMDevice* device = nullptr;
    if (deviceId.empty()) {
        hr = enumerator->GetDefaultAudioEndpoint(eCapture, eConsole, &device);
    } else {
        hr = enumerator->GetDevice(deviceId.c_str(), &device);
    }
    enumerator->Release();

    if (FAILED(hr) || !device) {
        LOG_ERROR("Failed to get audio device: " + HrToString(hr));
        return false;
    }

    bool ok = setupWasapi(device, targetRate, targetChannels);
    device->Release();
    return ok;
}

bool WasapiCapture::setupWasapi(IMMDevice* device, uint32_t targetRate, uint32_t targetChannels) {
    // Activate AudioClient
    HRESULT hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                                  (void**)&m_audioClient);
    if (FAILED(hr)) { LOG_ERROR("AudioClient activate failed: " + HrToString(hr)); return false; }

    // Get mix format (WASAPI shared)
    WAVEFORMATEX* wfx = nullptr;
    hr = m_audioClient->GetMixFormat(&wfx);
    if (FAILED(hr)) { LOG_ERROR("GetMixFormat failed: " + HrToString(hr)); return false; }

    // Parse capture format
    m_captureFormat.sampleRate = wfx->nSamplesPerSec;
    m_captureFormat.channels   = wfx->nChannels;

    // Check for extensible format (common for float)
    if (wfx->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
        auto* wfxEx = reinterpret_cast<WAVEFORMATEXTENSIBLE*>(wfx);
        if (wfxEx->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT) {
            m_captureFormat.format        = SampleFormat::Float32;
            m_captureFormat.bitsPerSample = 32;
        } else if (wfxEx->SubFormat == KSDATAFORMAT_SUBTYPE_PCM) {
            m_captureFormat.bitsPerSample = wfx->wBitsPerSample;
            m_captureFormat.format = (wfx->wBitsPerSample == 16) ? SampleFormat::Int16
                                   : (wfx->wBitsPerSample == 24) ? SampleFormat::Int24
                                                                  : SampleFormat::Int32;
        }
    } else if (wfx->wFormatTag == WAVE_FORMAT_IEEE_FLOAT) {
        m_captureFormat.format = SampleFormat::Float32;
        m_captureFormat.bitsPerSample = 32;
    } else {
        m_captureFormat.format = SampleFormat::Int16;
        m_captureFormat.bitsPerSample = wfx->wBitsPerSample;
    }

    LOG_INFO("Capture device format: " + m_captureFormat.toString());

    // Event handle for callback mode
    m_eventHandle = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!m_eventHandle) { CoTaskMemFree(wfx); return false; }

    // Buffer duration: 10ms
    REFERENCE_TIME bufDuration = 100000; // 10ms in 100ns units

    hr = m_audioClient->Initialize(
        AUDCLNT_SHAREMODE_SHARED,
        AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_NOPERSIST,
        bufDuration, 0, wfx, nullptr);
    CoTaskMemFree(wfx);

    if (FAILED(hr)) { LOG_ERROR("AudioClient Initialize failed: " + HrToString(hr)); return false; }

    hr = m_audioClient->SetEventHandle(m_eventHandle);
    if (FAILED(hr)) { LOG_ERROR("SetEventHandle failed: " + HrToString(hr)); return false; }

    hr = m_audioClient->GetService(__uuidof(IAudioCaptureClient), (void**)&m_captureClient);
    if (FAILED(hr)) { LOG_ERROR("GetService CaptureClient failed: " + HrToString(hr)); return false; }

    // Set up output format
    m_outputFormat.sampleRate  = (targetRate != 0) ? targetRate : m_captureFormat.sampleRate;
    m_outputFormat.channels    = (targetChannels != 0) ? targetChannels : m_captureFormat.channels;
    m_outputFormat.format      = SampleFormat::Float32;
    m_outputFormat.bitsPerSample = 32;

    // Configure resampler
    m_resampler.configure(m_captureFormat.sampleRate, m_outputFormat.sampleRate,
                          m_captureFormat.channels);

    if (!m_resampler.isPassthrough()) {
        LOG_INFO("Resampling: " + std::to_string(m_captureFormat.sampleRate) +
                 " -> " + std::to_string(m_outputFormat.sampleRate));
    }

    // Pre-allocate conversion buffers
    m_convertBuf.resize(4096 * m_captureFormat.channels, 0.0f);
    m_resampleBuf.resize(4096 * m_captureFormat.channels, 0.0f);

    LOG_INFO("WASAPI capture initialized OK");
    return true;
}

void WasapiCapture::start() {
    if (m_running.load()) return;
    m_stopFlag.store(false);
    m_running.store(true);

    HRESULT hr = m_audioClient->Start();
    if (FAILED(hr)) {
        LOG_ERROR("AudioClient Start failed: " + HrToString(hr));
        m_running.store(false);
        return;
    }

    m_thread = std::thread(&WasapiCapture::captureThread, this);
    LOG_INFO("Capture thread started");
}

void WasapiCapture::stop() {
    if (!m_running.load()) return;
    m_stopFlag.store(true);
    if (m_eventHandle) SetEvent(m_eventHandle);  // wake thread
    if (m_thread.joinable()) m_thread.join();
    if (m_audioClient) m_audioClient->Stop();
    m_running.store(false);
    LOG_INFO("Capture stopped. Total frames: " + std::to_string(m_totalFrames.load()));
}

void WasapiCapture::captureThread() {
    // Elevate thread priority to Pro Audio
    DWORD taskIdx = 0;
    HANDLE hTask = AvSetMmThreadCharacteristicsW(L"Pro Audio", &taskIdx);
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);

    LOG_INFO("Capture thread priority set");

    while (!m_stopFlag.load(std::memory_order_relaxed)) {
        DWORD waitResult = WaitForSingleObject(m_eventHandle, 200);
        if (waitResult == WAIT_TIMEOUT) continue;
        if (waitResult != WAIT_OBJECT_0) break;
        if (m_stopFlag.load(std::memory_order_relaxed)) break;

        // Drain all available packets
        UINT32 packetSize = 0;
        while (SUCCEEDED(m_captureClient->GetNextPacketSize(&packetSize)) && packetSize > 0) {
            BYTE*  data      = nullptr;
            UINT32 frames    = 0;
            DWORD  flags     = 0;

            HRESULT hr = m_captureClient->GetBuffer(&data, &frames, &flags, nullptr, nullptr);
            if (FAILED(hr)) break;

            bool silent = (flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0;
            processBuffer(data, frames, silent);
            m_captureClient->ReleaseBuffer(frames);
        }
    }

    if (hTask) AvRevertMmThreadCharacteristics(hTask);
}

void WasapiCapture::processBuffer(const BYTE* data, uint32_t frames, bool silent) {
    if (frames == 0) return;

    // Ensure buffer is large enough
    size_t needed = static_cast<size_t>(frames) * m_captureFormat.channels;
    if (m_convertBuf.size() < needed) m_convertBuf.resize(needed * 2);

    float* fBuf = m_convertBuf.data();

    if (silent) {
        std::fill(fBuf, fBuf + needed, 0.0f);
    } else {
        convertToFloat32(data, fBuf, frames);
    }

    // Level metering (before resampling -- same data, just for display)
    computeLevels(fBuf, frames, m_captureFormat.channels);

    // Resample if needed
    const float* toWrite = fBuf;
    size_t       outFrames = frames;

    if (!m_resampler.isPassthrough()) {
        size_t maxOut = static_cast<size_t>(frames * m_resampler.dstRate() / m_resampler.srcRate() + 8);
        if (m_resampleBuf.size() < maxOut * m_captureFormat.channels)
            m_resampleBuf.resize(maxOut * m_captureFormat.channels * 2);

        outFrames = m_resampler.process(fBuf, frames,
                                        m_resampleBuf.data(), maxOut);
        toWrite = m_resampleBuf.data();
    }

    // Write to ring buffer
    size_t written = m_ringBuffer->write(toWrite, outFrames);
    m_totalFrames.fetch_add(written, std::memory_order_relaxed);

    if (m_ringBuffer->overruns() > 0) {
        // Overrun: ring buffer full -- DAW not reading fast enough
        static uint64_t lastOverrun = 0;
        uint64_t now = m_ringBuffer->overruns();
        if (now != lastOverrun) {
            LOG_WARN("Ring buffer overrun #" + std::to_string(now));
            lastOverrun = now;
        }
    }
}

void WasapiCapture::convertToFloat32(const BYTE* src, float* dst, uint32_t frames) {
    uint32_t samples = frames * m_captureFormat.channels;

    switch (m_captureFormat.format) {
        case SampleFormat::Float32:
            std::memcpy(dst, src, samples * sizeof(float));
            break;

        case SampleFormat::Int16: {
            const int16_t* s = reinterpret_cast<const int16_t*>(src);
            for (uint32_t i = 0; i < samples; ++i)
                dst[i] = static_cast<float>(s[i]) / 32768.0f;
            break;
        }

        case SampleFormat::Int24: {
            // Packed 24-bit (3 bytes per sample, little-endian)
            const uint8_t* s = src;
            for (uint32_t i = 0; i < samples; ++i) {
                int32_t v = (s[0]) | (s[1] << 8) | (s[2] << 16);
                if (v & 0x800000) v |= 0xFF000000;  // sign extend
                dst[i] = static_cast<float>(v) / 8388608.0f;
                s += 3;
            }
            break;
        }

        case SampleFormat::Int32: {
            const int32_t* s = reinterpret_cast<const int32_t*>(src);
            for (uint32_t i = 0; i < samples; ++i)
                dst[i] = static_cast<float>(s[i]) / 2147483648.0f;
            break;
        }
    }
}

void WasapiCapture::computeLevels(const float* data, uint32_t frames, uint32_t channels) {
    if (frames == 0 || channels == 0) return;

    float sumL = 0.0f, sumR = 0.0f;
    for (uint32_t i = 0; i < frames; ++i) {
        float l = data[i * channels];
        float r = (channels > 1) ? data[i * channels + 1] : l;
        sumL += l * l;
        sumR += r * r;
    }

    float rmsL = std::sqrt(sumL / frames);
    float rmsR = std::sqrt(sumR / frames);

    // Smooth with simple leaky integrator
    constexpr float alpha = 0.1f;
    float oldL = m_levelL.load(std::memory_order_relaxed);
    float oldR = m_levelR.load(std::memory_order_relaxed);
    m_levelL.store(oldL + alpha * (rmsL - oldL), std::memory_order_relaxed);
    m_levelR.store(oldR + alpha * (rmsR - oldR), std::memory_order_relaxed);
}

} // namespace Bridge
