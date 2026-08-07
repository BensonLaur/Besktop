#pragma once

#include "besktop/animation/stage_guide_layout.h"

#include <cstdint>
#include <span>

namespace besktop {

enum class StageGuideNpcPhase {
    Dormant,
    Entering,
    Roaming,
    NoticingPointer,
    PresentingMenu,
    ShowingAbout,
    ConfirmingExternalAction,
    LeavingForExternalAction,
};

enum class StageGuideExternalAction {
    None,
    Feedback,
    Support,
};

struct StageGuideReservation {
    StageGuidePoint center{};
    double radius = 0.0;
};

struct StageGuideNpcTuning {
    double postEncounterDelayMinimumSeconds = 1.2;
    double postEncounterDelayMaximumSeconds = 2.0;
    double fallbackEntryMinimumSeconds = 20.0;
    double fallbackEntryMaximumSeconds = 30.0;
    double hoverDwellSeconds = 0.22;
    double menuExpandSeconds = 0.19;
    double pointerLeaveGraceSeconds = 0.52;
    double externalActionLeaveSeconds = 0.25;
    double enteringSpeed = 152.0;
    double roamingSpeed = 68.0;
    double roamingWaitMinimumSeconds = 0.70;
    double roamingWaitMaximumSeconds = 1.60;
    double bodyScale = 1.26;
    double reservationMarginScale = 1.42;
};

struct StageGuideNpcConfig {
    std::uint32_t randomSeed = 0xB17A6E21u;
    double referenceIconSize = 48.0;
    bool diagnosticPreview = false;
    StageGuideMenuAvailability availability{};
};

struct StageGuidePointerInput {
    bool insideWindow = false;
    bool insideBodyHover = false;
    bool insideInteractionRegion = false;
    StageGuidePoint position{};
    StageGuideHitTarget hitTarget = StageGuideHitTarget::None;
};

struct StageGuideNpcUpdateInput {
    double deltaSeconds = 0.0;
    StageGuideRect workArea{};
    std::span<const StageGuideReservation> reservations{};
    StageGuidePointerInput pointer{};
};

struct StageGuideNpcStep {
    bool phaseChanged = false;
    bool becameVisible = false;
    bool menuOpened = false;
    bool menuClosed = false;
    bool targetChanged = false;
    bool waitingForSafePath = false;
};

struct StageGuideNpcState {
    StageGuideNpcPhase phase = StageGuideNpcPhase::Dormant;
    StageGuidePoint position{};
    StageGuidePoint target{};
    StageGuidePoint firstEncounterCenter{};
    StageGuidePoint pointerPosition{};
    StageGuideMenuAvailability availability{};
    StageGuideMenuEntry confirmingEntry = StageGuideMenuEntry::None;
    StageGuideExternalAction pendingExternalAction = StageGuideExternalAction::None;
    std::uint32_t randomState = 0xB17A6E21u;
    double bodySize = 60.0;
    double startupElapsedSeconds = 0.0;
    double fallbackEntrySeconds = 25.0;
    double postEncounterDelayRemaining = 0.0;
    double hoverElapsedSeconds = 0.0;
    double menuProgress = 0.0;
    double pointerOutsideElapsedSeconds = 0.0;
    double roamingWaitRemaining = 0.0;
    double leavingElapsedSeconds = 0.0;
    double motionPhase = 0.0;
    bool diagnosticPreview = false;
    bool positionInitialized = false;
    bool firstEncounterConsumed = false;
    bool firstEncounterPending = false;
    bool entryPlanReady = false;
    bool targetValid = false;
    bool moving = false;
    bool externalActionIssued = false;
    bool pointerPresent = false;
};

const StageGuideNpcTuning& GetStageGuideNpcTuning();
const wchar_t* StageGuideNpcPhaseName(StageGuideNpcPhase phase);

void InitializeStageGuideNpc(
    StageGuideNpcState& state,
    const StageGuideNpcConfig& config);
bool NotifyStageGuideFirstEncounterCompleted(
    StageGuideNpcState& state,
    const StageGuidePoint& encounterCenter);
StageGuideNpcStep UpdateStageGuideNpc(
    StageGuideNpcState& state,
    const StageGuideNpcUpdateInput& input);
bool HandleStageGuideClick(
    StageGuideNpcState& state,
    StageGuideHitTarget target);
void ClearStageGuidePointerInteraction(
    StageGuideNpcState& state,
    bool dismissPanels);
StageGuideExternalAction ConsumeStageGuideExternalAction(StageGuideNpcState& state);

bool StageGuideNpcIsVisible(const StageGuideNpcState& state);
bool StageGuideNpcShowsMenu(const StageGuideNpcState& state);
bool StageGuideNpcShowsAbout(const StageGuideNpcState& state);
bool StageGuideNpcShowsExternalConfirmation(const StageGuideNpcState& state);
bool StageGuideNpcIsFrozen(const StageGuideNpcState& state);
bool StageGuidePointIsSafe(
    const StageGuidePoint& point,
    const StageGuideRect& workArea,
    std::span<const StageGuideReservation> reservations,
    double margin);
bool StageGuidePathIsSafe(
    const StageGuidePoint& from,
    const StageGuidePoint& to,
    std::span<const StageGuideReservation> reservations,
    double margin);

} // namespace besktop
