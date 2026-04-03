#pragma once
#include <windows.h>
#include <mmdeviceapi.h>
#include <string>
#include <vector>

namespace Bridge {

struct AudioDevice {
    std::wstring id;           // Device ID (COM)
    std::string  name;         // Friendly name (UTF-8)
    bool         isDefault = false;
    uint32_t     nativeChannels   = 2;
    uint32_t     nativeSampleRate = 48000;
};

class DeviceEnumerator {
public:
    DeviceEnumerator();
    ~DeviceEnumerator();

    // List capture (input) devices
    std::vector<AudioDevice> getCaptureDevices();

    // List render (output) devices
    std::vector<AudioDevice> getRenderDevices();

    // Get default capture device
    AudioDevice getDefaultCaptureDevice();

    // Get IMMDevice by id (caller releases)
    IMMDevice* openDevice(const std::wstring& id);

    // Refresh
    void refresh();

private:
    IMMDeviceEnumerator* m_enumerator = nullptr;

    std::vector<AudioDevice> enumerate(EDataFlow flow);
    static std::string wideToUtf8(const std::wstring& w);
};

} // namespace Bridge
