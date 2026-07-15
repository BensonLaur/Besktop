#pragma once

#include <windows.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "besktop/desktop/desktop_snapshot.h"
#include "besktop/animation/action_player.h"
#include "besktop/animation/active_encounter_pool.h"
#include "besktop/animation/actor_ecosystem.h"
#include "besktop/animation/awakening_director.h"
#include "besktop/animation/combat_pair.h"
#include "besktop/animation/combat_director.h"
#include "besktop/animation/encounter_arbiter.h"
#include "besktop/animation/encounter_director.h"
#include "besktop/animation/turn_motion.h"
#include "besktop/render/icon_image_cache.h"

namespace besktop {

class IconFightScene {
public:
    IconFightScene();
    ~IconFightScene();

    IconFightScene(const IconFightScene&) = delete;
    IconFightScene& operator=(const IconFightScene&) = delete;

    struct RenderTimings {
        double poseMs = 0.0;
        double actorPrepMs = 0.0;
        double limbsMs = 0.0;
        double iconBodyMs = 0.0;
        double labelMs = 0.0;
    };

    struct ActorPose {
        enum class Heading {
            MoveRight,
            MoveLeft,
            FacingOut,
        };

        double x = 0.0;
        double y = 0.0;
        double bob = 0.0;
        double bodyEffect = 0.0;
        double rotateX = 0.0;
        double rotateY = 0.0;
        double rotateZ = 0.0;
        double observationOrbitYaw = 0.0;
        double facing = 1.0;
        double punch = 0.0;
        double kick = 0.0;
        double dodge = 0.0;
        double hit = 0.0;
        double limbGrow = 0.0;
        double labelAlpha = 1.0;
        double gait = 0.0;
        double walkPhase = 0.0;
        double locomotionWeight = 0.0;
        Heading heading = Heading::MoveRight;
        bool attackingRight = true;
        ActionSample action{};
    };

    void Reset(const DesktopSnapshot& snapshot, const RECT& clientRect);
    void Update(double elapsedSeconds);
    void Render(HDC hdc, const RECT& clientRect, RenderTimings* timings = nullptr) const;
    bool ToggleAutomaticInteractions();

    // Public for the small free functions in the implementation file; this is
    // still an internal scene type, not a plugin-facing API.
    enum class ScenePhase {
        Sleeping,
        Awakening,
        GrowingLimbs,
        Wandering,
    };

    struct IconActor {
        std::wstring label;
        double baseX = 0.0;
        double baseY = 0.0;
        // Legacy preview centers retained for the reusable locomotion prototype.
        double battleX = 0.0;
        double battleY = 0.0;
        double x = 0.0;
        double y = 0.0;
        double targetX = 0.0;
        double targetY = 0.0;
        double walkSpeed = 80.0;
        double waitRemaining = 0.0;
        double reservationAvoidanceCooldown = 0.0;
        double awakeningStartSeconds = 0.0;
        double walkPhase = 0.0;
        double locomotionWeight = 0.0;
        double turnPoseWeight = 1.0;
        TurnMotionState turnMotion{};
        std::uint32_t randomState = 1;
        RECT labelBounds{};
        bool usedLabelBoundsFallback = true;
        double planeWidth = 48.0;
        double planeHeight = 48.0;
        bool usedPlaneFallback = true;
        std::wstring planeSizeSource;
        int role = 0;
        unsigned char red = 80;
        unsigned char green = 140;
        unsigned char blue = 240;
        const IconImage* iconImage = nullptr;
        bool usedIconImageFallback = true;
        bool actionPreviewActor = false;
        double actionPreviewPauseRemaining = 0.45;
        bool actionOrbitCameraEnabled = false;
        double actionOrbitElapsedSeconds = 0.0;
        bool turnPreviewActor = false;
        double turnPreviewPauseRemaining = 0.65;
        bool combatPreviewActor = false;
        bool combatImpactVisible = false;
        bool combatBlockedImpact = false;
        ActionPlayer actionPlayer;
        ActionSample actionSample{};
        ActorBehaviorProfile behaviorProfile{};
        ActorRuntimeState runtimeState{};
        EncounterPoseSample encounterPose{};
        ActionId pendingCombatAction = ActionId::None;
        ActionSample combatBlendFrom{};
        double combatBlendElapsed = 0.0;
        double combatBlendDuration = 0.0;
    };

private:
    struct RenderCache;
    ScenePhase DeterminePhase(double elapsedSeconds) const;
    void LogPhase(ScenePhase phase);
    void ChooseWanderTarget(IconActor& actor);
    void ChooseWanderTargetAwayFrom(IconActor& actor, double avoidX, double direction);
    void UpdateCombatPairActors(
        std::size_t attackerIndex,
        std::size_t defenderIndex,
        CombatScenarioId scenario,
        bool directorInteraction,
        CombatPairState& pairState,
        double attackerStationX,
        double defenderStationX,
        double stationY,
        TurnFacing attackerFacing,
        TurnFacing defenderFacing,
        CombatPairPhase& loggedPhase,
        double deltaSeconds,
        double actionDeltaSeconds);
    void UpdateCombatDirector(double deltaSeconds, double actionDeltaSeconds);
    void UpdateAwakeningSchedule();
    void ConfigureCombatStations(
        std::size_t attackerIndex,
        std::size_t defenderIndex,
        CombatScenarioId scenario,
        double centerX,
        double centerY);

    std::vector<IconActor> actors_;
    mutable std::vector<ActorPose> poseCache_;
    std::unique_ptr<RenderCache> renderCache_;
    IconImageCache iconImageCache_;
    RECT monitorBounds_{};
    RECT clientBounds_{};
    RECT wanderBounds_{};
    bool usingCapturedWorkArea_ = false;
    double elapsedSeconds_ = 0.0;
    double previousElapsedSeconds_ = 0.0;
    ScenePhase phase_ = ScenePhase::Sleeping;
    ActionId previewAction_ = ActionId::None;
    CombatScenarioId combatPreview_ = CombatScenarioId::None;
    CombatPairState combatPairState_{};
    AwakeningDirectorState awakeningDirectorState_{};
    std::vector<AwakeningActorInput> awakeningInputs_;
    std::vector<AwakeningActorObservation> awakeningObservations_;
    bool awakeningReadinessLogged_ = false;
    bool awakeningCompletionLogged_ = false;
    ActiveEncounterPoolState activeEncounterPool_{};
    std::vector<ActiveEncounterActorInput> activeEncounterActorInputs_;
    std::vector<ActorPerceptionInput> ecosystemPerceptionInputs_;
    std::vector<ActorRuntimeState> ecosystemRuntimeSnapshots_;
    std::vector<LocalIntent> ecosystemIntentSnapshot_;
    std::vector<EncounterArbiterActor> encounterArbiterActors_;
    bool combatDirectorEnabled_ = false;
    bool combatDirectorDiagnosticsEnabled_ = false;
    double combatStationLeftX_ = 0.0;
    double combatStationRightX_ = 0.0;
    double combatStationY_ = 0.0;
    std::size_t combatDirectorAvoidanceReplans_ = 0;
    std::size_t loggedActiveEncounterCount_ = 0;
    CombatPairPhase loggedCombatPhase_ = CombatPairPhase::Inactive;
    bool actionOrbitCameraEnabled_ = false;
    bool turnPreviewEnabled_ = false;
    std::wstring automaticInteractionToast_;
    ULONGLONG automaticInteractionToastStartTick_ = 0;
};

} // namespace besktop
