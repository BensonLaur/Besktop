#include "besktop/app/stage_guide_external_action.h"

namespace besktop {

StageGuideExternalDispatchResult ExecuteStageGuideExternalAction(
    StageGuideExternalAction action,
    const StageGuideExternalActionHandlers& handlers)
{
    if (action == StageGuideExternalAction::None) {
        return StageGuideExternalDispatchResult::NoAction;
    }
    if (!handlers.stopAnimation || !handlers.destroyStageWindow ||
        !handlers.stageWindowDestroyed || !handlers.dispatchApprovedAction ||
        !handlers.finishExit) {
        return StageGuideExternalDispatchResult::InvalidHandler;
    }

    handlers.stopAnimation();
    handlers.destroyStageWindow();
    if (!handlers.stageWindowDestroyed()) {
        return StageGuideExternalDispatchResult::WindowDestroyFailed;
    }
    const bool dispatched = handlers.dispatchApprovedAction(action);
    handlers.finishExit();
    if (!dispatched) {
        return StageGuideExternalDispatchResult::DispatchFailed;
    }
    return StageGuideExternalDispatchResult::Completed;
}

} // namespace besktop
