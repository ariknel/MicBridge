#pragma once
#include <windows.h>
#include <commctrl.h>
#include <dwmapi.h>
#include <string>
#include <vector>
#include <memory>
#include <atomic>
#include <array>
#include "AudioEngine.h"
#include "DeviceEnumerator.h"

namespace Bridge {

// Control IDs
#define IDC_COMBO_INPUT      101
#define IDC_COMBO_RATE       103
#define IDC_BTN_STARTSTOP    104
#define IDC_BTN_REFRESH      105
#define IDC_TIMER_METER      200
#define IDC_TIMER_STATS      201

// Exact colors from the screenshot
namespace Theme {
    static const COLORREF BG         = RGB(13,  15,  26 );  // #0d0f1a - main bg
    static const COLORREF SURFACE    = RGB(19,  22,  36 );  // #131624 - card/dropdown bg
    static const COLORREF SURFACE2   = RGB(23,  27,  45 );  // slightly lighter cards
    static const COLORREF BORDER     = RGB(40,  44,  70 );  // subtle border
    static const COLORREF TEXT       = RGB(220, 222, 240);  // primary text
    static const COLORREF TEXT_DIM   = RGB(90,  95,  130);  // dim labels
    static const COLORREF TEXT_LABEL = RGB(80,  85,  120);  // section labels (uppercase)
    static const COLORREF GREEN_PILL = RGB(34,  197, 94 );  // running pill
    static const COLORREF GREEN_DOT  = RGB(74,  222, 128);  // dot inside pill
    // Spectrum bar colors
    static const COLORREF BAR_GREEN  = RGB(74,  222, 128);  // low
    static const COLORREF BAR_YELLOW = RGB(250, 204, 21 );  // mid
    static const COLORREF BAR_RED    = RGB(239, 68,  68 );  // high
    // Gradient button: left=purple, right=cyan
    static const COLORREF BTN_LEFT   = RGB(124, 58,  237);  // #7c3aed
    static const COLORREF BTN_RIGHT  = RGB(6,   182, 212);  // #06b6d4
}

class MainWindow {
public:
    MainWindow();
    ~MainWindow();

    bool create(HINSTANCE hInst);
    int  run();

private:
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT handleMessage(UINT, WPARAM, LPARAM);

    void registerClass(HINSTANCE hInst);
    void createControls();
    void layoutControls(int w, int h);
    void populateDeviceLists();
    void onStartStop();
    void onTimerMeter();
    void onTimerStats();
    void setButtonState(bool running);
    void onPaint();
    void drawBackground(HDC hdc, int W, int H);
    void drawTitle(HDC hdc, int W);
    void drawSectionLabel(HDC hdc, const wchar_t* text, int x, int y);
    void drawStatusPill(HDC hdc, int x, int y);
    void drawStatCard(HDC hdc, int x, int y, int w, int h, const wchar_t* value, const wchar_t* suffix, const wchar_t* label);
    void drawSpectrumMeter(HDC hdc, int x, int y, int w, int h);
    void drawGradientButton(HDC hdc, int x, int y, int w, int h);
    void drawRoundRect(HDC hdc, int x, int y, int w, int h, int r, COLORREF fill, COLORREF border = 0, bool hasBorder = false);

    // Helpers
    static std::wstring Utf8ToWide(const std::string& s);
    static COLORREF LerpColor(COLORREF a, COLORREF b, float t);
    static void GradientFillRect(HDC hdc, RECT r, COLORREF left, COLORREF right);

    HWND      m_hwnd    = nullptr;
    HINSTANCE m_hInst   = nullptr;
    HFONT     m_fontUI  = nullptr;   // Segoe UI 13
    HFONT     m_fontSm  = nullptr;   // Segoe UI 10 - labels
    HFONT     m_fontBig = nullptr;   // Segoe UI 28 bold - stat numbers
    HFONT     m_fontMed = nullptr;   // Segoe UI 16 - button
    HBRUSH    m_bgBrush = nullptr;

    // Controls (invisible - custom drawn)
    HWND m_cmbInput   = nullptr;
    HWND m_cmbRate    = nullptr;

    // Device lists
    std::vector<AudioDevice> m_inputDevices;

    // Engine
    std::unique_ptr<AudioEngine> m_engine;
    bool     m_running  = false;

    // Meter state - spectrum bars
    static const int NUM_BARS = 20;
    std::array<float, NUM_BARS> m_bars    {};
    std::array<float, NUM_BARS> m_peaks   {};
    std::array<int,   NUM_BARS> m_peakHold{};

    // Stats display
    float    m_latencyMs  = 0.0f;
    uint64_t m_underruns  = 0;
    bool     m_running2   = false;  // confirmed running state

    // Layout constants
    static const int PAD = 20;
    int m_W = 860, m_H = 400;
};

} // namespace Bridge
