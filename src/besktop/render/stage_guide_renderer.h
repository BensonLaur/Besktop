#pragma once

#include "besktop/animation/stage_guide_layout.h"
#include "besktop/animation/stage_guide_npc.h"

#include <cstddef>
#include <memory>

namespace Gdiplus {
class Graphics;
}

namespace besktop {

class StageGuideSupportImages;

// Scene-owned, single-card cache. Destroy before the scene's GDI+ owner.
// Animated character/menu state is deliberately excluded from cached pixels.
class StageGuideCardCache {
public:
    StageGuideCardCache();
    ~StageGuideCardCache();
    StageGuideCardCache(const StageGuideCardCache&) = delete;
    StageGuideCardCache& operator=(const StageGuideCardCache&) = delete;

    void Clear();
    std::size_t BuildCount() const;
    bool Draw(Gdiplus::Graphics& graphics, const StageGuideNpcState& state,
        const StageGuideLayout& layout, const StageGuideSupportImages* images);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

void DrawStageGuideNpc(
    Gdiplus::Graphics& graphics,
    const StageGuideNpcState& state,
    const StageGuideLayout& layout,
    const StageGuideSupportImages* supportImages = nullptr,
    StageGuideCardCache* cardCache = nullptr);

} // namespace besktop
