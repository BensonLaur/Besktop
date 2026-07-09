#include "besktop/desktop/desktop_snapshot.h"

#include "besktop/logging/logger.h"

#include <objbase.h>
#include <shobjidl.h>

#include <cwchar>

namespace {

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
    HRESULT initializeResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool shouldUninitialize = SUCCEEDED(initializeResult);
    if (FAILED(initializeResult) && initializeResult != RPC_E_CHANGED_MODE) {
        snapshot.wallpaper.usedFallback = true;
        snapshot.warnings.push_back(
            L"Wallpaper COM initialization failed (" + FormatHResult(initializeResult) + L").");
        besktop::LogWarning(L"wallpaper COM initialization failed: " + FormatHResult(initializeResult));
        return;
    }
    if (initializeResult == RPC_E_CHANGED_MODE) {
        besktop::LogWarning(L"COM apartment already initialized with a different mode");
    }

    IDesktopWallpaper* desktopWallpaper = nullptr;
    HRESULT createResult = CoCreateInstance(
        CLSID_DesktopWallpaper,
        nullptr,
        CLSCTX_ALL,
        IID_PPV_ARGS(&desktopWallpaper));

    if (FAILED(createResult) || desktopWallpaper == nullptr) {
        snapshot.wallpaper.usedFallback = true;
        snapshot.warnings.push_back(
            L"IDesktopWallpaper creation failed (" + FormatHResult(createResult) + L").");
        besktop::LogWarning(L"IDesktopWallpaper creation failed: " + FormatHResult(createResult));
        if (shouldUninitialize) {
            CoUninitialize();
        }
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

    desktopWallpaper->Release();
    if (shouldUninitialize) {
        CoUninitialize();
    }
}

void AddDemoIconFallbacks(besktop::DesktopSnapshot& snapshot)
{
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
        icon.displayName = L"Demo Icon " + std::to_wstring(index + 1);
        icon.bounds = RECT{left, top, left + iconWidth, top + iconHeight};
        icon.usedFallback = true;
        snapshot.icons.push_back(icon);
    }

    snapshot.warnings.push_back(L"Real desktop icon scanning is not implemented; using demo icon fallback.");
    besktop::LogWarning(L"real desktop icon scanning is not implemented; using demo icon fallback");
}

} // namespace

namespace besktop {

DesktopSnapshot CaptureDesktopSnapshot()
{
    DesktopSnapshot snapshot;
    CapturePrimaryMonitorBounds(snapshot);
    CaptureWallpaperSnapshot(snapshot);
    AddDemoIconFallbacks(snapshot);
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
