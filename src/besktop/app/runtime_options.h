#pragma once

#include "besktop/animation/action_clip.h"
#include "besktop/animation/combat_pair.h"

namespace besktop {

enum class RuntimeExperienceMode {
    Wandering,
    CombatDirector,
    FixedCombatPreview,
    TurnPreview,
    ActionPreview,
};

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
    CombatScenarioId combatPreview = CombatScenarioId::None;
    bool invalidCombatPreview = false;
    bool combatDirectorEnabled = true;
    bool combatDirectorDiagnosticsEnabled = false;
    bool actionOrbitCameraEnabled = false;
    bool turnPreviewEnabled = false;
};

RuntimeOptions LoadRuntimeOptions();
const RuntimeOptions& GetRuntimeOptions();
RuntimeExperienceMode ResolveRuntimeExperienceMode(const RuntimeOptions& options);

} // namespace besktop
