#pragma once

#include "besktop/animation/action_clip.h"

namespace besktop {

struct RuntimeOptions {
    bool developerBuild = false;
    bool diagnosticsEnabled = false;
    bool verboseInfoLogging = false;
    bool frameStatsEnabled = false;
    bool frameTraceEnabled = false;
    bool debugIconPlaneEnabled = false;
    bool renderShadowsEnabled = false;
    unsigned int maxActors = 0;
    double animationSpeed = 1.0;
    double animationOffsetSeconds = 0.0;
    ActionId actionPreview = ActionId::None;
    bool invalidActionPreview = false;
};

const RuntimeOptions& GetRuntimeOptions();

} // namespace besktop
