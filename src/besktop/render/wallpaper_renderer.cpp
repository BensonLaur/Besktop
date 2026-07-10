#include "besktop/render/wallpaper_renderer.h"

#include "besktop/app/runtime_options.h"
#include "besktop/logging/logger.h"

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

bool FrameTraceEnabled()
{
    return besktop::GetRuntimeOptions().frameTraceEnabled;
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
    if (gdiplusReady_) {
        LogInfo(L"GDI+ startup succeeded");
    } else {
        LogWarning(L"GDI+ startup failed");
    }
}

WallpaperRenderer::~WallpaperRenderer()
{
    ResetRenderedBitmap();
    ResetCachedImage();
    if (gdiplusReady_) {
        Gdiplus::GdiplusShutdown(gdiplusToken_);
        gdiplusToken_ = 0;
        gdiplusReady_ = false;
        LogInfo(L"GDI+ shutdown");
    }
}

void WallpaperRenderer::ResetCachedImage()
{
    cachedImage_.reset();
    cachedWallpaperPath_.clear();
}

void WallpaperRenderer::ResetRenderedBitmap()
{
    if (cachedRenderedBitmap_ != nullptr) {
        DeleteObject(cachedRenderedBitmap_);
        cachedRenderedBitmap_ = nullptr;
    }
    cachedRenderedSize_ = {};
    cachedRenderedWallpaperLayout_ = WallpaperLayout::Unknown;
}

bool WallpaperRenderer::EnsureImageLoaded(const WallpaperSnapshot& wallpaper)
{
    if (!gdiplusReady_) {
        return false;
    }
    if (!FileExists(wallpaper.path)) {
        return false;
    }
    if (cachedImage_ != nullptr && cachedWallpaperPath_ == wallpaper.path) {
        return true;
    }

    ResetRenderedBitmap();
    auto image = std::make_unique<Gdiplus::Image>(wallpaper.path.c_str());
    if (image->GetLastStatus() != Gdiplus::Ok || image->GetWidth() == 0 || image->GetHeight() == 0) {
        ResetCachedImage();
        return false;
    }

    cachedImage_ = std::move(image);
    cachedWallpaperPath_ = wallpaper.path;
    LogInfo(
        L"wallpaper image cached: " +
        std::to_wstring(cachedImage_->GetWidth()) +
        L" x " +
        std::to_wstring(cachedImage_->GetHeight()));
    return true;
}

bool WallpaperRenderer::EnsureRenderedBitmap(HDC hdc, const RECT& target, const WallpaperSnapshot& wallpaper)
{
    if (!gdiplusReady_ || hdc == nullptr || RectWidth(target) <= 0 || RectHeight(target) <= 0) {
        return false;
    }
    if (!EnsureImageLoaded(wallpaper)) {
        return false;
    }

    const int width = RectWidth(target);
    const int height = RectHeight(target);
    if (cachedRenderedBitmap_ != nullptr &&
        cachedRenderedSize_.cx == width &&
        cachedRenderedSize_.cy == height &&
        cachedWallpaperPath_ == wallpaper.path &&
        cachedRenderedWallpaperLayout_ == wallpaper.layout) {
        return true;
    }

    Gdiplus::Status drawStatus = Gdiplus::GenericError;
    {
        const bool trace = FrameTraceEnabled();
        if (trace) {
            LogInfo(L"wallpaper cache trace: creating graphics");
        }
        Gdiplus::Graphics graphics(hdc);
        if (graphics.GetLastStatus() != Gdiplus::Ok) {
            return false;
        }

        graphics.SetInterpolationMode(Gdiplus::InterpolationModeBilinear);
        graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
        if (trace) {
            LogInfo(L"wallpaper cache trace: graphics configured");
        }

        const Gdiplus::RectF targetRect = ToRectF(target);
        Gdiplus::GraphicsState state = graphics.Save();
        graphics.SetClip(targetRect);

        if (wallpaper.layout == WallpaperLayout::Tile) {
            if (trace) {
                LogInfo(L"wallpaper cache trace: drawing tiled wallpaper");
            }
            Gdiplus::TextureBrush brush(cachedImage_.get(), Gdiplus::WrapModeTile);
            drawStatus = graphics.FillRectangle(&brush, targetRect);
        } else {
            const Gdiplus::RectF imageRect =
                CalculateImageRect(target, cachedImage_->GetWidth(), cachedImage_->GetHeight(), wallpaper.layout);
            if (trace) {
                LogInfo(L"wallpaper cache trace: drawing scaled wallpaper");
            }
            drawStatus = graphics.DrawImage(cachedImage_.get(), imageRect);
        }
        graphics.Restore(state);
    }
    if (FrameTraceEnabled()) {
        LogInfo(L"wallpaper cache trace: wallpaper draw call returned");
    }

    if (drawStatus != Gdiplus::Ok) {
        return false;
    }

    if (FrameTraceEnabled()) {
        LogInfo(L"wallpaper cache trace: creating compatible cache bitmap");
    }
    HDC memoryHdc = CreateCompatibleDC(hdc);
    HBITMAP renderedBitmap = CreateCompatibleBitmap(hdc, width, height);
    if (memoryHdc == nullptr || renderedBitmap == nullptr) {
        if (renderedBitmap != nullptr) {
            DeleteObject(renderedBitmap);
        }
        if (memoryHdc != nullptr) {
            DeleteDC(memoryHdc);
        }
        return true;
    }

    HGDIOBJ previousBitmap = SelectObject(memoryHdc, renderedBitmap);
    if (FrameTraceEnabled()) {
        LogInfo(L"wallpaper cache trace: copying rendered wallpaper into cache bitmap");
    }
    const BOOL copied = BitBlt(memoryHdc, 0, 0, width, height, hdc, target.left, target.top, SRCCOPY);
    if (FrameTraceEnabled()) {
        LogInfo(L"wallpaper cache trace: cache bitmap copy returned");
    }
    SelectObject(memoryHdc, previousBitmap);
    DeleteDC(memoryHdc);
    if (copied == FALSE) {
        DeleteObject(renderedBitmap);
        return true;
    }

    if (FrameTraceEnabled()) {
        LogInfo(L"wallpaper cache trace: resetting previous rendered bitmap");
    }
    ResetRenderedBitmap();
    if (FrameTraceEnabled()) {
        LogInfo(L"wallpaper cache trace: storing rendered bitmap handle");
    }
    cachedRenderedBitmap_ = renderedBitmap;
    cachedRenderedSize_.cx = width;
    cachedRenderedSize_.cy = height;
    if (FrameTraceEnabled()) {
        LogInfo(L"wallpaper cache trace: storing rendered bitmap metadata");
    }
    cachedRenderedWallpaperLayout_ = wallpaper.layout;
    if (FrameTraceEnabled()) {
        LogInfo(L"wallpaper cache trace: rendered bitmap metadata stored");
    }
    LogInfo(
        L"wallpaper rendered bitmap cached: " +
        std::to_wstring(width) +
        L" x " +
        std::to_wstring(height));
    return true;
}

bool WallpaperRenderer::Draw(HDC hdc, const RECT& target, const WallpaperSnapshot& wallpaper)
{
    if (!EnsureRenderedBitmap(hdc, target, wallpaper)) {
        return false;
    }
    if (cachedRenderedBitmap_ == nullptr) {
        return true;
    }

    HDC memoryHdc = CreateCompatibleDC(hdc);
    if (memoryHdc == nullptr) {
        return false;
    }
    HGDIOBJ previousBitmap = SelectObject(memoryHdc, cachedRenderedBitmap_);
    const BOOL copied = BitBlt(
        hdc,
        target.left,
        target.top,
        RectWidth(target),
        RectHeight(target),
        memoryHdc,
        0,
        0,
        SRCCOPY);
    SelectObject(memoryHdc, previousBitmap);
    DeleteDC(memoryHdc);
    return copied != FALSE;
}

} // namespace besktop
