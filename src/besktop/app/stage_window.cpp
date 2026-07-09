#include "besktop/app/stage_window.h"

#include "besktop/animation/icon_fight_scene.h"
#include "besktop/desktop/desktop_snapshot.h"
#include "besktop/logging/logger.h"
#include "besktop/render/wallpaper_renderer.h"

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
    scene_.Reset(snapshot_, RECT{0, 0, width, height});

    ShowWindow(hwnd_, showCommand == 0 ? SW_SHOW : showCommand);
    SetWindowPos(hwnd_, HWND_TOPMOST, bounds.left, bounds.top, width, height, SWP_SHOWWINDOW);
    SetFocus(hwnd_);
    animationStartTick_ = GetTickCount64();
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

    HBRUSH backgroundBrush = CreateSolidBrush(RGB(10, 12, 16));
    FillRect(renderHdc, &clientRect, backgroundBrush);
    DeleteObject(backgroundBrush);
    const bool wallpaperDrawn = wallpaperRenderer_.Draw(renderHdc, clientRect, snapshot_.wallpaper);
    if (!wallpaperDrawLogged_) {
        wallpaperDrawLogged_ = true;
        if (wallpaperDrawn) {
            LogInfo(L"wallpaper draw succeeded");
        } else {
            LogWarning(L"wallpaper draw failed; using fallback background");
        }
    }

    scene_.Render(renderHdc, clientRect);

    if (renderHdc == bufferHdc) {
        BitBlt(paintHdc, 0, 0, width, height, bufferHdc, 0, 0, SRCCOPY);
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
    LogInfo(L"monitor: " + FormatMonitorSize(snapshot_.monitorBounds));
    LogInfo(
        L"wallpaper path: " +
        (snapshot_.wallpaper.path.empty() ? std::wstring(L"<fallback>") : snapshot_.wallpaper.path));
    LogInfo(L"wallpaper layout: " + ToDisplayString(snapshot_.wallpaper.layout));
    LogInfo(L"desktop icons: " + std::to_wstring(snapshot_.icons.size()));
    for (const std::wstring& warning : snapshot_.warnings) {
        LogWarning(L"snapshot warning: " + warning);
    }
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
            elapsedSeconds_ = static_cast<double>(now - animationStartTick_) / 1000.0;
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
