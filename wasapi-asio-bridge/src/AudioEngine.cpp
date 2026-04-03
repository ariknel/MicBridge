#include "AudioEngine.h"
#include "Logger.h"
#include <stdexcept>

namespace Bridge {

AudioEngine::AudioEngine() {
    LOG_INFO("AudioEngine v" + std::string(VERSION_STR) + " init");
}

AudioEngine::~AudioEngine() {
    stop();
}

bool AudioEngine::configure(const EngineConfig& config) {
    stop();
    m_config = config;

    // Safety defaults - prevent zero-value crashes
    if (m_config.sampleRate   == 0) m_config.sampleRate   = 48000;
    if (m_config.channels     == 0) m_config.channels     = 2;
    if (m_config.ringFrames   == 0) m_config.ringFrames   = m_config.sampleRate / 5;
    if (m_config.bufferFrames == 0) m_config.bufferFrames = m_config.sampleRate / 100;

    if (config.debugLog) Logger::get().setLevel(LogLevel::Debug);

    LOG_INFO("Engine config: rate=" + std::to_string(config.sampleRate) +
             " ch=" + std::to_string(config.channels) +
             " ring=" + std::to_string(config.ringFrames) + " frames");
    return true;
}

bool AudioEngine::start() {
    if (m_status.load() == BridgeStatus::Running) return true;
    updateStatus(BridgeStatus::Starting, "Initializing...");

    // -- Ring buffer ----------------------------------------------------------
    size_t frameBytes = m_config.channels * sizeof(float);
    m_ringBuffer = std::make_shared<RingBuffer>(m_config.ringFrames, frameBytes);

    // -- WASAPI Capture -------------------------------------------------------
    m_capture = std::make_unique<WasapiCapture>(m_ringBuffer);
    m_capture->setErrorCallback([this](const std::string& msg) { onError(msg); });

    if (!m_capture->open(m_config.captureDeviceId,
                         m_config.sampleRate,
                         m_config.channels)) {
        updateStatus(BridgeStatus::NoDevice, "Failed to open capture device");
        m_capture.reset();
        return false;
    }
    LOG_INFO("Capture format: " + m_capture->captureFormat().toString());

    // -- WASAPI Output (render to virtual device) -----------------------------
    m_output = std::make_unique<AsioOutput>(m_ringBuffer);
    m_output->setErrorCallback([this](const std::string& msg) { onError(msg); });

    if (!m_output->open(m_config.outputDeviceId,
                        m_config.sampleRate,
                        m_config.channels)) {
        updateStatus(BridgeStatus::Error, "Failed to open output device");
        m_capture.reset();
        m_output.reset();
        return false;
    }
    LOG_INFO("Output format: " + m_output->outputFormat().toString());

    // -- Start threads --------------------------------------------------------
    m_startTime = std::chrono::steady_clock::now();

    m_output->start();
    m_capture->start();

    updateStatus(BridgeStatus::Running, "Bridge running");
    LOG_INFO("Bridge started OK");
    return true;
}

void AudioEngine::stop() {
    if (m_status.load() == BridgeStatus::Idle) return;
    updateStatus(BridgeStatus::Stopping, "Stopping...");

    if (m_capture) { m_capture->stop(); m_capture.reset(); }
    if (m_output)  { m_output->stop();  m_output.reset();  }
    m_ringBuffer.reset();

    updateStatus(BridgeStatus::Idle, "Stopped");
}

EngineStats AudioEngine::getStats() const {
    EngineStats stats;
    stats.status = m_status.load();

    if (m_capture) {
        stats.totalFrames  = m_capture->totalFramesCaptured();
        stats.levelL       = m_capture->levelL();
        stats.levelR       = m_capture->levelR();
        stats.captureFormat = m_capture->captureFormat().toString();
    }
    if (m_output) {
        stats.outputFormat = m_output->outputFormat().toString();
    }
    if (m_ringBuffer) {
        stats.underruns = m_ringBuffer->underruns();
        stats.overruns  = m_ringBuffer->overruns();

        // Latency = ring buffer fill time
        size_t fill = m_ringBuffer->availableRead();
        uint32_t sr = m_config.sampleRate > 0 ? m_config.sampleRate : 48000;
        stats.latencyMs = (fill * 1000.0) / sr;
    }

    if (m_status.load() == BridgeStatus::Running) {
        auto now  = std::chrono::steady_clock::now();
        stats.uptime = std::chrono::duration_cast<std::chrono::seconds>(now - m_startTime);
    }

    return stats;
}

void AudioEngine::onError(const std::string& msg) {
    LOG_ERROR("Engine error: " + msg);
    updateStatus(BridgeStatus::Error, msg);
    if (m_errorCb) m_errorCb(msg);
}

void AudioEngine::updateStatus(BridgeStatus s, const std::string& msg) {
    m_status.store(s);
    LOG_INFO("Status -> " + std::string(StatusToString(s)) +
             (msg.empty() ? "" : ": " + msg));
    if (m_statusCb) m_statusCb(s, msg);
}

} // namespace Bridge
