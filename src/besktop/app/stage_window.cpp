#include "besktop/app/stage_window.h"

namespace {

constexpr wchar_t kStageWindowClassName[] = L"BesktopStageWindow";
constexpr int kForceExitHotkeyId = 1;

bool IsKeyPressed(int virtualKey)
{
    return (GetKeyState(virtualKey) & 0x8000) != 0;
}

HFONT CreateStageFont(HDC hdc, int pointSize, int weight)
{
    return CreateFontW(
        -MulDiv(pointSize, GetDeviceCaps(hdc, LOGPIXELSY), 72),
        0,
        0,
        0,
        weight,
        FALSE,
        FALSE,
        FALSE,
        DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_SWISS,
        L"Microsoft YaHei UI");
}

void DrawCenteredLine(HDC hdc, const wchar_t* text, RECT lineRect)
{
    DrawTextW(hdc, text, -1, &lineRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

} // namespace

namespace besktop {

StageWindow::StageWindow(HINSTANCE instance)
    : instance_(instance)
{
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
        return false;
    }

    const POINT primaryPoint{0, 0};
    HMONITOR primaryMonitor = MonitorFromPoint(primaryPoint, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO monitorInfo{};
    monitorInfo.cbSize = sizeof(monitorInfo);
    if (primaryMonitor == nullptr || !GetMonitorInfoW(primaryMonitor, &monitorInfo)) {
        return false;
    }

    const RECT bounds = monitorInfo.rcMonitor;
    const int width = bounds.right - bounds.left;
    const int height = bounds.bottom - bounds.top;

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
        return false;
    }

    RegisterForceExitHotkey();

    ShowWindow(hwnd_, showCommand == 0 ? SW_SHOW : showCommand);
    SetWindowPos(hwnd_, HWND_TOPMOST, bounds.left, bounds.top, width, height, SWP_SHOWWINDOW);
    UpdateWindow(hwnd_);
    return true;
}

void StageWindow::Close()
{
    if (hwnd_ != nullptr) {
        DestroyWindow(hwnd_);
    }
}

void StageWindow::Paint()
{
    PAINTSTRUCT paint{};
    HDC hdc = BeginPaint(hwnd_, &paint);

    RECT clientRect{};
    GetClientRect(hwnd_, &clientRect);

    HBRUSH backgroundBrush = CreateSolidBrush(RGB(10, 12, 16));
    FillRect(hdc, &clientRect, backgroundBrush);
    DeleteObject(backgroundBrush);

    SetBkMode(hdc, TRANSPARENT);

    HFONT titleFont = CreateStageFont(hdc, 34, FW_SEMIBOLD);
    HFONT bodyFont = CreateStageFont(hdc, 18, FW_NORMAL);
    HGDIOBJ previousFont = nullptr;

    const int height = clientRect.bottom - clientRect.top;
    RECT titleRect = clientRect;
    titleRect.top = (height / 2) - 96;
    titleRect.bottom = titleRect.top + 56;

    if (titleFont != nullptr) {
        previousFont = SelectObject(hdc, titleFont);
    }
    SetTextColor(hdc, RGB(246, 248, 252));
    DrawCenteredLine(hdc, L"Besktop Stage Window MVP", titleRect);

    if (bodyFont != nullptr) {
        SelectObject(hdc, bodyFont);
    }
    SetTextColor(hdc, RGB(190, 198, 210));

    RECT escRect = clientRect;
    escRect.top = titleRect.bottom + 28;
    escRect.bottom = escRect.top + 32;
    DrawCenteredLine(hdc, L"Esc \u9000\u51fa", escRect);

    RECT hotkeyRect = clientRect;
    hotkeyRect.top = escRect.bottom + 12;
    hotkeyRect.bottom = hotkeyRect.top + 32;
    DrawCenteredLine(hdc, L"Ctrl + Shift + B \u5f3a\u5236\u9000\u51fa", hotkeyRect);

    if (previousFont != nullptr) {
        SelectObject(hdc, previousFont);
    }
    if (bodyFont != nullptr) {
        DeleteObject(bodyFont);
    }
    if (titleFont != nullptr) {
        DeleteObject(titleFont);
    }

    EndPaint(hwnd_, &paint);
}

void StageWindow::RegisterForceExitHotkey()
{
    forceExitHotkeyRegistered_ =
        RegisterHotKey(hwnd_, kForceExitHotkeyId, MOD_CONTROL | MOD_SHIFT, 'B') != FALSE;
}

void StageWindow::UnregisterForceExitHotkey()
{
    if (forceExitHotkeyRegistered_) {
        UnregisterHotKey(hwnd_, kForceExitHotkeyId);
        forceExitHotkeyRegistered_ = false;
    }
}

LRESULT StageWindow::HandleMessage(UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message) {
    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE ||
            (wParam == 'B' && IsKeyPressed(VK_CONTROL) && IsKeyPressed(VK_SHIFT))) {
            Close();
            return 0;
        }
        break;
    case WM_HOTKEY:
        if (wParam == kForceExitHotkeyId) {
            Close();
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
        UnregisterForceExitHotkey();
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
