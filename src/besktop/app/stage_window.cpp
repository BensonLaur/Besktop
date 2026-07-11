#include "besktop/app/stage_window.h"

#include "besktop/animation/icon_fight_scene.h"
#include "besktop/app/runtime_options.h"
#include "besktop/desktop/desktop_snapshot.h"
#include "besktop/logging/logger.h"
#include "besktop/render/wallpaper_renderer.h"
#include "besktop/render/taskbar_renderer.h"

#include <algorithm>
#include <cstdio>
#include <string>

namespace {

constexpr wchar_t kStageWindowClassName[] = L"BesktopStageWindow";
constexpr int kForceExitHotkeyId = 1;
constexpr UINT_PTR kAnimationTimerId = 2;
constexpr UINT kAnimationFrameMs = 16;

bool IsKeyPressed(int virtualKey)
{
    return (GetKeyState(virtualKey) & 0x8000) != 0;
}

std::wstring FormatMonitorSize(const RECT& bounds)
{
    return std::to_wstring(bounds.right - bounds.left) + L" x " + std::to_wstring(bounds.bottom - bounds.top);
}

std::wstring FormatRect(const RECT& rect)
{
    return L"(" +
        std::to_wstring(rect.left) +
        L"," +
        std::to_wstring(rect.top) +
        L")-(" +
        std::to_wstring(rect.right) +
        L"," +
        std::to_wstring(rect.bottom) +
        L") " +
        FormatMonitorSize(rect);
}

std::wstring FormatDouble(double value, int precision = 1)
{
    wchar_t buffer[64]{};
    swprintf_s(buffer, L"%.*f", precision, value);
    return buffer;
}

UINT GetDpiForWindowCompat(HWND hwnd)
{
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32 != nullptr) {
        using GetDpiForWindowFn = UINT(WINAPI*)(HWND);
        auto* getDpiForWindow = reinterpret_cast<GetDpiForWindowFn>(
            GetProcAddress(user32, "GetDpiForWindow"));
        if (getDpiForWindow != nullptr) {
            const UINT dpi = getDpiForWindow(hwnd);
            if (dpi > 0) {
                return dpi;
            }
        }
    }
    HDC screenDc = GetDC(nullptr);
    const UINT dpi = screenDc != nullptr ? static_cast<UINT>(GetDeviceCaps(screenDc, LOGPIXELSX)) : 96;
    if (screenDc != nullptr) {
        ReleaseDC(nullptr, screenDc);
    }
    return dpi > 0 ? dpi : 96;
}

} // namespace

namespace besktop {

class StageWindow {
public:
    StageWindow(HINSTANCE instance, const RuntimeOptions& options);

    StageWindow(const StageWindow&) = delete;
    StageWindow& operator=(const StageWindow&) = delete;

    int Run(int showCommand);

private:
    bool Create(int showCommand);
    void Close();
    void Paint();
    void RegisterForceExitHotkey();
    void UnregisterForceExitHotkey();
    void LogSnapshot() const;
    void ResetFrameStats(ULONGLONG now);
    void RecordTimerTick(ULONGLONG now);
    void RecordPaintFrame(ULONGLONG startTick, ULONGLONG endTick);
    void LogFrameStatsIfDue(ULONGLONG now);

    LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

    HINSTANCE instance_ = nullptr;
    const RuntimeOptions& options_;
    HWND hwnd_ = nullptr;
    DesktopSnapshot snapshot_;
    WallpaperRenderer wallpaperRenderer_;
    TaskbarRenderer taskbarRenderer_;
    IconFightScene scene_;
    ULONGLONG animationStartTick_ = 0;
    double elapsedSeconds_ = 0.0;
    bool animationTimerStarted_ = false;
    bool forceExitHotkeyRegistered_ = false;
    bool wallpaperDrawLogged_ = false;
    bool taskbarDrawLogged_ = false;
    bool firstPaintTraceLogged_ = false;
    ULONGLONG frameStatsStartTick_ = 0;
    ULONGLONG lastTimerTick_ = 0;
    ULONGLONG totalTimerDeltaMs_ = 0;
    ULONGLONG maxTimerDeltaMs_ = 0;
    ULONGLONG totalPaintMs_ = 0;
    ULONGLONG maxPaintMs_ = 0;
    unsigned int timerTickCount_ = 0;
    unsigned int timerDeltaCount_ = 0;
    unsigned int paintFrameCount_ = 0;
};

StageWindow::StageWindow(HINSTANCE instance, const RuntimeOptions& options)
    : instance_(instance),
      options_(options)
{
}

int RunStageWindow(HINSTANCE instance, int showCommand, const RuntimeOptions& options)
{
    LogInfo(L"app start");
    if (options.verboseInfoLogging) {
        LogInfo(L"log file: " + GetLogFilePath());
    }
    StageWindow stageWindow(instance, options);
    const int exitCode = stageWindow.Run(showCommand);
    LogInfo(L"app exit code: " + std::to_wstring(exitCode));
    return exitCode;
}

int StageWindow::Run(int showCommand)
{
    if (!Create(showCommand)) {
        return 1;
    }

    MSG message{};
    for (;;) {
        const BOOL result = GetMessageW(&message, nullptr, 0, 0);
        if (result == -1) {
            return 1;
        }
        if (result == 0) {
            break;
        }

        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    return static_cast<int>(message.wParam);
}

bool StageWindow::Create(int showCommand)
{
    LogInfo(std::wstring(L"developer build: ") + (options_.developerBuild ? L"yes" : L"no"));
    LogInfo(std::wstring(L"diagnostics: ") + (options_.diagnosticsEnabled ? L"enabled" : L"disabled"));
    LogInfo(std::wstring(L"frame stats: ") + (options_.frameStatsEnabled ? L"enabled" : L"disabled"));
    LogInfo(std::wstring(L"frame trace: ") + (options_.frameTraceEnabled ? L"enabled" : L"disabled"));
    LogInfo(L"animation speed: " + FormatDouble(options_.animationSpeed, 2) + L"x");
    LogInfo(L"animation offset: " + FormatDouble(options_.animationOffsetSeconds, 2) + L"s");

    snapshot_ = CaptureDesktopSnapshot();
    LogSnapshot();

    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = StageWindow::WindowProc;
    windowClass.hInstance = instance_;
    windowClass.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hbrBackground = nullptr;
    windowClass.lpszClassName = kStageWindowClassName;
    windowClass.hIconSm = LoadIconW(nullptr, IDI_APPLICATION);

    if (RegisterClassExW(&windowClass) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        LogError(L"RegisterClassExW failed: " + std::to_wstring(GetLastError()));
        return false;
    }

    const RECT bounds = snapshot_.monitorBounds;
    const int width = bounds.right - bounds.left;
    const int height = bounds.bottom - bounds.top;
    if (width <= 0 || height <= 0) {
        LogError(L"invalid monitor bounds: " + FormatMonitorSize(bounds));
        return false;
    }

    hwnd_ = CreateWindowExW(
        WS_EX_APPWINDOW | WS_EX_TOPMOST,
        kStageWindowClassName,
        L"Besktop",
        WS_POPUP,
        bounds.left,
        bounds.top,
        width,
        height,
        nullptr,
        nullptr,
        instance_,
        this);

    if (hwnd_ == nullptr) {
        LogError(L"CreateWindowExW failed: " + std::to_wstring(GetLastError()));
        return false;
    }

    RegisterForceExitHotkey();
    SetWindowPos(hwnd_, HWND_TOPMOST, bounds.left, bounds.top, width, height, SWP_NOACTIVATE);
    RECT windowRect{};
    RECT clientRect{};
    GetWindowRect(hwnd_, &windowRect);
    GetClientRect(hwnd_, &clientRect);
    LogInfo(L"stage window dpi: " + std::to_wstring(GetDpiForWindowCompat(hwnd_)));
    LogInfo(L"stage window rect: " + FormatRect(windowRect));
    LogInfo(L"stage client rect: " + FormatRect(clientRect));
    scene_.Reset(snapshot_, clientRect);
    ShowWindow(hwnd_, showCommand == 0 ? SW_SHOW : showCommand);
    SetFocus(hwnd_);
    animationStartTick_ = GetTickCount64();
    ResetFrameStats(animationStartTick_);
    animationTimerStarted_ = SetTimer(hwnd_, kAnimationTimerId, kAnimationFrameMs, nullptr) != 0;
    if (animationTimerStarted_) {
        LogInfo(L"animation timer started: " + std::to_wstring(kAnimationFrameMs) + L"ms");
    } else {
        LogWarning(L"animation timer failed: " + std::to_wstring(GetLastError()));
    }
    UpdateWindow(hwnd_);
    LogInfo(L"stage window created: " + FormatMonitorSize(bounds));
    return true;
}

void StageWindow::Close()
{
    LogInfo(L"Close called");
    if (hwnd_ != nullptr) {
        DestroyWindow(hwnd_);
    }
}

void StageWindow::Paint()
{
    const ULONGLONG paintStartTick = GetTickCount64();
    const bool traceFirstPaint = options_.frameTraceEnabled && !firstPaintTraceLogged_;
    if (traceFirstPaint) {
        firstPaintTraceLogged_ = true;
        LogInfo(L"paint trace: begin first paint");
    }
    PAINTSTRUCT paint{};
    HDC paintHdc = BeginPaint(hwnd_, &paint);

    RECT clientRect{};
    GetClientRect(hwnd_, &clientRect);
    const int width = clientRect.right - clientRect.left;
    const int height = clientRect.bottom - clientRect.top;
    if (width <= 0 || height <= 0) {
        EndPaint(hwnd_, &paint);
        return;
    }

    HDC bufferHdc = CreateCompatibleDC(paintHdc);
    HBITMAP bufferBitmap = CreateCompatibleBitmap(paintHdc, width, height);
    HGDIOBJ previousBitmap = nullptr;
    HDC renderHdc = paintHdc;
    if (bufferHdc != nullptr && bufferBitmap != nullptr) {
        previousBitmap = SelectObject(bufferHdc, bufferBitmap);
        renderHdc = bufferHdc;
    }
    if (traceFirstPaint) {
        LogInfo(L"paint trace: double buffer ready");
    }

    HBRUSH backgroundBrush = CreateSolidBrush(RGB(10, 12, 16));
    FillRect(renderHdc, &clientRect, backgroundBrush);
    DeleteObject(backgroundBrush);
    if (traceFirstPaint) {
        LogInfo(L"paint trace: background filled");
    }
    const bool wallpaperDrawn = wallpaperRenderer_.Draw(renderHdc, clientRect, snapshot_.wallpaper);
    if (traceFirstPaint) {
        LogInfo(L"paint trace: wallpaper draw returned");
    }
    if (!wallpaperDrawLogged_) {
        wallpaperDrawLogged_ = true;
        if (wallpaperDrawn) {
            LogInfo(L"wallpaper draw succeeded");
        } else {
            LogWarning(L"wallpaper draw failed; using fallback background");
        }
    }

    const bool taskbarDrawn = taskbarRenderer_.Draw(renderHdc, clientRect, snapshot_);
    if (!taskbarDrawLogged_) {
        taskbarDrawLogged_ = true;
        if (taskbarDrawn) {
            LogInfo(L"static taskbar draw succeeded");
        } else if (snapshot_.taskbar.captureSucceeded) {
            LogWarning(L"static taskbar draw unavailable; continuing without taskbar pixels");
        }
    }
    if (traceFirstPaint) {
        LogInfo(L"paint trace: taskbar rendered");
    }

    scene_.Render(renderHdc, clientRect);
    if (traceFirstPaint) {
        LogInfo(L"paint trace: scene rendered");
    }

    if (renderHdc == bufferHdc) {
        BitBlt(paintHdc, 0, 0, width, height, bufferHdc, 0, 0, SRCCOPY);
    }
    if (traceFirstPaint) {
        LogInfo(L"paint trace: buffer copied");
    }
    if (previousBitmap != nullptr) {
        SelectObject(bufferHdc, previousBitmap);
    }
    if (bufferBitmap != nullptr) {
        DeleteObject(bufferBitmap);
    }
    if (bufferHdc != nullptr) {
        DeleteDC(bufferHdc);
    }

    EndPaint(hwnd_, &paint);
    RecordPaintFrame(paintStartTick, GetTickCount64());
}

void StageWindow::RegisterForceExitHotkey()
{
    forceExitHotkeyRegistered_ =
        RegisterHotKey(hwnd_, kForceExitHotkeyId, MOD_CONTROL | MOD_SHIFT, 'B') != FALSE;
    if (forceExitHotkeyRegistered_) {
        LogInfo(L"force exit hotkey registered: Ctrl+Shift+B");
    } else {
        LogWarning(L"force exit hotkey registration failed: " + std::to_wstring(GetLastError()));
    }
}

void StageWindow::UnregisterForceExitHotkey()
{
    if (forceExitHotkeyRegistered_) {
        UnregisterHotKey(hwnd_, kForceExitHotkeyId);
        forceExitHotkeyRegistered_ = false;
        LogInfo(L"force exit hotkey unregistered");
    }
}

void StageWindow::LogSnapshot() const
{
    LogInfo(L"snapshot captured");
    LogInfo(L"monitor: " + FormatRect(snapshot_.monitorBounds));
    LogInfo(L"work area: " + FormatRect(snapshot_.workArea));
    LogInfo(std::wstring(L"taskbar detected: ") + (snapshot_.taskbar.visible ? L"yes" : L"no"));
    LogInfo(std::wstring(L"taskbar pixels captured: ") +
        (snapshot_.taskbar.captureSucceeded ? L"yes" : L"no"));
    LogInfo(
        L"wallpaper path: " +
        (snapshot_.wallpaper.path.empty() ? std::wstring(L"<fallback>") : snapshot_.wallpaper.path));
    LogInfo(L"wallpaper layout: " + ToDisplayString(snapshot_.wallpaper.layout));
    LogInfo(L"desktop icons: " + std::to_wstring(snapshot_.icons.size()));
    LogInfo(
        L"desktop image list icon size: " +
        std::to_wstring(snapshot_.iconDisplay.imageListIconSize.cx) +
        L" x " +
        std::to_wstring(snapshot_.iconDisplay.imageListIconSize.cy) +
        L" (" +
        snapshot_.iconDisplay.source +
        (snapshot_.iconDisplay.usedFallback ? L", fallback" : L"") +
        L")");
    size_t iconImageSourceCount = 0;
    for (const DesktopIconSnapshot& icon : snapshot_.icons) {
        if (!icon.image.usedFallback && !icon.image.sourceIdentifier.empty()) {
            ++iconImageSourceCount;
        }
    }
    LogInfo(
        L"desktop icon image sources: " +
        std::to_wstring(iconImageSourceCount) +
        L" / " +
        std::to_wstring(snapshot_.icons.size()));
    size_t iconBoundsCount = 0;
    for (const DesktopIconSnapshot& icon : snapshot_.icons) {
        if (!icon.usedIconBoundsFallback &&
            icon.iconBounds.right > icon.iconBounds.left &&
            icon.iconBounds.bottom > icon.iconBounds.top) {
            ++iconBoundsCount;
        }
    }
    LogInfo(
        L"desktop icon bounds: " +
        std::to_wstring(iconBoundsCount) +
        L" / " +
        std::to_wstring(snapshot_.icons.size()));
    size_t labelBoundsCount = 0;
    for (const DesktopIconSnapshot& icon : snapshot_.icons) {
        if (!icon.usedLabelBoundsFallback &&
            icon.labelBounds.right > icon.labelBounds.left &&
            icon.labelBounds.bottom > icon.labelBounds.top) {
            ++labelBoundsCount;
        }
    }
    LogInfo(
        L"desktop icon label bounds: " +
        std::to_wstring(labelBoundsCount) +
        L" / " +
        std::to_wstring(snapshot_.icons.size()));
    size_t sampleIndex = 0;
    for (const DesktopIconSnapshot& icon : snapshot_.icons) {
        const int iconWidth = icon.iconBounds.right - icon.iconBounds.left;
        const int iconHeight = icon.iconBounds.bottom - icon.iconBounds.top;
        if (sampleIndex < 5) {
            LogInfo(
                L"snapshot icon geometry sample: " +
                icon.displayName +
                L"; listview position: " +
                std::to_wstring(icon.listViewPosition.x) +
                L"," +
                std::to_wstring(icon.listViewPosition.y) +
                L"; LVIR_ICON: " +
                FormatRect(icon.iconBounds) +
                L"; LVIR_LABEL: " +
                FormatRect(icon.labelBounds));
        }
        LogInfo(
            L"snapshot icon bounds: " +
            icon.displayName +
            L" -> " +
            std::to_wstring(iconWidth) +
            L" x " +
            std::to_wstring(iconHeight) +
            (icon.usedIconBoundsFallback ? L" (fallback)" : L" (LVIR_ICON)"));
        if (!icon.image.usedFallback && !icon.image.sourceIdentifier.empty()) {
            LogInfo(L"snapshot icon image source: " + icon.displayName + L" -> " + icon.image.sourceIdentifier);
        } else {
            const std::wstring reason =
                icon.image.warning.empty() ? std::wstring(L"no source path") : icon.image.warning;
            LogInfo(L"snapshot icon image fallback: " + icon.displayName + L" (" + reason + L")");
        }
        ++sampleIndex;
    }
    for (const std::wstring& warning : snapshot_.warnings) {
        LogWarning(L"snapshot warning: " + warning);
    }
}

void StageWindow::ResetFrameStats(ULONGLONG now)
{
    frameStatsStartTick_ = now;
    lastTimerTick_ = 0;
    totalTimerDeltaMs_ = 0;
    maxTimerDeltaMs_ = 0;
    totalPaintMs_ = 0;
    maxPaintMs_ = 0;
    timerTickCount_ = 0;
    timerDeltaCount_ = 0;
    paintFrameCount_ = 0;
}

void StageWindow::RecordTimerTick(ULONGLONG now)
{
    if (!options_.frameStatsEnabled) {
        return;
    }

    ++timerTickCount_;
    if (lastTimerTick_ != 0) {
        const ULONGLONG deltaMs = now - lastTimerTick_;
        totalTimerDeltaMs_ += deltaMs;
        maxTimerDeltaMs_ = std::max(maxTimerDeltaMs_, deltaMs);
        ++timerDeltaCount_;
    }
    lastTimerTick_ = now;
}

void StageWindow::RecordPaintFrame(ULONGLONG startTick, ULONGLONG endTick)
{
    if (!options_.frameStatsEnabled) {
        return;
    }

    ++paintFrameCount_;
    const ULONGLONG durationMs = endTick >= startTick ? endTick - startTick : 0;
    totalPaintMs_ += durationMs;
    maxPaintMs_ = std::max(maxPaintMs_, durationMs);
    LogFrameStatsIfDue(endTick);
}

void StageWindow::LogFrameStatsIfDue(ULONGLONG now)
{
    if (!options_.frameStatsEnabled) {
        return;
    }

    constexpr ULONGLONG kLogIntervalMs = 1000;
    const ULONGLONG elapsedMs = now >= frameStatsStartTick_ ? now - frameStatsStartTick_ : 0;
    if (elapsedMs < kLogIntervalMs) {
        return;
    }

    const double elapsedSeconds = static_cast<double>(elapsedMs) / 1000.0;
    const double paintFps = elapsedSeconds > 0.0 ?
        static_cast<double>(paintFrameCount_) / elapsedSeconds :
        0.0;
    const double timerFps = elapsedSeconds > 0.0 ?
        static_cast<double>(timerTickCount_) / elapsedSeconds :
        0.0;
    const double avgTimerMs = timerDeltaCount_ > 0 ?
        static_cast<double>(totalTimerDeltaMs_) / static_cast<double>(timerDeltaCount_) :
        0.0;
    const double avgPaintMs = paintFrameCount_ > 0 ?
        static_cast<double>(totalPaintMs_) / static_cast<double>(paintFrameCount_) :
        0.0;

    LogInfo(
        L"frame stats: paint fps=" +
        FormatDouble(paintFps, 1) +
        L"; timer fps=" +
        FormatDouble(timerFps, 1) +
        L"; timer avg/max ms=" +
        FormatDouble(avgTimerMs, 1) +
        L"/" +
        std::to_wstring(maxTimerDeltaMs_) +
        L"; paint avg/max ms=" +
        FormatDouble(avgPaintMs, 1) +
        L"/" +
        std::to_wstring(maxPaintMs_));

    ResetFrameStats(now);
}

LRESULT StageWindow::HandleMessage(UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message) {
    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE ||
            (wParam == 'B' && IsKeyPressed(VK_CONTROL) && IsKeyPressed(VK_SHIFT))) {
            LogInfo(L"WM_KEYDOWN exit key: " + std::to_wstring(static_cast<unsigned int>(wParam)));
            Close();
            return 0;
        }
        break;
    case WM_HOTKEY:
        if (wParam == kForceExitHotkeyId) {
            LogInfo(L"WM_HOTKEY force exit");
            Close();
            return 0;
        }
        break;
    case WM_TIMER:
        if (wParam == kAnimationTimerId) {
            const ULONGLONG now = GetTickCount64();
            RecordTimerTick(now);
            elapsedSeconds_ = options_.animationOffsetSeconds +
                ((static_cast<double>(now - animationStartTick_) / 1000.0) * options_.animationSpeed);
            scene_.Update(elapsedSeconds_);
            InvalidateRect(hwnd_, nullptr, FALSE);
            return 0;
        }
        break;
    case WM_CLOSE:
        Close();
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        Paint();
        return 0;
    case WM_DESTROY:
        if (animationTimerStarted_) {
            KillTimer(hwnd_, kAnimationTimerId);
            animationTimerStarted_ = false;
            LogInfo(L"animation timer stopped");
        }
        UnregisterForceExitHotkey();
        LogInfo(L"WM_DESTROY; posting quit message");
        PostQuitMessage(0);
        return 0;
    case WM_NCDESTROY: {
        HWND hwnd = hwnd_;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        hwnd_ = nullptr;
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }
    default:
        break;
    }

    return DefWindowProcW(hwnd_, message, wParam, lParam);
}

LRESULT CALLBACK StageWindow::WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (message == WM_NCCREATE) {
        auto* createStruct = reinterpret_cast<CREATESTRUCTW*>(lParam);
        auto* window = reinterpret_cast<StageWindow*>(createStruct->lpCreateParams);
        window->hwnd_ = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(window));
    }

    auto* window = reinterpret_cast<StageWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (window != nullptr) {
        return window->HandleMessage(message, wParam, lParam);
    }

    return DefWindowProcW(hwnd, message, wParam, lParam);
}

} // namespace besktop
