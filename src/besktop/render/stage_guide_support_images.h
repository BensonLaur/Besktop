#pragma once

#include "besktop/animation/stage_guide_layout.h"

#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>

#include <array>
#include <cstdint>
#include <memory>

namespace besktop {

// Owns the GDI+ lifetime and streams independently of desktop icon caches.
class StageGuideSupportImages {
public:
    StageGuideSupportImages();
    ~StageGuideSupportImages();
    StageGuideSupportImages(const StageGuideSupportImages&) = delete;
    StageGuideSupportImages& operator=(const StageGuideSupportImages&) = delete;

    bool Load(HMODULE module);
    bool IsReady() const;
    std::uint64_t Revision() const { return revision_; }
    bool Draw(Gdiplus::Graphics& graphics, StageGuideSupportProvider provider,
        const StageGuideRect& bounds) const;

private:
    void Clear();
    struct Entry {
        IStream* stream = nullptr;
        std::unique_ptr<Gdiplus::Bitmap> bitmap;
    };
    ULONG_PTR token_ = 0;
    std::uint64_t revision_ = 0;
    std::array<Entry, 2> images_;
};

} // namespace besktop
