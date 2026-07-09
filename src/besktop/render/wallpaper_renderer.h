#pragma once

#include <windows.h>

#include "besktop/desktop/desktop_snapshot.h"

namespace besktop {

class WallpaperRenderer {
public:
    WallpaperRenderer();
    ~WallpaperRenderer();

    WallpaperRenderer(const WallpaperRenderer&) = delete;
    WallpaperRenderer& operator=(const WallpaperRenderer&) = delete;

    bool Draw(HDC hdc, const RECT& target, const WallpaperSnapshot& wallpaper);

private:
    ULONG_PTR gdiplusToken_ = 0;
    bool gdiplusReady_ = false;
};

} // namespace besktop
