// bridge.exe -- WASAPI->ASIO Bridge CLI
// Usage: bridge [options]
//   --list-inputs        List capture devices
//   --list-outputs       List render (output) devices
//   --input  <index|id>  Select capture device
//   --output <index|id>  Select render/output device
//   --rate   <hz>        Sample rate (default 48000)
//   --buffer <frames>    Buffer size (default 480)
//   --debug              Enable debug logging
//   --log    <file>      Log to file

#include "AudioEngine.h"
#include "DeviceEnumerator.h"
#include "Logger.h"
#include "Common.h"
#include <windows.h>
#include <objbase.h>

#include <iostream>
#include <string>
#include <vector>
#include <cstring>
#include <csignal>
#include <algorithm>
#include <thread>
#include <chrono>
#include <iomanip>

using namespace Bridge;

// -- Signal handler ------------------------------------------------------------
static std::atomic<bool> g_running { true };

static void sigHandler(int) {
    g_running.store(false);
    std::cout << "\nShutting down...\n";
}

// -- Arg helpers ---------------------------------------------------------------
static const char* argVal(int argc, char* argv[], const char* flag) {
    for (int i = 1; i < argc - 1; ++i)
        if (strcmp(argv[i], flag) == 0) return argv[i + 1];
    return nullptr;
}

static bool argFlag(int argc, char* argv[], const char* flag) {
    for (int i = 1; i < argc; ++i)
        if (strcmp(argv[i], flag) == 0) return true;
    return false;
}

// -- Device listing ------------------------------------------------------------
static void listDevices(DeviceEnumerator& enumerator, bool inputs, bool outputs) {
    if (inputs || (!inputs && !outputs)) {
        auto devices = enumerator.getCaptureDevices();
        std::cout << "\n=== Capture Devices (Input) ===\n";
        for (size_t i = 0; i < devices.size(); ++i) {
            auto& d = devices[i];
            std::cout << "  [" << i << "] " << d.name
                      << " (" << d.nativeSampleRate << "Hz / "
                      << d.nativeChannels << "ch)"
                      << (d.isDefault ? " [DEFAULT]" : "") << "\n";
        }
    }

    if (outputs || (!inputs && !outputs)) {
        auto devices = enumerator.getRenderDevices();
        std::cout << "\n=== Render Devices (Output) ===\n";
        for (size_t i = 0; i < devices.size(); ++i) {
            auto& d = devices[i];
            std::cout << "  [" << i << "] " << d.name
                      << " (" << d.nativeSampleRate << "Hz / "
                      << d.nativeChannels << "ch)"
                      << (d.isDefault ? " [DEFAULT]" : "") << "\n";
        }
    }
    std::cout << "\n";
}

// -- Status monitor ------------------------------------------------------------
static void printStats(const EngineStats& stats) {
    auto sec = stats.uptime.count();
    std::cout << "\r[" << StatusToString(stats.status) << "] "
              << "uptime=" << (sec/60) << "m" << (sec%60) << "s  "
              << "frames=" << stats.totalFrames << "  "
              << "underruns=" << stats.underruns << "  "
              << "latency=" << std::fixed << std::setprecision(1) << stats.latencyMs << "ms  "
              << "L=" << std::setprecision(3) << stats.levelL
              << " R=" << stats.levelR
              << "   " << std::flush;
}

// -- Main ----------------------------------------------------------------------
int main(int argc, char* argv[]) {
    // Console UTF-8
    SetConsoleOutputCP(CP_UTF8);

    // COM
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(hr)) {
        std::cerr << "COM init failed\n";
        return 1;
    }

    std::signal(SIGINT,  sigHandler);
    std::signal(SIGTERM, sigHandler);

    bool listInputs  = argFlag(argc, argv, "--list-inputs");
    bool listOutputs = argFlag(argc, argv, "--list-outputs");
    bool listAll     = argFlag(argc, argv, "--list");
    bool debug       = argFlag(argc, argv, "--debug");

    const char* inputArg   = argVal(argc, argv, "--input");
    const char* outputArg  = argVal(argc, argv, "--output");
    const char* rateArg    = argVal(argc, argv, "--rate");
    const char* bufArg     = argVal(argc, argv, "--buffer");
    const char* logArg     = argVal(argc, argv, "--log");

    if (debug) Logger::get().setLevel(LogLevel::Debug);
    if (logArg) Logger::get().setFileOutput(logArg);

    std::cout << "WASAPI->ASIO Bridge v" << VERSION_STR << "\n";

    AudioEngine engine;
    DeviceEnumerator& enumerator = engine.enumerator();

    if (listInputs || listOutputs || listAll) {
        listDevices(enumerator, listInputs || listAll, listOutputs || listAll);
        CoUninitialize();
        return 0;
    }

    if (argc < 2) {
        std::cout << "Usage:\n"
                  << "  bridge --list                  List all devices\n"
                  << "  bridge --list-inputs           List capture devices\n"
                  << "  bridge --list-outputs          List render devices\n"
                  << "  bridge --input <index>         Select input (by list index)\n"
                  << "  bridge --output <index>        Select output (by list index)\n"
                  << "  bridge --rate <hz>             Sample rate (default 48000)\n"
                  << "  bridge --buffer <frames>       Buffer frames (default 480)\n"
                  << "  bridge --debug                 Verbose logging\n"
                  << "  bridge --log <file>            Log to file\n"
                  << "\nExample:\n"
                  << "  bridge --list\n"
                  << "  bridge --input 0 --output 2 --rate 48000\n";
        CoUninitialize();
        return 0;
    }

    // -- Build config ----------------------------------------------------------
    EngineConfig config;
    config.debugLog  = debug;
    config.sampleRate = rateArg  ? (uint32_t)std::stoul(rateArg) : DEFAULT_SAMPLE_RATE;
    config.bufferFrames = bufArg ? (uint32_t)std::stoul(bufArg)  : DEFAULT_BUFFER_FRAMES;
    config.channels  = 2;
    config.ringFrames = config.sampleRate / 5;  // 200ms ring

    // Resolve input device
    if (inputArg) {
        auto inputs = enumerator.getCaptureDevices();
        try {
            size_t idx = std::stoul(inputArg);
            if (idx < inputs.size()) {
                config.captureDeviceId = inputs[idx].id;
                std::cout << "Input: " << inputs[idx].name << "\n";
            } else {
                std::cerr << "Input index out of range. Use --list-inputs.\n";
                CoUninitialize(); return 1;
            }
        } catch (...) {
            // Treat as device ID string
            config.captureDeviceId = std::wstring(inputArg, inputArg + strlen(inputArg));
        }
    } else {
        std::cout << "No --input specified; using default capture device.\n";
    }

    // Resolve output device
    if (outputArg) {
        auto outputs = enumerator.getRenderDevices();
        try {
            size_t idx = std::stoul(outputArg);
            if (idx < outputs.size()) {
                config.outputDeviceId = outputs[idx].id;
                std::cout << "Output: " << outputs[idx].name << "\n";
            } else {
                std::cerr << "Output index out of range. Use --list-outputs.\n";
                CoUninitialize(); return 1;
            }
        } catch (...) {
            config.outputDeviceId = std::wstring(outputArg, outputArg + strlen(outputArg));
        }
    } else {
        std::cout << "No --output specified; using default render device.\n";
        std::cout << "TIP: For FlexASIO/DAW routing, use --output with VB-Cable or similar.\n";
    }

    // -- Start engine ----------------------------------------------------------
    engine.configure(config);
    engine.setStatusCallback([](BridgeStatus s, const std::string& msg) {
        std::cout << "\n[STATUS] " << StatusToString(s)
                  << (msg.empty() ? "" : ": " + msg) << "\n";
    });

    if (!engine.start()) {
        std::cerr << "Failed to start bridge. Check device selection.\n";
        CoUninitialize();
        return 1;
    }

    std::cout << "\nBridge running. Press Ctrl+C to stop.\n";
    std::cout << "Stats: uptime | frames | underruns | latency | levels\n";

    while (g_running.load()) {
        printStats(engine.getStats());
        std::this_thread::sleep_for(std::chrono::milliseconds(200));

        // Auto-detect underrun issues
        auto stats = engine.getStats();
        if (stats.status == BridgeStatus::Error) {
            std::cerr << "\nEngine error -- stopping.\n";
            break;
        }
    }

    std::cout << "\nStopping bridge...\n";
    engine.stop();

    auto stats = engine.getStats();
    std::cout << "\n=== Session Summary ===\n"
              << "Total frames:  " << stats.totalFrames << "\n"
              << "Underruns:     " << stats.underruns << "\n"
              << "Overruns:      " << stats.overruns << "\n"
              << "Capture format:" << stats.captureFormat << "\n"
              << "Output format: " << stats.outputFormat << "\n";

    CoUninitialize();
    return 0;
}
