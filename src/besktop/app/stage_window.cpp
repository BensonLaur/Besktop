#include "besktop/app/stage_window.h"

#include "besktop/animation/icon_fight_scene.h"
#include "besktop/desktop/desktop_snapshot.h"
#include "besktop/logging/logger.h"
#include "besktop/render/wallpaper_renderer.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
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

bool IsTruthyEnvironmentFlag(const wchar_t* name)
{
    wchar_t value[16]{};
    constexpr auto valueCapacity = static_cast<DWORD>(sizeof(value) / sizeof(value[0]));
    const DWORD length = GetEnvironmentVariableW(name, value, valueCapacity);
    if (length == 0 || length >= valueCapacity) {
        return false;
    }

    return value[0] == L'1' ||
        value[0] == L't' ||
        value[0] == L'T' ||
        value[0] == L'y' ||
        value[0] == L'Y' ||
        value[0] == L'o' ||
        value[0] == L'O';
}

double ReadEnvironmentDouble(const wchar_t* name, double fallback, double minimum, double maximum)
{
    wchar_t value[64]{};
    constexpr auto valueCapacity = static_cast<DWORD>(sizeof(value) / sizeof(value[0]));
    const DWORD length = GetEnvironmentVariableW(name, value, valueCapacity);
    if (length == 0 || length >= valueCapacity) {
        return fallback;
    }

    wchar_t* end = nullptr;
    const double parsed = wcstod(value, &end);
    if (end == value || !std::isfinite(parsed)) {
        besktop::LogWarning(std::wstring(L"invalid ") + name + L"; using " + FormatDouble(fallback, 2));
        return fallback;
    }

    return std::clamp(parsed, minimum, maximum);
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
    explicit StageWindow(HINSTANCE instance);

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
    HWND hwnd_ = nullptr;
    DesktopSnapshot snapshot_;
    WallpaperRenderer wallpaperRenderer_;
    IconFightScene scene_;
    ULONGLONG animationStartTick_ = 0;
    double elapsedSeconds_ = 0.0;
    bool animationTimerStarted_ = false;
    bool forceExitHotkeyRegistered_ = false;
    bool wallpaperDrawLogged_ = false;
    bool frameStatsEnabled_ = false;
    bool frameTraceEnabled_ = false;
    bool firstPaintTraceLogged_ = false;
    double animationSpeed_ = 1.0;
    double animationOffsetSeconds_ = 0.0;
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

StageWindow::StageWindow(HINSTANCE instance)
    : instance_(instance)
{
}

int RunStageWindow(HINSTANCE instance, int showCommand)
{
    LogInfo(L"app start");
    LogInfo(L"log file: " + GetLogFilePath());
    StageWindow stageWindow(instance);
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
    frameStatsEnabled_ = IsTruthyEnvironmentFlag(L"BESKTOP_FRAME_STATS");
    frameTraceEnabled_ = IsTruthyEnvironmentFlag(L"BESKTOP_FRAME_TRACE");
    animationSpeed_ = ReadEnvironmentDouble(L"BESKTOP_ANIMATION_SPEED", 1.0, 0.05, 8.0);
    animationOffsetSeconds_ = ReadEnvironmentDouble(L"BESKTOP_ANIMATION_OFFSET", 0.0, 0.0, 3600.0);
    LogInfo(std::wstring(L"frame stats: ") + (frameStatsEnabled_ ? L"enabled" : L"disabled"));
    LogInfo(std::wstring(L"frame trace: ") + (frameTraceEnabled_ ? L"enabled" : L"disabled"));
    LogInfo(L"animation speed: " + FormatDouble(animationSpeed_, 2) + L"x");
    LogInfo(L"animation offset: " + FormatDouble(animationOffsetSeconds_, 2) + L"s");

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
    const bool traceFirstPaint = frameTraceEnabled_ && !firstPaintTraceLogged_;
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
        if (!icon.image.usedFallback && !icon.image.sourcePath.empty()) {
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
                FormatRect(icon.iconBounds));
        }
        LogInfo(
            L"snapshot icon bounds: " +
            icon.displayName +
            L" -> " +
            std::to_wstring(iconWidth) +
            L" x " +
            std::to_wstring(iconHeight) +
            (icon.usedIconBoundsFallback ? L" (fallback)" : L" (LVIR_ICON)"));
        if (!icon.image.usedFallback && !icon.image.sourcePath.empty()) {
            LogInfo(L"snapshot icon image source: " + icon.displayName + L" -> " + icon.image.sourcePath);
        } else {
            const std::wstring reason =
                icon.image.warning.empty() ? std::wstring(L"no source path") : icon.image.warning;
            LogWarning(L"snapshot icon image fallback: " + icon.displayName + L" (" + reason + L")");
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
    if (!frameStatsEnabled_) {
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
    if (!frameStatsEnabled_) {
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
    if (!frameStatsEnabled_) {
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
            elapsedSeconds_ = animationOffsetSeconds_ +
                ((static_cast<double>(now - animationStartTick_) / 1000.0) * animationSpeed_);
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
