// bridge_gui.exe -- WASAPI->ASIO Bridge GUI
#include "MainWindow.h"
#include "AudioEngine.h"
#include "Logger.h"
#include <windows.h>
#include <objbase.h>
#include <string>

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int) {
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    if (FAILED(hr)) {
        MessageBoxW(nullptr, L"COM initialization failed.", L"Bridge Error", MB_ICONERROR);
        return 1;
    }

    // Log to file beside the exe
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    std::wstring logPath = exePath;
    auto pos = logPath.rfind(L'\\');
    if (pos != std::wstring::npos) logPath = logPath.substr(0, pos + 1);
    logPath += L"bridge.log";

    int sz = WideCharToMultiByte(CP_UTF8, 0, logPath.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string logPathA(sz - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, logPath.c_str(), -1, logPathA.data(), sz, nullptr, nullptr);
    Bridge::Logger::get().setFileOutput(logPathA);
    Bridge::Logger::get().info("bridge_gui started");

    Bridge::MainWindow win;
    if (!win.create(hInst)) {
        MessageBoxW(nullptr, L"Failed to create main window.", L"Bridge Error", MB_ICONERROR);
        CoUninitialize();
        return 1;
    }

    int result = win.run();
    CoUninitialize();
    return result;
}
