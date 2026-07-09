#include "besktop/app/stage_window.h"

#include <string>

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

void DrawInfoLine(HDC hdc, const std::wstring& text, RECT lineRect)
{
    DrawTextW(hdc, text.c_str(), -1, &lineRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}

class ScopedGdiSelection {
public:
    explicit ScopedGdiSelection(HDC hdc)
        : hdc_(hdc)
    {
    }

    ScopedGdiSelection(const ScopedGdiSelection&) = delete;
    ScopedGdiSelection& operator=(const ScopedGdiSelection&) = delete;

    ~ScopedGdiSelection()
    {
        Restore();
    }

    void Select(HGDIOBJ object)
    {
        if (object == nullptr) {
            return;
        }

        HGDIOBJ previous = SelectObject(hdc_, object);
        if (previous != nullptr && previous != HGDI_ERROR && !hasOriginal_) {
            original_ = previous;
            hasOriginal_ = true;
        }
    }

    void Restore()
    {
        if (hasOriginal_) {
            SelectObject(hdc_, original_);
            original_ = nullptr;
            hasOriginal_ = false;
        }
    }

private:
    HDC hdc_ = nullptr;
    HGDIOBJ original_ = nullptr;
    bool hasOriginal_ = false;
};

std::wstring FormatMonitorSize(const RECT& bounds)
{
    return std::to_wstring(bounds.right - bounds.left) + L" x " + std::to_wstring(bounds.bottom - bounds.top);
}

} // namespace

namespace besktop {

StageWindow::StageWindow(HINSTANCE instance)
    : instance_(instance)
{
}

int RunStageWindow(HINSTANCE instance, int showCommand)
{
    StageWindow stageWindow(instance);
    return stageWindow.Run(showCommand);
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

    const RECT bounds = snapshot_.monitorBounds;
    const int width = bounds.right - bounds.left;
    const int height = bounds.bottom - bounds.top;
    if (width <= 0 || height <= 0) {
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
        return false;
    }

    RegisterForceExitHotkey();

    ShowWindow(hwnd_, showCommand == 0 ? SW_SHOW : showCommand);
    SetWindowPos(hwnd_, HWND_TOPMOST, bounds.left, bounds.top, width, height, SWP_SHOWWINDOW);
    SetFocus(hwnd_);
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
    wallpaperRenderer_.Draw(hdc, clientRect, snapshot_.wallpaper);

    SetBkMode(hdc, TRANSPARENT);

    HFONT titleFont = CreateStageFont(hdc, 34, FW_SEMIBOLD);
    HFONT bodyFont = CreateStageFont(hdc, 18, FW_NORMAL);
    ScopedGdiSelection selectedFont(hdc);

    const int height = clientRect.bottom - clientRect.top;
    RECT titleRect = clientRect;
    titleRect.top = (height / 2) - 96;
    titleRect.bottom = titleRect.top + 56;

    selectedFont.Select(titleFont);
    SetTextColor(hdc, RGB(246, 248, 252));
    DrawCenteredLine(hdc, L"Besktop Stage Window MVP", titleRect);

    selectedFont.Select(bodyFont);
    SetTextColor(hdc, RGB(190, 198, 210));

    RECT escRect = clientRect;
    escRect.top = titleRect.bottom + 28;
    escRect.bottom = escRect.top + 32;
    DrawCenteredLine(hdc, L"Esc \u9000\u51fa", escRect);

    RECT hotkeyRect = clientRect;
    hotkeyRect.top = escRect.bottom + 12;
    hotkeyRect.bottom = hotkeyRect.top + 32;
    DrawCenteredLine(hdc, L"Ctrl + Shift + B \u5f3a\u5236\u9000\u51fa", hotkeyRect);

    RECT infoRect = clientRect;
    infoRect.left += 48;
    infoRect.right -= 48;
    infoRect.top = hotkeyRect.bottom + 36;
    infoRect.bottom = infoRect.top + 26;

    const std::wstring wallpaperPath =
        snapshot_.wallpaper.path.empty() ? L"<fallback>" : snapshot_.wallpaper.path;
    const std::wstring warning =
        snapshot_.warnings.empty() ? L"None" : snapshot_.warnings.front();
    const std::wstring infoLines[] = {
        L"Monitor: " + FormatMonitorSize(snapshot_.monitorBounds),
        L"Wallpaper: " + wallpaperPath,
        L"Layout: " + ToDisplayString(snapshot_.wallpaper.layout),
        L"Demo icons: " + std::to_wstring(snapshot_.icons.size()),
        L"Warning: " + warning,
    };

    SetTextColor(hdc, RGB(150, 160, 174));
    for (const std::wstring& line : infoLines) {
        DrawInfoLine(hdc, line, infoRect);
        OffsetRect(&infoRect, 0, 28);
    }

    selectedFont.Restore();
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
