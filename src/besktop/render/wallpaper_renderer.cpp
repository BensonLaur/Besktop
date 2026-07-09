#include "besktop/render/wallpaper_renderer.h"

#include <objidl.h>
#include <gdiplus.h>

#include <algorithm>

namespace {

int RectWidth(const RECT& rect)
{
    return rect.right - rect.left;
}

int RectHeight(const RECT& rect)
{
    return rect.bottom - rect.top;
}

bool FileExists(const std::wstring& path)
{
    if (path.empty()) {
        return false;
    }

    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

Gdiplus::RectF ToRectF(const RECT& rect)
{
    return Gdiplus::RectF(
        static_cast<Gdiplus::REAL>(rect.left),
        static_cast<Gdiplus::REAL>(rect.top),
        static_cast<Gdiplus::REAL>(RectWidth(rect)),
        static_cast<Gdiplus::REAL>(RectHeight(rect)));
}

Gdiplus::RectF MakeCenteredRect(const RECT& target, double width, double height)
{
    const double targetWidth = RectWidth(target);
    const double targetHeight = RectHeight(target);
    const double left = target.left + ((targetWidth - width) / 2.0);
    const double top = target.top + ((targetHeight - height) / 2.0);

    return Gdiplus::RectF(
        static_cast<Gdiplus::REAL>(left),
        static_cast<Gdiplus::REAL>(top),
        static_cast<Gdiplus::REAL>(width),
        static_cast<Gdiplus::REAL>(height));
}

Gdiplus::RectF CalculateImageRect(const RECT& target, UINT imageWidth, UINT imageHeight, besktop::WallpaperLayout layout)
{
    const double targetWidth = RectWidth(target);
    const double targetHeight = RectHeight(target);
    const double sourceWidth = static_cast<double>(imageWidth);
    const double sourceHeight = static_cast<double>(imageHeight);

    switch (layout) {
    case besktop::WallpaperLayout::Stretch:
        return ToRectF(target);
    case besktop::WallpaperLayout::Center:
        return MakeCenteredRect(target, sourceWidth, sourceHeight);
    case besktop::WallpaperLayout::Fit: {
        const double scale = std::min(targetWidth / sourceWidth, targetHeight / sourceHeight);
        return MakeCenteredRect(target, sourceWidth * scale, sourceHeight * scale);
    }
    case besktop::WallpaperLayout::Fill:
    case besktop::WallpaperLayout::Span:
    case besktop::WallpaperLayout::Unknown:
    default: {
        const double scale = std::max(targetWidth / sourceWidth, targetHeight / sourceHeight);
        return MakeCenteredRect(target, sourceWidth * scale, sourceHeight * scale);
    }
    }
}

} // namespace

namespace besktop {

WallpaperRenderer::WallpaperRenderer()
{
    Gdiplus::GdiplusStartupInput startupInput;
    gdiplusReady_ = Gdiplus::GdiplusStartup(&gdiplusToken_, &startupInput, nullptr) == Gdiplus::Ok;
}

WallpaperRenderer::~WallpaperRenderer()
{
    if (gdiplusReady_) {
        Gdiplus::GdiplusShutdown(gdiplusToken_);
        gdiplusToken_ = 0;
        gdiplusReady_ = false;
    }
}

bool WallpaperRenderer::Draw(HDC hdc, const RECT& target, const WallpaperSnapshot& wallpaper)
{
    if (!gdiplusReady_ || hdc == nullptr || RectWidth(target) <= 0 || RectHeight(target) <= 0) {
        return false;
    }
    if (!FileExists(wallpaper.path)) {
        return false;
    }

    Gdiplus::Image image(wallpaper.path.c_str());
    if (image.GetLastStatus() != Gdiplus::Ok || image.GetWidth() == 0 || image.GetHeight() == 0) {
        return false;
    }

    Gdiplus::Graphics graphics(hdc);
    if (graphics.GetLastStatus() != Gdiplus::Ok) {
        return false;
    }

    graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
    graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);

    const Gdiplus::RectF targetRect = ToRectF(target);
    Gdiplus::GraphicsState state = graphics.Save();
    graphics.SetClip(targetRect);

    Gdiplus::Status drawStatus = Gdiplus::GenericError;
    if (wallpaper.layout == WallpaperLayout::Tile) {
        Gdiplus::TextureBrush brush(&image, Gdiplus::WrapModeTile);
        drawStatus = graphics.FillRectangle(&brush, targetRect);
    } else {
        const Gdiplus::RectF imageRect =
            CalculateImageRect(target, image.GetWidth(), image.GetHeight(), wallpaper.layout);
        drawStatus = graphics.DrawImage(&image, imageRect);
    }

    graphics.Restore(state);
    return drawStatus == Gdiplus::Ok;
}

} // namespace besktop
