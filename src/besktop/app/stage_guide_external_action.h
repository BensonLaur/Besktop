#pragma once

#include "besktop/animation/stage_guide_npc.h"

#include <functional>

namespace besktop {

enum class StageGuideExternalDispatchResult {
    NoAction,
    InvalidHandler,
    WindowDestroyFailed,
    DispatchFailed,
    Completed,
};

struct StageGuideExternalActionHandlers {
    std::function<void()> stopAnimation;
    std::function<void()> destroyStageWindow;
    std::function<bool()> stageWindowDestroyed;
    std::function<bool(StageGuideExternalAction)> dispatchApprovedAction;
};

StageGuideExternalDispatchResult ExecuteStageGuideExternalAction(
    StageGuideExternalAction action,
    const StageGuideExternalActionHandlers& handlers);

} // namespace besktop
