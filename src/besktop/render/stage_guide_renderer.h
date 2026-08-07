#pragma once

#include "besktop/animation/stage_guide_layout.h"
#include "besktop/animation/stage_guide_npc.h"

namespace Gdiplus {
class Graphics;
}

namespace besktop {

void DrawStageGuideNpc(
    Gdiplus::Graphics& graphics,
    const StageGuideNpcState& state,
    const StageGuideLayout& layout);

} // namespace besktop
