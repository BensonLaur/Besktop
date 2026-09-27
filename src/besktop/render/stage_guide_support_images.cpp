#include "besktop/render/stage_guide_support_images.h"
#include "besktop/resources/resource.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace besktop {

StageGuideSupportImages::StageGuideSupportImages()
{
    Gdiplus::GdiplusStartupInput input;
    if (Gdiplus::GdiplusStartup(&token_, &input, nullptr) != Gdiplus::Ok) token_ = 0;
}

StageGuideSupportImages::~StageGuideSupportImages()
{
    Clear();
    if (token_ != 0) Gdiplus::GdiplusShutdown(token_);
}

void StageGuideSupportImages::Clear()
{
    for (auto& entry : images_) {
        // GDI+ may read the source lazily. Destroy the image before its stream.
        entry.bitmap.reset();
        if (entry.stream != nullptr) entry.stream->Release();
        entry.stream = nullptr;
    }
}

bool StageGuideSupportImages::Load(HMODULE module)
{
    // A failed reload must invalidate previously cached payment pixels too.
    ++revision_;
    Clear();
    if (token_ == 0) return false;
    const int ids[]{IDR_BESKTOP_SUPPORT_WECHAT, IDR_BESKTOP_SUPPORT_ALIPAY};
    for (std::size_t index = 0; index < images_.size(); ++index) {
        const HRSRC resource = FindResourceW(module, MAKEINTRESOURCEW(ids[index]), RT_RCDATA);
        const DWORD size = resource != nullptr ? SizeofResource(module, resource) : 0;
        const HGLOBAL loaded = resource != nullptr ? LoadResource(module, resource) : nullptr;
        const void* source = loaded != nullptr ? LockResource(loaded) : nullptr;
        if (source == nullptr || size == 0) { Clear(); return false; }
        const HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, size);
        if (memory == nullptr) { Clear(); return false; }
        void* destination = GlobalLock(memory);
        if (destination == nullptr) { GlobalFree(memory); Clear(); return false; }
        std::memcpy(destination, source, size);
        GlobalUnlock(memory);
        auto& entry = images_[index];
        if (FAILED(CreateStreamOnHGlobal(memory, TRUE, &entry.stream))) {
            GlobalFree(memory);
            Clear();
            return false;
        }
        entry.bitmap.reset(Gdiplus::Bitmap::FromStream(entry.stream));
        if (entry.bitmap == nullptr || entry.bitmap->GetLastStatus() != Gdiplus::Ok ||
            entry.bitmap->GetWidth() == 0 || entry.bitmap->GetHeight() == 0) {
            Clear();
            return false;
        }
    }
    return true;
}

bool StageGuideSupportImages::IsReady() const
{
    return images_[0].bitmap != nullptr && images_[1].bitmap != nullptr;
}

bool StageGuideSupportImages::Draw(Gdiplus::Graphics& graphics,
    StageGuideSupportProvider provider, const StageGuideRect& bounds) const
{
    if (!IsReady() || !StageGuideRectIsValid(bounds)) return false;
    auto* bitmap = images_[provider == StageGuideSupportProvider::WeChat ? 0 : 1].bitmap.get();
    const double width = bitmap->GetWidth();
    const double height = bitmap->GetHeight();
    const double scale = std::min((bounds.right - bounds.left) / width,
        (bounds.bottom - bounds.top) / height);
    const Gdiplus::Rect target(
        static_cast<INT>(std::round((bounds.left + bounds.right - width * scale) * 0.5)),
        static_cast<INT>(std::round((bounds.top + bounds.bottom - height * scale) * 0.5)),
        std::max(1, static_cast<INT>(std::round(width * scale))),
        std::max(1, static_cast<INT>(std::round(height * scale))));
    const auto saved = graphics.Save();
    // Keep the original poster, quiet zones and payment identity intact; avoid
    // smoothing black/white QR modules or applying the character's opacity.
    graphics.SetInterpolationMode(Gdiplus::InterpolationModeNearestNeighbor);
    graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeNone);
    const auto status = graphics.DrawImage(bitmap, target, 0, 0,
        static_cast<INT>(width), static_cast<INT>(height), Gdiplus::UnitPixel);
    graphics.Restore(saved);
    return status == Gdiplus::Ok;
}

} // namespace besktop
