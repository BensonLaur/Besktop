#pragma once

#include <windows.h>

#include "besktop/desktop/desktop_snapshot.h"

#include <memory>
#include <string>

namespace Gdiplus {
class Image;
}

namespace besktop {

class WallpaperRenderer {
public:
    WallpaperRenderer();
    ~WallpaperRenderer();

    WallpaperRenderer(const WallpaperRenderer&) = delete;
    WallpaperRenderer& operator=(const WallpaperRenderer&) = delete;

    bool Draw(HDC hdc, const RECT& target, const WallpaperSnapshot& wallpaper);

private:
    bool EnsureImageLoaded(const WallpaperSnapshot& wallpaper);
    bool EnsureRenderedBitmap(HDC hdc, const RECT& target, const WallpaperSnapshot& wallpaper);
    void ResetCachedImage();
    void ResetRenderedBitmap();

    ULONG_PTR gdiplusToken_ = 0;
    bool gdiplusReady_ = false;
    std::unique_ptr<Gdiplus::Image> cachedImage_;
    std::wstring cachedWallpaperPath_;
    HBITMAP cachedRenderedBitmap_ = nullptr;
    SIZE cachedRenderedSize_{};
    WallpaperLayout cachedRenderedWallpaperLayout_ = WallpaperLayout::Unknown;
};

} // namespace besktop
