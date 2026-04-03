#include "DeviceEnumerator.h"
#include "Logger.h"
#include <functiondiscoverykeys_devpkey.h>
#include <audioclient.h>
#include <stdexcept>

#pragma comment(lib, "ole32.lib")

namespace Bridge {

DeviceEnumerator::DeviceEnumerator() {
    HRESULT hr = CoCreateInstance(
        __uuidof(MMDeviceEnumerator), nullptr,
        CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
        (void**)&m_enumerator);
    if (FAILED(hr)) throw std::runtime_error("Failed to create MMDeviceEnumerator");
}

DeviceEnumerator::~DeviceEnumerator() {
    if (m_enumerator) { m_enumerator->Release(); m_enumerator = nullptr; }
}

std::string DeviceEnumerator::wideToUtf8(const std::wstring& w) {
    if (w.empty()) return {};
    int sz = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string result(sz - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, result.data(), sz, nullptr, nullptr);
    return result;
}

std::vector<AudioDevice> DeviceEnumerator::enumerate(EDataFlow flow) {
    std::vector<AudioDevice> devices;

    IMMDeviceCollection* collection = nullptr;
    HRESULT hr = m_enumerator->EnumAudioEndpoints(flow, DEVICE_STATE_ACTIVE, &collection);
    if (FAILED(hr)) return devices;

    UINT count = 0;
    collection->GetCount(&count);

    // Get default device ID for marking
    IMMDevice* defDevice = nullptr;
    std::wstring defId;
    if (SUCCEEDED(m_enumerator->GetDefaultAudioEndpoint(flow, eConsole, &defDevice))) {
        LPWSTR id = nullptr;
        if (SUCCEEDED(defDevice->GetId(&id))) {
            defId = id;
            CoTaskMemFree(id);
        }
        defDevice->Release();
    }

    for (UINT i = 0; i < count; ++i) {
        IMMDevice* device = nullptr;
        if (FAILED(collection->Item(i, &device))) continue;

        AudioDevice ad;

        // Device ID
        LPWSTR deviceId = nullptr;
        if (SUCCEEDED(device->GetId(&deviceId))) {
            ad.id = deviceId;
            CoTaskMemFree(deviceId);
        }
        ad.isDefault = (ad.id == defId);

        // Friendly name
        IPropertyStore* props = nullptr;
        if (SUCCEEDED(device->OpenPropertyStore(STGM_READ, &props))) {
            PROPVARIANT pv;
            PropVariantInit(&pv);
            if (SUCCEEDED(props->GetValue(PKEY_Device_FriendlyName, &pv)) &&
                pv.vt == VT_LPWSTR) {
                ad.name = wideToUtf8(pv.pwszVal);
            }
            PropVariantClear(&pv);
            props->Release();
        }
        if (ad.name.empty()) ad.name = "(Unknown Device)";

        // Native format
        IAudioClient* client = nullptr;
        if (SUCCEEDED(device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**)&client))) {
            WAVEFORMATEX* wfx = nullptr;
            if (SUCCEEDED(client->GetMixFormat(&wfx))) {
                ad.nativeSampleRate = wfx->nSamplesPerSec;
                ad.nativeChannels   = wfx->nChannels;
                CoTaskMemFree(wfx);
            }
            client->Release();
        }

        devices.push_back(std::move(ad));
        device->Release();
    }

    collection->Release();
    return devices;
}

std::vector<AudioDevice> DeviceEnumerator::getCaptureDevices() {
    return enumerate(eCapture);
}

std::vector<AudioDevice> DeviceEnumerator::getRenderDevices() {
    return enumerate(eRender);
}

AudioDevice DeviceEnumerator::getDefaultCaptureDevice() {
    auto devices = getCaptureDevices();
    for (auto& d : devices) {
        if (d.isDefault) return d;
    }
    if (!devices.empty()) return devices[0];
    return {};
}

IMMDevice* DeviceEnumerator::openDevice(const std::wstring& id) {
    IMMDevice* device = nullptr;
    m_enumerator->GetDevice(id.c_str(), &device);
    return device;
}

void DeviceEnumerator::refresh() {
    // Enumerator is stateless -- no-op needed
}

} // namespace Bridge
