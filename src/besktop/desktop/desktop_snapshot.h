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

struct DesktopIconImageSnapshot {
    std::wstring sourcePath;
    bool usedFallback = true;
    std::wstring warning;
};

struct DesktopIconDisplayMetrics {
    SIZE imageListIconSize{};
    bool usedFallback = true;
    std::wstring source;
};

struct DesktopIconSnapshot {
    std::wstring id;
    std::wstring displayName;
    POINT listViewPosition{};
    RECT bounds{};
    RECT iconBounds{};
    RECT labelBounds{};
    bool usedIconBoundsFallback = true;
    bool usedLabelBoundsFallback = true;
    DesktopIconImageSnapshot image;
    bool usedFallback = true;
};

struct DesktopSnapshot {
    RECT monitorBounds{};
    WallpaperSnapshot wallpaper;
    DesktopIconDisplayMetrics iconDisplay;
    std::vector<DesktopIconSnapshot> icons;
    std::vector<std::wstring> warnings;
};

DesktopSnapshot CaptureDesktopSnapshot();

std::wstring ToDisplayString(WallpaperLayout layout);

} // namespace besktop
