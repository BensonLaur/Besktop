#include "besktop/desktop/desktop_snapshot.h"

#include "besktop/logging/logger.h"

#include <commctrl.h>
#include <objbase.h>
#include <shobjidl.h>
#include <wrl/client.h>

#include <cwchar>

namespace {

using Microsoft::WRL::ComPtr;

std::wstring FormatHResult(HRESULT result)
{
    wchar_t buffer[16]{};
    swprintf_s(buffer, L"0x%08X", static_cast<unsigned int>(result));
    return buffer;
}

besktop::WallpaperLayout MapWallpaperLayout(DESKTOP_WALLPAPER_POSITION position)
{
    switch (position) {
    case DWPOS_CENTER:
        return besktop::WallpaperLayout::Center;
    case DWPOS_TILE:
        return besktop::WallpaperLayout::Tile;
    case DWPOS_STRETCH:
        return besktop::WallpaperLayout::Stretch;
    case DWPOS_FIT:
        return besktop::WallpaperLayout::Fit;
    case DWPOS_FILL:
        return besktop::WallpaperLayout::Fill;
    case DWPOS_SPAN:
        return besktop::WallpaperLayout::Span;
    default:
        return besktop::WallpaperLayout::Unknown;
    }
}

class ScopedComInitializer {
public:
    ScopedComInitializer()
    {
        result_ = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        shouldUninitialize_ = SUCCEEDED(result_);
    }

    ScopedComInitializer(const ScopedComInitializer&) = delete;
    ScopedComInitializer& operator=(const ScopedComInitializer&) = delete;

    ~ScopedComInitializer()
    {
        if (shouldUninitialize_) {
            CoUninitialize();
        }
    }

    bool IsUsable() const
    {
        return SUCCEEDED(result_) || result_ == RPC_E_CHANGED_MODE;
    }

    HRESULT Result() const
    {
        return result_;
    }

private:
    HRESULT result_ = E_FAIL;
    bool shouldUninitialize_ = false;
};

void CapturePrimaryMonitorBounds(besktop::DesktopSnapshot& snapshot)
{
    const POINT primaryPoint{0, 0};
    HMONITOR primaryMonitor = MonitorFromPoint(primaryPoint, MONITOR_DEFAULTTOPRIMARY);

    MONITORINFO monitorInfo{};
    monitorInfo.cbSize = sizeof(monitorInfo);
    if (primaryMonitor != nullptr && GetMonitorInfoW(primaryMonitor, &monitorInfo)) {
        snapshot.monitorBounds = monitorInfo.rcMonitor;
        besktop::LogInfo(
            L"primary monitor captured: " +
            std::to_wstring(monitorInfo.rcMonitor.right - monitorInfo.rcMonitor.left) +
            L" x " +
            std::to_wstring(monitorInfo.rcMonitor.bottom - monitorInfo.rcMonitor.top));
        return;
    }

    snapshot.monitorBounds = RECT{0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN)};
    snapshot.warnings.push_back(L"Primary monitor rcMonitor unavailable; using screen metrics fallback.");
    besktop::LogWarning(L"primary monitor fallback used");
}

void CaptureWallpaperSnapshot(besktop::DesktopSnapshot& snapshot)
{
    ScopedComInitializer com;
    if (!com.IsUsable()) {
        snapshot.wallpaper.usedFallback = true;
        snapshot.warnings.push_back(
            L"Wallpaper COM initialization failed (" + FormatHResult(com.Result()) + L").");
        besktop::LogWarning(L"wallpaper COM initialization failed: " + FormatHResult(com.Result()));
        return;
    }
    if (com.Result() == RPC_E_CHANGED_MODE) {
        besktop::LogWarning(L"COM apartment already initialized with a different mode");
    }

    ComPtr<IDesktopWallpaper> desktopWallpaper;
    HRESULT createResult = CoCreateInstance(
        CLSID_DesktopWallpaper,
        nullptr,
        CLSCTX_ALL,
        IID_PPV_ARGS(desktopWallpaper.GetAddressOf()));

    if (FAILED(createResult) || desktopWallpaper == nullptr) {
        snapshot.wallpaper.usedFallback = true;
        snapshot.warnings.push_back(
            L"IDesktopWallpaper creation failed (" + FormatHResult(createResult) + L").");
        besktop::LogWarning(L"IDesktopWallpaper creation failed: " + FormatHResult(createResult));
        return;
    }

    LPWSTR wallpaperPath = nullptr;
    HRESULT wallpaperResult = desktopWallpaper->GetWallpaper(nullptr, &wallpaperPath);
    if (SUCCEEDED(wallpaperResult) && wallpaperPath != nullptr && wallpaperPath[0] != L'\0') {
        snapshot.wallpaper.path = wallpaperPath;
        besktop::LogInfo(L"wallpaper path captured: " + snapshot.wallpaper.path);
    } else {
        snapshot.wallpaper.usedFallback = true;
        snapshot.warnings.push_back(
            L"Wallpaper path capture failed (" + FormatHResult(wallpaperResult) + L").");
        besktop::LogWarning(L"wallpaper path capture failed: " + FormatHResult(wallpaperResult));
    }
    if (wallpaperPath != nullptr) {
        CoTaskMemFree(wallpaperPath);
    }

    DESKTOP_WALLPAPER_POSITION wallpaperPosition = DWPOS_CENTER;
    HRESULT positionResult = desktopWallpaper->GetPosition(&wallpaperPosition);
    if (SUCCEEDED(positionResult)) {
        snapshot.wallpaper.layout = MapWallpaperLayout(wallpaperPosition);
        besktop::LogInfo(L"wallpaper layout captured: " + besktop::ToDisplayString(snapshot.wallpaper.layout));
    } else {
        snapshot.wallpaper.usedFallback = true;
        snapshot.warnings.push_back(
            L"Wallpaper layout capture failed (" + FormatHResult(positionResult) + L").");
        besktop::LogWarning(L"wallpaper layout capture failed: " + FormatHResult(positionResult));
    }

}

bool IsLikelyOnPrimaryMonitor(const RECT& monitorBounds, const POINT& point)
{
    constexpr int kMargin = 128;
    return point.x >= monitorBounds.left - kMargin &&
        point.x <= monitorBounds.right + kMargin &&
        point.y >= monitorBounds.top - kMargin &&
        point.y <= monitorBounds.bottom + kMargin;
}

HWND FindDesktopListViewInParent(HWND parent)
{
    HWND shellView = FindWindowExW(parent, nullptr, L"SHELLDLL_DefView", nullptr);
    if (shellView == nullptr) {
        return nullptr;
    }
    return FindWindowExW(shellView, nullptr, L"SysListView32", nullptr);
}

HWND FindDesktopListView()
{
    if (HWND progman = FindWindowW(L"Progman", nullptr)) {
        if (HWND listView = FindDesktopListViewInParent(progman)) {
            return listView;
        }
    }

    struct EnumState {
        HWND listView = nullptr;
    } state{};

    EnumWindows(
        [](HWND hwnd, LPARAM lParam) -> BOOL {
            auto* enumState = reinterpret_cast<EnumState*>(lParam);
            wchar_t className[64]{};
            if (GetClassNameW(hwnd, className, static_cast<int>(std::size(className))) == 0 ||
                wcscmp(className, L"WorkerW") != 0) {
                return TRUE;
            }

            HWND listView = FindDesktopListViewInParent(hwnd);
            if (listView != nullptr) {
                enumState->listView = listView;
                return FALSE;
            }
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&state));

    return state.listView;
}

class RemoteAllocation {
public:
    RemoteAllocation(HANDLE process, SIZE_T size)
        : process_(process)
    {
        address_ = VirtualAllocEx(process_, nullptr, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    }

    RemoteAllocation(const RemoteAllocation&) = delete;
    RemoteAllocation& operator=(const RemoteAllocation&) = delete;

    ~RemoteAllocation()
    {
        if (address_ != nullptr) {
            VirtualFreeEx(process_, address_, 0, MEM_RELEASE);
        }
    }

    bool IsValid() const
    {
        return address_ != nullptr;
    }

    void* Address() const
    {
        return address_;
    }

private:
    HANDLE process_ = nullptr;
    void* address_ = nullptr;
};

std::wstring ReadDesktopListViewText(HWND listView, HANDLE explorerProcess, int index)
{
    constexpr int kMaxTextLength = 260;
    constexpr SIZE_T kTextBytes = sizeof(wchar_t) * kMaxTextLength;
    constexpr SIZE_T kRemoteSize = sizeof(LVITEMW) + kTextBytes;

    RemoteAllocation remote(explorerProcess, kRemoteSize);
    if (!remote.IsValid()) {
        return L"Icon " + std::to_wstring(index + 1);
    }

    auto* remoteItem = static_cast<LVITEMW*>(remote.Address());
    auto* remoteText = reinterpret_cast<wchar_t*>(
        static_cast<unsigned char*>(remote.Address()) + sizeof(LVITEMW));

    LVITEMW item{};
    item.mask = LVIF_TEXT;
    item.iItem = index;
    item.iSubItem = 0;
    item.pszText = remoteText;
    item.cchTextMax = kMaxTextLength;

    SIZE_T bytesWritten = 0;
    if (!WriteProcessMemory(explorerProcess, remoteItem, &item, sizeof(item), &bytesWritten)) {
        return L"Icon " + std::to_wstring(index + 1);
    }

    SendMessageW(listView, LVM_GETITEMTEXTW, static_cast<WPARAM>(index), reinterpret_cast<LPARAM>(remoteItem));

    wchar_t buffer[kMaxTextLength]{};
    SIZE_T bytesRead = 0;
    if (!ReadProcessMemory(explorerProcess, remoteText, buffer, kTextBytes, &bytesRead) || buffer[0] == L'\0') {
        return L"Icon " + std::to_wstring(index + 1);
    }

    return buffer;
}

bool TryReadDesktopListViewPosition(HWND listView, HANDLE explorerProcess, int index, POINT& position)
{
    RemoteAllocation remote(explorerProcess, sizeof(POINT));
    if (!remote.IsValid()) {
        return false;
    }

    if (!SendMessageW(listView, LVM_GETITEMPOSITION, static_cast<WPARAM>(index), reinterpret_cast<LPARAM>(remote.Address()))) {
        return false;
    }

    SIZE_T bytesRead = 0;
    return ReadProcessMemory(explorerProcess, remote.Address(), &position, sizeof(position), &bytesRead) &&
        bytesRead == sizeof(position);
}

bool CaptureDesktopIconsFromListView(besktop::DesktopSnapshot& snapshot)
{
    HWND listView = FindDesktopListView();
    if (listView == nullptr) {
        snapshot.warnings.push_back(L"Desktop SysListView32 was not found.");
        besktop::LogWarning(L"desktop SysListView32 was not found");
        return false;
    }

    DWORD processId = 0;
    GetWindowThreadProcessId(listView, &processId);
    if (processId == 0) {
        snapshot.warnings.push_back(L"Desktop SysListView32 process id was not found.");
        besktop::LogWarning(L"desktop SysListView32 process id was not found");
        return false;
    }

    HANDLE explorerProcess = OpenProcess(
        PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_OPERATION | PROCESS_VM_READ | PROCESS_VM_WRITE,
        FALSE,
        processId);
    if (explorerProcess == nullptr) {
        const DWORD error = GetLastError();
        snapshot.warnings.push_back(
            L"Desktop SysListView32 process open failed (" + std::to_wstring(error) + L").");
        besktop::LogWarning(L"desktop SysListView32 process open failed: " + std::to_wstring(error));
        return false;
    }

    const LRESULT itemCountResult = SendMessageW(listView, LVM_GETITEMCOUNT, 0, 0);
    if (itemCountResult <= 0) {
        CloseHandle(explorerProcess);
        snapshot.warnings.push_back(L"Desktop SysListView32 returned no icons.");
        besktop::LogWarning(L"desktop SysListView32 returned no icons");
        return false;
    }

    const int itemCount = static_cast<int>(itemCountResult);
    int capturedCount = 0;
    for (int index = 0; index < itemCount; ++index) {
        POINT position{};
        if (!TryReadDesktopListViewPosition(listView, explorerProcess, index, position) ||
            !IsLikelyOnPrimaryMonitor(snapshot.monitorBounds, position)) {
            continue;
        }

        constexpr int kIconWidth = 84;
        constexpr int kIconHeight = 92;

        besktop::DesktopIconSnapshot icon;
        icon.id = L"desktop-listview-icon-" + std::to_wstring(index + 1);
        icon.displayName = ReadDesktopListViewText(listView, explorerProcess, index);
        icon.bounds = RECT{
            position.x,
            position.y,
            position.x + kIconWidth,
            position.y + kIconHeight,
        };
        icon.usedFallback = false;
        snapshot.icons.push_back(icon);
        ++capturedCount;
    }

    CloseHandle(explorerProcess);

    if (capturedCount <= 0) {
        snapshot.warnings.push_back(L"Desktop SysListView32 returned no icons on the primary monitor.");
        besktop::LogWarning(L"desktop SysListView32 returned no icons on the primary monitor");
        return false;
    }

    besktop::LogInfo(L"desktop icons captured from SysListView32: " + std::to_wstring(capturedCount));
    return true;
}

void AddDemoIconFallbacks(besktop::DesktopSnapshot& snapshot)
{
    constexpr const wchar_t* kDemoIconNames[] = {
        L"Chat",
        L"Music",
        L"Game",
    };

    const RECT bounds = snapshot.monitorBounds;
    const int monitorWidth = bounds.right - bounds.left;
    const int monitorHeight = bounds.bottom - bounds.top;
    const int iconWidth = 84;
    const int iconHeight = 92;
    const int spacing = 116;
    const int startX = bounds.left + (monitorWidth / 2) - spacing;
    const int top = bounds.top + (monitorHeight / 2) + 112;

    for (int index = 0; index < 3; ++index) {
        const int left = startX + (spacing * index);

        besktop::DesktopIconSnapshot icon;
        icon.id = L"demo-icon-" + std::to_wstring(index + 1);
        icon.displayName = kDemoIconNames[index];
        icon.bounds = RECT{left, top, left + iconWidth, top + iconHeight};
        icon.usedFallback = true;
        snapshot.icons.push_back(icon);
    }

    snapshot.warnings.push_back(L"Using demo icon fallback.");
    besktop::LogWarning(L"using demo icon fallback");
}

} // namespace

namespace besktop {

DesktopSnapshot CaptureDesktopSnapshot()
{
    DesktopSnapshot snapshot;
    CapturePrimaryMonitorBounds(snapshot);
    CaptureWallpaperSnapshot(snapshot);
    if (!CaptureDesktopIconsFromListView(snapshot)) {
        AddDemoIconFallbacks(snapshot);
    }
    return snapshot;
}

std::wstring ToDisplayString(WallpaperLayout layout)
{
    switch (layout) {
    case WallpaperLayout::Center:
        return L"Center";
    case WallpaperLayout::Tile:
        return L"Tile";
    case WallpaperLayout::Stretch:
        return L"Stretch";
    case WallpaperLayout::Fit:
        return L"Fit";
    case WallpaperLayout::Fill:
        return L"Fill";
    case WallpaperLayout::Span:
        return L"Span";
    case WallpaperLayout::Unknown:
    default:
        return L"Unknown";
    }
}

} // namespace besktop
