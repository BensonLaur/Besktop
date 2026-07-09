#pragma once

#include <windows.h>

#include <string>
#include <vector>

namespace besktop {

enum class WallpaperLayout {
    Unknown,
    Center,
    Tile,
    Stretch,
    Fit,
    Fill,
    Span,
};

struct WallpaperSnapshot {
    std::wstring path;
    WallpaperLayout layout = WallpaperLayout::Unknown;
    bool usedFallback = false;
};

struct DesktopIconSnapshot {
    std::wstring id;
    std::wstring displayName;
    RECT bounds{};
    bool usedFallback = true;
};

struct DesktopSnapshot {
    RECT monitorBounds{};
    WallpaperSnapshot wallpaper;
    std::vector<DesktopIconSnapshot> icons;
    std::vector<std::wstring> warnings;
};

DesktopSnapshot CaptureDesktopSnapshot();

std::wstring ToDisplayString(WallpaperLayout layout);

} // namespace besktop
