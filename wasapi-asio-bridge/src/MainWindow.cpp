#include "MainWindow.h"
#include "Logger.h"
#include "Common.h"
#include <dwmapi.h>
#include <uxtheme.h>
#include <commctrl.h>
#include <stdexcept>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <random>

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "uxtheme.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "user32.lib")

namespace Bridge {

// ---- Helpers ----------------------------------------------------------------

static void EnableDarkTitleBar(HWND hwnd) {
    BOOL dark = TRUE;
    DwmSetWindowAttribute(hwnd, 20, &dark, sizeof(dark));
    DwmSetWindowAttribute(hwnd, 19, &dark, sizeof(dark));
}

std::wstring MainWindow::Utf8ToWide(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w(n - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), n);
    return w;
}

COLORREF MainWindow::LerpColor(COLORREF a, COLORREF b, float t) {
    int ar = GetRValue(a), ag = GetGValue(a), ab_ = GetBValue(a);
    int br = GetRValue(b), bg = GetGValue(b), bb_ = GetBValue(b);
    return RGB(
        (int)(ar + (br - ar) * t),
        (int)(ag + (bg - ag) * t),
        (int)(ab_ + (bb_ - ab_) * t)
    );
}

void MainWindow::GradientFillRect(HDC hdc, RECT r, COLORREF left, COLORREF right) {
    TRIVERTEX v[2];
    v[0].x = r.left;  v[0].y = r.top;
    v[0].Red   = (COLOR16)(GetRValue(left)  << 8);
    v[0].Green = (COLOR16)(GetGValue(left)  << 8);
    v[0].Blue  = (COLOR16)(GetBValue(left)  << 8);
    v[0].Alpha = 0;
    v[1].x = r.right; v[1].y = r.bottom;
    v[1].Red   = (COLOR16)(GetRValue(right) << 8);
    v[1].Green = (COLOR16)(GetGValue(right) << 8);
    v[1].Blue  = (COLOR16)(GetBValue(right) << 8);
    v[1].Alpha = 0;
    GRADIENT_RECT gr = {0, 1};
    GradientFill(hdc, v, 2, &gr, 1, GRADIENT_FILL_RECT_H);
}

// Draw a filled rounded rectangle with optional border
void MainWindow::drawRoundRect(HDC hdc, int x, int y, int w, int h, int r,
                                COLORREF fill, COLORREF border, bool hasBorder) {
    HBRUSH br  = CreateSolidBrush(fill);
    HPEN   pen = hasBorder ? CreatePen(PS_SOLID, 1, border)
                           : (HPEN)GetStockObject(NULL_PEN);
    HBRUSH oldBr  = (HBRUSH)SelectObject(hdc, br);
    HPEN   oldPen = (HPEN)SelectObject(hdc, pen);
    RoundRect(hdc, x, y, x + w, y + h, r * 2, r * 2);
    SelectObject(hdc, oldBr);
    SelectObject(hdc, oldPen);
    DeleteObject(br);
    if (hasBorder) DeleteObject(pen);
}

// ---- Constructor/Destructor -------------------------------------------------

MainWindow::MainWindow() {
    m_engine = std::make_unique<AudioEngine>();
    m_bars.fill(0.0f);
    m_peaks.fill(0.0f);
    m_peakHold.fill(0);
}

MainWindow::~MainWindow() {
    if (m_running) m_engine->stop();
    if (m_fontUI)  DeleteObject(m_fontUI);
    if (m_fontSm)  DeleteObject(m_fontSm);
    if (m_fontBig) DeleteObject(m_fontBig);
    if (m_fontMed) DeleteObject(m_fontMed);
    if (m_bgBrush) DeleteObject(m_bgBrush);
}

// ---- Create -----------------------------------------------------------------

void MainWindow::registerClass(HINSTANCE hInst) {
    WNDCLASSEXW wc = {};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInst;
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = L"MicBridgeWnd";
    RegisterClassExW(&wc);
}

bool MainWindow::create(HINSTANCE hInst) {
    m_hInst = hInst;

    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&icc);

    // Fonts
    m_fontUI  = CreateFontW(-13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                             DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                             CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
    m_fontSm  = CreateFontW(-11, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                             DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                             CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
    m_fontBig = CreateFontW(-28, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                             DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                             CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
    m_fontMed = CreateFontW(-15, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                             DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                             CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
    m_bgBrush = CreateSolidBrush(Theme::BG);

    registerClass(hInst);

    int sw = GetSystemMetrics(SM_CXSCREEN);
    int sh = GetSystemMetrics(SM_CYSCREEN);

    m_hwnd = CreateWindowExW(
        0, L"MicBridgeWnd",
        L"MicBridge v1.0.0",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        (sw - m_W) / 2, (sh - m_H) / 2, m_W, m_H,
        nullptr, nullptr, hInst, this);

    if (!m_hwnd) return false;

    EnableDarkTitleBar(m_hwnd);
    createControls();
    populateDeviceLists();

    ShowWindow(m_hwnd, SW_SHOW);
    UpdateWindow(m_hwnd);

    SetTimer(m_hwnd, IDC_TIMER_METER, 40, nullptr);   // 25fps meter
    SetTimer(m_hwnd, IDC_TIMER_STATS, 300, nullptr);  // stats update

    return true;
}

int MainWindow::run() {
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return (int)msg.wParam;
}

// ---- WndProc ----------------------------------------------------------------

LRESULT CALLBACK MainWindow::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    MainWindow* self = nullptr;
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        self = reinterpret_cast<MainWindow*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)self);
        self->m_hwnd = hwnd;
    } else {
        self = reinterpret_cast<MainWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (self) return self->handleMessage(msg, wp, lp);
    return DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT MainWindow::handleMessage(UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_SIZE: layoutControls(LOWORD(lp), HIWORD(lp)); return 0;
    case WM_PAINT: onPaint(); return 0;
    case WM_ERASEBKGND: return 1;

    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLOREDIT: {
        HDC hdc = (HDC)wp;
        SetBkColor(hdc, Theme::BG);
        SetTextColor(hdc, Theme::TEXT);
        return (LRESULT)m_bgBrush;
    }
    case WM_CTLCOLORLISTBOX: {
        HDC hdc = (HDC)wp;
        SetBkColor(hdc, Theme::SURFACE);
        SetTextColor(hdc, Theme::TEXT);
        return (LRESULT)CreateSolidBrush(Theme::SURFACE);
    }

    case WM_COMMAND:
        if (LOWORD(wp) == IDC_BTN_STARTSTOP) onStartStop();
        return 0;

    case WM_APP: {
        BridgeStatus s = (BridgeStatus)(int)wp;
        if (s == BridgeStatus::Error) { m_running = false; setButtonState(false); }
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return 0;
    }

    case WM_TIMER:
        if (wp == IDC_TIMER_METER) onTimerMeter();
        if (wp == IDC_TIMER_STATS) onTimerStats();
        return 0;

    case WM_DESTROY:
        KillTimer(m_hwnd, IDC_TIMER_METER);
        KillTimer(m_hwnd, IDC_TIMER_STATS);
        if (m_running) m_engine->stop();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(m_hwnd, msg, wp, lp);
}

// ---- Controls ---------------------------------------------------------------

void MainWindow::createControls() {
    // Hidden combo boxes - we custom-draw them, combos just hold data + intercept clicks
    // Actually use real combos positioned over our drawn dropdowns for click handling
    m_cmbInput = CreateWindowExW(0, L"COMBOBOX", nullptr,
        WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL | CBS_HASSTRINGS,
        0, 0, 10, 200, m_hwnd, (HMENU)IDC_COMBO_INPUT, m_hInst, nullptr);
    SendMessageW(m_cmbInput, WM_SETFONT, (WPARAM)m_fontUI, TRUE);

    m_cmbRate = CreateWindowExW(0, L"COMBOBOX", nullptr,
        WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL | CBS_HASSTRINGS,
        0, 0, 10, 200, m_hwnd, (HMENU)IDC_COMBO_RATE, m_hInst, nullptr);
    SendMessageW(m_cmbRate, WM_SETFONT, (WPARAM)m_fontUI, TRUE);

    const wchar_t* rates[] = {
        L"44 100 Hz", L"48 000 Hz (auto-match)",
        L"88 200 Hz", L"96 000 Hz", L"192 000 Hz"
    };
    for (auto r : rates) SendMessageW(m_cmbRate, CB_ADDSTRING, 0, (LPARAM)r);
    SendMessageW(m_cmbRate, CB_SETCURSEL, 1, 0);
}

void MainWindow::layoutControls(int W, int H) {
    if (!m_cmbInput) return;
    int p = PAD;
    // Place combo boxes exactly over our drawn dropdown areas
    // Row 1: Input device (left 55%) | Rate (right 40%)
    int dropY  = 80;
    int dropH  = 40;
    int inputW = (int)(W * 0.52f) - p - p / 2;
    int rateX  = p + inputW + p / 2;
    int rateW  = W - rateX - p;

    SetWindowPos(m_cmbInput, nullptr, p,    dropY, inputW, dropH, SWP_NOZORDER);
    SetWindowPos(m_cmbRate,  nullptr, rateX, dropY, rateW,  dropH, SWP_NOZORDER);

    InvalidateRect(m_hwnd, nullptr, FALSE);
}

// ---- Populate devices -------------------------------------------------------

void MainWindow::populateDeviceLists() {
    SendMessageW(m_cmbInput, CB_RESETCONTENT, 0, 0);
    m_inputDevices = m_engine->enumerator().getCaptureDevices();

    int defSel = 0;
    for (int i = 0; i < (int)m_inputDevices.size(); ++i) {
        auto& d = m_inputDevices[i];
        std::wstring name = Utf8ToWide(d.name);
        SendMessageW(m_cmbInput, CB_ADDSTRING, 0, (LPARAM)name.c_str());
        if (d.isDefault) defSel = i;
    }
    if (m_inputDevices.empty())
        SendMessageW(m_cmbInput, CB_ADDSTRING, 0, (LPARAM)L"(No input devices)");

    SendMessageW(m_cmbInput, CB_SETCURSEL, defSel, 0);
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

// ---- Start/Stop -------------------------------------------------------------

void MainWindow::onStartStop() {
    if (m_running) {
        m_engine->stop();
        m_running = false;
        m_bars.fill(0.0f);
        m_peaks.fill(0.0f);
        setButtonState(false);
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return;
    }

    int inputSel = (int)SendMessageW(m_cmbInput, CB_GETCURSEL, 0, 0);
    int rateSel  = (int)SendMessageW(m_cmbRate,  CB_GETCURSEL, 0, 0);
    uint32_t rates[] = {44100, 48000, 88200, 96000, 192000};
    uint32_t sr = (rateSel >= 0 && rateSel < 5) ? rates[rateSel] : 48000;

    EngineConfig cfg;
    cfg.sampleRate  = sr;
    cfg.channels    = 2;
    cfg.ringFrames  = sr / 5;
    cfg.bufferFrames = sr / 100;

    if (inputSel >= 0 && inputSel < (int)m_inputDevices.size())
        cfg.captureDeviceId = m_inputDevices[inputSel].id;

    // Auto-select VB-Cable as output
    auto outputs = m_engine->enumerator().getRenderDevices();
    for (auto& d : outputs) {
        std::wstring name = Utf8ToWide(d.name);
        if (name.find(L"CABLE") != std::wstring::npos ||
            name.find(L"VB-Audio") != std::wstring::npos) {
            cfg.outputDeviceId = d.id;
            break;
        }
    }

    m_engine->configure(cfg);
    m_engine->setStatusCallback([this](BridgeStatus s, const std::string&) {
        PostMessageW(m_hwnd, WM_APP, (WPARAM)s, 0);
    });

    if (!m_engine->start()) {
        MessageBoxW(m_hwnd,
            L"Failed to start bridge.\n\n"
            L"- Check microphone is connected\n"
            L"- Ensure VB-Cable is installed\n"
            L"- Check bridge.log for details",
            L"Bridge Error", MB_ICONERROR | MB_OK);
        return;
    }

    m_running = true;
    setButtonState(true);
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void MainWindow::setButtonState(bool running) {
    m_running = running;
    EnableWindow(m_cmbInput, !running);
    EnableWindow(m_cmbRate,  !running);
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

// ---- Timers -----------------------------------------------------------------

void MainWindow::onTimerMeter() {
    if (!m_running || !m_engine) {
        // Decay all bars
        for (int i = 0; i < NUM_BARS; ++i) {
            m_bars[i] *= 0.88f;
            if (m_peakHold[i] > 0) m_peakHold[i]--;
            else m_peaks[i] *= 0.97f;
        }
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return;
    }

    auto stats = m_engine->getStats();
    float levelL = stats.levelL;
    float levelR = stats.levelR;

    // Distribute energy across bars with some randomization for spectrum effect
    // Center bars get more energy, sides less - simulate frequency spread
    static std::mt19937 rng(42);
    std::uniform_real_distribution<float> noise(0.85f, 1.15f);

    for (int i = 0; i < NUM_BARS; ++i) {
        // Bell-curve distribution across bars, centered
        float pos    = (float)i / (NUM_BARS - 1);
        float center = 0.45f;
        float spread = 0.35f;
        float weight = std::exp(-0.5f * std::pow((pos - center) / spread, 2.0f));
        float avg    = (levelL + levelR) * 0.5f;
        float target = std::min(1.0f, avg * weight * 3.5f * noise(rng));

        // Fast attack, slow decay
        if (target > m_bars[i]) m_bars[i] = target;
        else                     m_bars[i] = m_bars[i] * 0.80f + target * 0.20f;

        // Peak hold
        if (m_bars[i] > m_peaks[i]) { m_peaks[i] = m_bars[i]; m_peakHold[i] = 30; }
        else if (m_peakHold[i] > 0)   m_peakHold[i]--;
        else                           m_peaks[i] *= 0.97f;
    }

    // Only redraw the meter area
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void MainWindow::onTimerStats() {
    if (!m_engine) return;
    auto stats = m_engine->getStats();
    m_latencyMs = (float)stats.latencyMs;
    m_underruns  = stats.underruns;
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

// ---- Paint ------------------------------------------------------------------

void MainWindow::onPaint() {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(m_hwnd, &ps);

    RECT rc;
    GetClientRect(m_hwnd, &rc);
    int W = rc.right, H = rc.bottom;

    HDC     mem = CreateCompatibleDC(hdc);
    HBITMAP bmp = CreateCompatibleBitmap(hdc, W, H);
    SelectObject(mem, bmp);

    SetBkMode(mem, TRANSPARENT);

    drawBackground(mem, W, H);
    drawTitle(mem, W);

    int p = PAD;

    // --- Row 1: INPUT DEVICE + SAMPLE RATE labels ---
    int row1LabelY = 52;
    drawSectionLabel(mem, L"INPUT DEVICE", p, row1LabelY);
    int rateX = p + (int)(W * 0.52f) - p / 2;
    drawSectionLabel(mem, L"SAMPLE RATE", rateX, row1LabelY);

    // Draw dropdown backgrounds (actual combo box sits on top, transparent-ish)
    int dropY  = row1LabelY + 16;
    int dropH  = 38;
    int inputW = (int)(W * 0.52f) - p - p / 2;
    int rateW  = W - rateX - p;

    // Draw Input dropdown box
    drawRoundRect(mem, p, dropY, inputW, dropH, 6, Theme::SURFACE, Theme::BORDER, true);
    // Draw rate dropdown box
    drawRoundRect(mem, rateX, dropY, rateW, dropH, 6, Theme::SURFACE, Theme::BORDER, true);

    // Draw dropdown text (read from combos)
    {
        wchar_t buf[128] = {};
        int sel = (int)SendMessageW(m_cmbInput, CB_GETCURSEL, 0, 0);
        if (sel >= 0) SendMessageW(m_cmbInput, CB_GETLBTEXT, sel, (LPARAM)buf);
        SelectObject(mem, m_fontUI);
        SetTextColor(mem, Theme::TEXT);
        RECT tr = { p + 12, dropY, p + inputW - 28, dropY + dropH };
        DrawTextW(mem, buf, -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

        // Mic icon (simple dot)
        HBRUSH iconBr = CreateSolidBrush(RGB(100, 120, 200));
        SelectObject(mem, iconBr);
        SelectObject(mem, GetStockObject(NULL_PEN));
        Ellipse(mem, p + 12, dropY + 14, p + 20, dropY + 22);
        DeleteObject(iconBr);

        // Chevron
        SetTextColor(mem, Theme::TEXT_DIM);
        RECT cr = { p + inputW - 22, dropY, p + inputW - 4, dropY + dropH };
        DrawTextW(mem, L"v", -1, &cr, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
    {
        wchar_t buf[128] = {};
        int sel = (int)SendMessageW(m_cmbRate, CB_GETCURSEL, 0, 0);
        if (sel >= 0) SendMessageW(m_cmbRate, CB_GETLBTEXT, sel, (LPARAM)buf);
        SelectObject(mem, m_fontUI);
        SetTextColor(mem, Theme::TEXT);
        RECT tr = { rateX + 12, dropY, rateX + rateW - 24, dropY + dropH };
        DrawTextW(mem, buf, -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        SetTextColor(mem, Theme::TEXT_DIM);
        RECT cr = { rateX + rateW - 22, dropY, rateX + rateW - 4, dropY + dropH };
        DrawTextW(mem, L"v", -1, &cr, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    // --- Row 2: STATUS (left) + AUDIO LEVEL (right) ---
    int row2Y = dropY + dropH + 18;

    // Left column width: same as input dropdown
    int leftW  = inputW;
    int rightX = rateX;
    int rightW = rateW;

    // STATUS label
    drawSectionLabel(mem, L"STATUS", p, row2Y);

    // AUDIO LEVEL label
    drawSectionLabel(mem, L"AUDIO LEVEL", rightX, row2Y);

    int row2ContentY = row2Y + 16;

    // Status pill
    drawStatusPill(mem, p, row2ContentY);

    // Stat cards below pill
    int cardY  = row2ContentY + 34;
    int cardH  = 58;
    int cardW  = (leftW - 10) / 2;

    // Latency card
    wchar_t latBuf[32], latSuf[8];
    swprintf_s(latBuf, L"%.0f", m_latencyMs);
    wcscpy_s(latSuf, L"ms");
    drawStatCard(mem, p, cardY, cardW, cardH, latBuf, latSuf, L"LATENCY");

    // Underruns card
    wchar_t urBuf[32];
    swprintf_s(urBuf, L"%llu", m_underruns);
    drawStatCard(mem, p + cardW + 10, cardY, cardW, cardH, urBuf, L"xruns", L"UNDERRUNS");

    // Spectrum meter (right column, spans from label down to above button)
    int meterH = cardY + cardH - row2ContentY;
    drawSpectrumMeter(mem, rightX, row2ContentY, rightW, meterH);

    // --- Button ---
    int btnY = H - 56 - p;
    int btnH = 52;
    drawGradientButton(mem, p, btnY, W - p * 2, btnH);

    BitBlt(hdc, 0, 0, W, H, mem, 0, 0, SRCCOPY);
    DeleteObject(bmp);
    DeleteDC(mem);
    EndPaint(m_hwnd, &ps);
}

void MainWindow::drawBackground(HDC hdc, int W, int H) {
    HBRUSH bg = CreateSolidBrush(Theme::BG);
    RECT r = {0, 0, W, H};
    FillRect(hdc, &r, bg);
    DeleteObject(bg);
}

void MainWindow::drawTitle(HDC hdc, int W) {
    SelectObject(hdc, m_fontSm);
    SetTextColor(hdc, Theme::TEXT_DIM);
    RECT r = {0, 10, W, 34};
    DrawTextW(hdc, L"MicBridge v1.0.0", -1, &r, DT_CENTER | DT_TOP | DT_SINGLELINE);
}

void MainWindow::drawSectionLabel(HDC hdc, const wchar_t* text, int x, int y) {
    SelectObject(hdc, m_fontSm);
    SetTextColor(hdc, Theme::TEXT_LABEL);
    RECT r = {x, y, x + 300, y + 14};
    DrawTextW(hdc, text, -1, &r, DT_LEFT | DT_TOP | DT_SINGLELINE);
}

void MainWindow::drawStatusPill(HDC hdc, int x, int y) {
    if (m_running) {
        // Green pill background
        COLORREF pillBg = RGB(20, 60, 35);
        drawRoundRect(hdc, x, y, 100, 26, 13, pillBg);

        // Green dot
        HBRUSH dotBr = CreateSolidBrush(Theme::GREEN_DOT);
        HPEN   dotPn = CreatePen(PS_SOLID, 1, Theme::GREEN_DOT);
        SelectObject(hdc, dotBr);
        SelectObject(hdc, dotPn);
        Ellipse(hdc, x + 12, y + 9, x + 20, y + 17);
        DeleteObject(dotBr);
        DeleteObject(dotPn);

        // RUNNING text
        SelectObject(hdc, m_fontSm);
        SetTextColor(hdc, Theme::GREEN_DOT);
        RECT r = {x + 22, y, x + 100, y + 26};
        DrawTextW(hdc, L"RUNNING", -1, &r, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    } else {
        // Dim pill
        drawRoundRect(hdc, x, y, 80, 26, 13, Theme::SURFACE);
        SelectObject(hdc, m_fontSm);
        SetTextColor(hdc, Theme::TEXT_DIM);
        RECT r = {x, y, x + 80, y + 26};
        DrawTextW(hdc, L"IDLE", -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
}

void MainWindow::drawStatCard(HDC hdc, int x, int y, int w, int h,
                               const wchar_t* value, const wchar_t* suffix,
                               const wchar_t* label) {
    drawRoundRect(hdc, x, y, w, h, 8, Theme::SURFACE2, Theme::BORDER, true);

    // Big number
    SelectObject(hdc, m_fontBig);
    SetTextColor(hdc, Theme::TEXT);
    RECT vr = {x + 12, y + 6, x + w - 4, y + h - 20};
    DrawTextW(hdc, value, -1, &vr, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    // Measure value width to place suffix right after
    SIZE sz = {};
    GetTextExtentPoint32W(hdc, value, (int)wcslen(value), &sz);

    // Suffix (small, next to number, vertically lower)
    SelectObject(hdc, m_fontSm);
    SetTextColor(hdc, Theme::TEXT_DIM);
    RECT sr = {x + 12 + sz.cx + 2, y + h - 34, x + w, y + h - 14};
    DrawTextW(hdc, suffix, -1, &sr, DT_LEFT | DT_TOP | DT_SINGLELINE);

    // Label at bottom
    RECT lr = {x + 12, y + h - 18, x + w - 4, y + h - 2};
    DrawTextW(hdc, label, -1, &lr, DT_LEFT | DT_TOP | DT_SINGLELINE);
}

void MainWindow::drawSpectrumMeter(HDC hdc, int x, int y, int w, int h) {
    // Background
    drawRoundRect(hdc, x, y, w, h, 8, Theme::SURFACE2, Theme::BORDER, true);

    int pad  = 10;
    int bx   = x + pad;
    int by   = y + pad;
    int bw   = w - pad * 2;
    int bh   = h - pad * 2;

    int totalGap = (NUM_BARS - 1) * 3;
    int barW     = (bw - totalGap) / NUM_BARS;
    if (barW < 2) barW = 2;

    for (int i = 0; i < NUM_BARS; ++i) {
        float level = std::clamp(m_bars[i], 0.0f, 1.0f);
        float peak  = std::clamp(m_peaks[i], 0.0f, 1.0f);

        int barX = bx + i * (barW + 3);
        int barH = (int)(level * bh);

        // Color based on height: green -> yellow -> red
        COLORREF col;
        if (level < 0.6f)       col = Theme::BAR_GREEN;
        else if (level < 0.85f) col = LerpColor(Theme::BAR_GREEN, Theme::BAR_YELLOW,
                                                 (level - 0.6f) / 0.25f);
        else                    col = LerpColor(Theme::BAR_YELLOW, Theme::BAR_RED,
                                                (level - 0.85f) / 0.15f);

        if (barH > 0) {
            HBRUSH barBr = CreateSolidBrush(col);
            RECT   barR  = { barX, by + bh - barH, barX + barW, by + bh };
            SelectObject(hdc, barBr);
            SelectObject(hdc, GetStockObject(NULL_PEN));
            RoundRect(hdc, barR.left, barR.top, barR.right, barR.bottom, 3, 3);
            DeleteObject(barBr);
        }

        // Peak marker
        if (peak > 0.02f) {
            int peakY = by + bh - (int)(peak * bh) - 2;
            COLORREF pc = (peak > 0.85f) ? Theme::BAR_RED :
                          (peak > 0.6f)  ? Theme::BAR_YELLOW : Theme::BAR_GREEN;
            HPEN pk = CreatePen(PS_SOLID, 2, pc);
            HPEN op = (HPEN)SelectObject(hdc, pk);
            MoveToEx(hdc, barX, peakY, nullptr);
            LineTo(hdc, barX + barW, peakY);
            SelectObject(hdc, op);
            DeleteObject(pk);
        }
    }
}

void MainWindow::drawGradientButton(HDC hdc, int x, int y, int w, int h) {
    // Clip to rounded rect shape
    HRGN clip = CreateRoundRectRgn(x, y, x + w, y + h, 14, 14);
    SelectClipRgn(hdc, clip);

    RECT gr = { x, y, x + w, y + h };
    GradientFillRect(hdc, gr, Theme::BTN_LEFT, Theme::BTN_RIGHT);

    SelectClipRgn(hdc, nullptr);
    DeleteObject(clip);

    // Button text
    SelectObject(hdc, m_fontMed);
    SetTextColor(hdc, RGB(255, 255, 255));
    RECT tr = { x, y, x + w, y + h };

    if (m_running) {
        DrawTextW(hdc, L"||  Stop Bridge", -1, &tr, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    } else {
        DrawTextW(hdc, L">  Start Bridge", -1, &tr, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
}

} // namespace Bridge
