#include "besktop/animation/stage_guide_npc.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace {

double Clamp01(double value)
{
    return std::clamp(value, 0.0, 1.0);
}

double Distance(
    const besktop::StageGuidePoint& first,
    const besktop::StageGuidePoint& second)
{
    return std::hypot(first.x - second.x, first.y - second.y);
}

double Width(const besktop::StageGuideRect& rect)
{
    return std::max(0.0, rect.right - rect.left);
}

double Height(const besktop::StageGuideRect& rect)
{
    return std::max(0.0, rect.bottom - rect.top);
}

double NextUnit(besktop::StageGuideNpcState& state)
{
    state.randomState = state.randomState * 1664525u + 1013904223u;
    return static_cast<double>(state.randomState & 0x00FFFFFFu) / 16777215.0;
}

double NextRange(besktop::StageGuideNpcState& state, double minimum, double maximum)
{
    return minimum + (maximum - minimum) * NextUnit(state);
}

bool PointInsideReservation(
    const besktop::StageGuidePoint& point,
    const besktop::StageGuideReservation& reservation,
    double margin)
{
    return reservation.radius > 0.0 &&
        Distance(point, reservation.center) < reservation.radius + margin;
}

double DistancePointToSegment(
    const besktop::StageGuidePoint& point,
    const besktop::StageGuidePoint& from,
    const besktop::StageGuidePoint& to)
{
    const double dx = to.x - from.x;
    const double dy = to.y - from.y;
    const double lengthSquared = dx * dx + dy * dy;
    if (lengthSquared <= 1e-12) return Distance(point, from);
    const double t = std::clamp(
        ((point.x - from.x) * dx + (point.y - from.y) * dy) / lengthSquared,
        0.0,
        1.0);
    return std::hypot(
        point.x - (from.x + dx * t),
        point.y - (from.y + dy * t));
}

bool CurrentPositionInsideReservation(
    const besktop::StageGuideNpcState& state,
    std::span<const besktop::StageGuideReservation> reservations,
    double margin)
{
    return std::any_of(reservations.begin(), reservations.end(), [&](const auto& reservation) {
        return PointInsideReservation(state.position, reservation, margin);
    });
}

bool TargetMovesOutwardFromContainingReservations(
    const besktop::StageGuidePoint& from,
    const besktop::StageGuidePoint& to,
    std::span<const besktop::StageGuideReservation> reservations,
    double margin)
{
    bool foundContaining = false;
    for (const auto& reservation : reservations) {
        if (!PointInsideReservation(from, reservation, margin)) continue;
        foundContaining = true;
        if (Distance(to, reservation.center) <= Distance(from, reservation.center) + 1e-6) return false;
    }
    return foundContaining;
}

void SetPhase(
    besktop::StageGuideNpcState& state,
    besktop::StageGuideNpcPhase phase,
    besktop::StageGuideNpcStep& step)
{
    if (state.phase == phase) return;
    const bool menuWasVisible = besktop::StageGuideNpcShowsMenu(state);
    state.phase = phase;
    step.phaseChanged = true;
    const bool menuIsVisible = besktop::StageGuideNpcShowsMenu(state);
    step.menuOpened = step.menuOpened || (!menuWasVisible && menuIsVisible);
    step.menuClosed = step.menuClosed || (menuWasVisible && !menuIsVisible);
}

besktop::StageGuidePoint ClampPoint(
    const besktop::StageGuidePoint& point,
    const besktop::StageGuideRect& bounds,
    double margin)
{
    const double minimumX = bounds.left + margin;
    const double maximumX = bounds.right - margin;
    const double minimumY = bounds.top + margin;
    const double maximumY = bounds.bottom - margin;
    return {
        maximumX >= minimumX ? std::clamp(point.x, minimumX, maximumX) :
            (bounds.left + bounds.right) * 0.5,
        maximumY >= minimumY ? std::clamp(point.y, minimumY, maximumY) :
            (bounds.top + bounds.bottom) * 0.5,
    };
}

bool TryChooseRoamingTarget(
    besktop::StageGuideNpcState& state,
    const besktop::StageGuideRect& workArea,
    std::span<const besktop::StageGuideReservation> reservations,
    besktop::StageGuideNpcStep& step)
{
    const auto& tuning = besktop::GetStageGuideNpcTuning();
    const double margin = state.bodySize * tuning.reservationMarginScale;
    const double horizontalRange = workArea.right - workArea.left - margin * 2.0;
    const double verticalRange = workArea.bottom - workArea.top - margin * 2.0;
    if (horizontalRange <= 1.0 || verticalRange <= 1.0) return false;

    const bool currentlyInside = CurrentPositionInsideReservation(state, reservations, margin);
    for (int attempt = 0; attempt < 18; ++attempt) {
        const besktop::StageGuidePoint candidate{
            workArea.left + margin + horizontalRange * NextUnit(state),
            workArea.top + margin + verticalRange * NextUnit(state),
        };
        if (!besktop::StageGuidePointIsSafe(candidate, workArea, reservations, margin)) continue;
        const bool pathSafe = besktop::StageGuidePathIsSafe(
            state.position, candidate, reservations, margin);
        if (!pathSafe && !(currentlyInside && TargetMovesOutwardFromContainingReservations(
                state.position, candidate, reservations, margin))) {
            continue;
        }
        state.target = candidate;
        state.targetValid = true;
        state.roamingWaitRemaining = 0.0;
        step.targetChanged = true;
        return true;
    }
    state.targetValid = false;
    step.waitingForSafePath = true;
    return false;
}

bool TryPrepareEntry(
    besktop::StageGuideNpcState& state,
    const besktop::StageGuideRect& workArea,
    std::span<const besktop::StageGuideReservation> reservations,
    besktop::StageGuideNpcStep& step)
{
    const auto& tuning = besktop::GetStageGuideNpcTuning();
    const double margin = state.bodySize * tuning.reservationMarginScale;
    const double inset = state.bodySize * 1.15;
    const double centerX = (workArea.left + workArea.right) * 0.5;
    const double centerY = (workArea.top + workArea.bottom) * 0.5;
    const double laneX = std::clamp(
        workArea.left + Width(workArea) * (0.22 + NextUnit(state) * 0.56),
        workArea.left + margin,
        workArea.right - margin);
    const double laneY = std::clamp(
        workArea.top + Height(workArea) * (0.20 + NextUnit(state) * 0.58),
        workArea.top + margin,
        workArea.bottom - margin);
    struct Candidate {
        besktop::StageGuidePoint spawn;
        besktop::StageGuidePoint target;
        double score = 0.0;
    };
    std::array<Candidate, 4> candidates{{
        {{workArea.left - state.bodySize, laneY}, {workArea.left + inset, laneY}, 0.0},
        {{workArea.right + state.bodySize, laneY}, {workArea.right - inset, laneY}, 0.0},
        {{laneX, workArea.top - state.bodySize}, {laneX, workArea.top + inset}, 0.0},
        {{laneX, workArea.bottom + state.bodySize}, {laneX, workArea.bottom - inset}, 0.0},
    }};
    for (Candidate& candidate : candidates) {
        candidate.target = ClampPoint(candidate.target, workArea, margin);
        candidate.score = state.firstEncounterConsumed ?
            Distance(candidate.target, state.firstEncounterCenter) :
            Distance(candidate.target, {centerX, centerY});
    }
    std::sort(candidates.begin(), candidates.end(), [](const auto& first, const auto& second) {
        return first.score > second.score;
    });
    for (const Candidate& candidate : candidates) {
        if (!besktop::StageGuidePointIsSafe(candidate.target, workArea, reservations, margin) ||
            !besktop::StageGuidePathIsSafe(candidate.spawn, candidate.target, reservations, margin)) {
            continue;
        }
        state.position = candidate.spawn;
        state.target = candidate.target;
        state.positionInitialized = true;
        state.entryPlanReady = true;
        state.targetValid = true;
        state.moving = true;
        step.targetChanged = true;
        return true;
    }
    step.waitingForSafePath = true;
    return false;
}

void AdvanceTowards(
    besktop::StageGuideNpcState& state,
    double speed,
    double deltaSeconds)
{
    const double dx = state.target.x - state.position.x;
    const double dy = state.target.y - state.position.y;
    const double distance = std::hypot(dx, dy);
    if (distance <= 1.0) {
        state.position = state.target;
        state.moving = false;
        return;
    }
    const double amount = std::min(distance, std::max(0.0, speed * deltaSeconds));
    state.position.x += dx / distance * amount;
    state.position.y += dy / distance * amount;
    state.motionPhase += amount / std::max(1.0, state.bodySize * 1.35);
    state.moving = amount > 0.0 && amount + 1e-6 < distance;
}

void ReturnToRoaming(
    besktop::StageGuideNpcState& state,
    besktop::StageGuideNpcStep& step)
{
    state.hoverElapsedSeconds = 0.0;
    state.menuProgress = 0.0;
    state.pointerOutsideElapsedSeconds = 0.0;
    state.confirmingEntry = besktop::StageGuideMenuEntry::None;
    SetPhase(state, besktop::StageGuideNpcPhase::Roaming, step);
}

} // namespace

namespace besktop {

const StageGuideNpcTuning& GetStageGuideNpcTuning()
{
    static const StageGuideNpcTuning tuning;
    return tuning;
}

const wchar_t* StageGuideNpcPhaseName(StageGuideNpcPhase phase)
{
    switch (phase) {
    case StageGuideNpcPhase::Entering: return L"entering";
    case StageGuideNpcPhase::Roaming: return L"roaming";
    case StageGuideNpcPhase::NoticingPointer: return L"noticing-pointer";
    case StageGuideNpcPhase::PresentingMenu: return L"presenting-menu";
    case StageGuideNpcPhase::ShowingAbout: return L"showing-about";
    case StageGuideNpcPhase::ConfirmingExternalAction: return L"confirming-external-action";
    case StageGuideNpcPhase::LeavingForExternalAction: return L"leaving-for-external-action";
    case StageGuideNpcPhase::Dormant:
    default:
        return L"dormant";
    }
}

void InitializeStageGuideNpc(
    StageGuideNpcState& state,
    const StageGuideNpcConfig& config)
{
    state = {};
    state.randomState = config.randomSeed == 0 ? 0xB17A6E21u : config.randomSeed;
    state.bodySize = std::max(32.0,
        config.referenceIconSize * GetStageGuideNpcTuning().bodyScale);
    state.diagnosticPreview = config.diagnosticPreview;
    state.availability = config.availability;
    state.availability.about = true;
    state.fallbackEntrySeconds = NextRange(
        state,
        GetStageGuideNpcTuning().fallbackEntryMinimumSeconds,
        GetStageGuideNpcTuning().fallbackEntryMaximumSeconds);
    if (state.diagnosticPreview) state.fallbackEntrySeconds = 0.0;
}

bool NotifyStageGuideFirstEncounterCompleted(
    StageGuideNpcState& state,
    const StageGuidePoint& encounterCenter)
{
    if (state.firstEncounterConsumed) return false;
    state.firstEncounterConsumed = true;
    state.firstEncounterPending = true;
    state.firstEncounterCenter = encounterCenter;
    state.postEncounterDelayRemaining = NextRange(
        state,
        GetStageGuideNpcTuning().postEncounterDelayMinimumSeconds,
        GetStageGuideNpcTuning().postEncounterDelayMaximumSeconds);
    return true;
}

StageGuideNpcStep UpdateStageGuideNpc(
    StageGuideNpcState& state,
    const StageGuideNpcUpdateInput& input)
{
    StageGuideNpcStep step;
    if (!std::isfinite(input.deltaSeconds) || input.deltaSeconds < 0.0 ||
        !StageGuideRectIsValid(input.workArea)) {
        return step;
    }
    const double deltaSeconds = input.deltaSeconds;
    const double motionDeltaSeconds = std::min(deltaSeconds, 0.25);
    state.startupElapsedSeconds += deltaSeconds;
    state.pointerPresent = input.pointer.insideWindow;
    if (state.pointerPresent) state.pointerPosition = input.pointer.position;

    if (state.diagnosticPreview && !state.positionInitialized) {
        const double margin = state.bodySize * GetStageGuideNpcTuning().reservationMarginScale;
        StageGuidePoint preferred{
            input.workArea.left + (input.workArea.right - input.workArea.left) * 0.72,
            input.workArea.top + (input.workArea.bottom - input.workArea.top) * 0.56,
        };
        preferred = ClampPoint(preferred, input.workArea, margin);
        if (!StageGuidePointIsSafe(preferred, input.workArea, input.reservations, margin)) {
            state.position = {
                (input.workArea.left + input.workArea.right) * 0.5,
                (input.workArea.top + input.workArea.bottom) * 0.5,
            };
            TryChooseRoamingTarget(state, input.workArea, input.reservations, step);
            if (state.targetValid) state.position = state.target;
        } else {
            state.position = preferred;
        }
        state.positionInitialized = true;
        state.entryPlanReady = true;
        state.targetValid = false;
        SetPhase(state, StageGuideNpcPhase::Roaming, step);
        step.becameVisible = true;
    }

    if (state.phase == StageGuideNpcPhase::Dormant) {
        if (state.firstEncounterPending) {
            state.postEncounterDelayRemaining = std::max(
                0.0, state.postEncounterDelayRemaining - deltaSeconds);
        }
        const bool encounterReady = state.firstEncounterPending &&
            state.postEncounterDelayRemaining <= 0.0;
        const bool fallbackReady = state.startupElapsedSeconds >= state.fallbackEntrySeconds;
        if ((encounterReady || fallbackReady) &&
            TryPrepareEntry(state, input.workArea, input.reservations, step)) {
            state.firstEncounterPending = false;
            SetPhase(state, StageGuideNpcPhase::Entering, step);
            step.becameVisible = true;
        }
        return step;
    }

    const double reservationMargin =
        state.bodySize * GetStageGuideNpcTuning().reservationMarginScale;
    const bool reservationAtBody = CurrentPositionInsideReservation(
        state, input.reservations, reservationMargin);
    const bool unsafeCurrentPath = state.targetValid && !StageGuidePathIsSafe(
        state.position, state.target, input.reservations, reservationMargin) &&
        !(reservationAtBody && TargetMovesOutwardFromContainingReservations(
            state.position, state.target, input.reservations, reservationMargin));
    if ((reservationAtBody || unsafeCurrentPath) &&
        state.phase != StageGuideNpcPhase::Entering &&
        state.phase != StageGuideNpcPhase::LeavingForExternalAction) {
        if (StageGuideNpcIsFrozen(state)) ReturnToRoaming(state, step);
        state.targetValid = false;
        TryChooseRoamingTarget(state, input.workArea, input.reservations, step);
    }

    switch (state.phase) {
    case StageGuideNpcPhase::Entering:
        if (!StageGuidePathIsSafe(
                state.position, state.target, input.reservations, reservationMargin)) {
            state.entryPlanReady = false;
            state.targetValid = false;
            step.waitingForSafePath = true;
            break;
        }
        AdvanceTowards(state, GetStageGuideNpcTuning().enteringSpeed, motionDeltaSeconds);
        if (!state.moving) {
            state.targetValid = false;
            state.roamingWaitRemaining = NextRange(
                state,
                GetStageGuideNpcTuning().roamingWaitMinimumSeconds,
                GetStageGuideNpcTuning().roamingWaitMaximumSeconds);
            SetPhase(state, StageGuideNpcPhase::Roaming, step);
        }
        break;
    case StageGuideNpcPhase::Roaming:
        if (input.pointer.insideWindow && input.pointer.insideBodyHover) {
            state.hoverElapsedSeconds = 0.0;
            state.pointerOutsideElapsedSeconds = 0.0;
            state.moving = false;
            SetPhase(state, StageGuideNpcPhase::NoticingPointer, step);
            break;
        }
        if (state.roamingWaitRemaining > 0.0) {
            state.roamingWaitRemaining = std::max(
                0.0, state.roamingWaitRemaining - deltaSeconds);
            state.moving = false;
            break;
        }
        if (!state.targetValid &&
            !TryChooseRoamingTarget(state, input.workArea, input.reservations, step)) {
            state.moving = false;
            break;
        }
        AdvanceTowards(state, GetStageGuideNpcTuning().roamingSpeed, motionDeltaSeconds);
        if (!state.moving) {
            state.targetValid = false;
            state.roamingWaitRemaining = NextRange(
                state,
                GetStageGuideNpcTuning().roamingWaitMinimumSeconds,
                GetStageGuideNpcTuning().roamingWaitMaximumSeconds);
        }
        break;
    case StageGuideNpcPhase::NoticingPointer:
        state.moving = false;
        if (!input.pointer.insideWindow || !input.pointer.insideBodyHover) {
            ReturnToRoaming(state, step);
            break;
        }
        state.hoverElapsedSeconds += deltaSeconds;
        if (state.hoverElapsedSeconds >= GetStageGuideNpcTuning().hoverDwellSeconds) {
            state.menuProgress = 0.0;
            SetPhase(state, StageGuideNpcPhase::PresentingMenu, step);
        }
        break;
    case StageGuideNpcPhase::PresentingMenu:
        state.moving = false;
        state.menuProgress = Clamp01(
            state.menuProgress + deltaSeconds / GetStageGuideNpcTuning().menuExpandSeconds);
        if (input.pointer.insideWindow && input.pointer.insideInteractionRegion) {
            state.pointerOutsideElapsedSeconds = 0.0;
        } else {
            state.pointerOutsideElapsedSeconds += deltaSeconds;
            if (state.pointerOutsideElapsedSeconds >=
                GetStageGuideNpcTuning().pointerLeaveGraceSeconds) {
                ReturnToRoaming(state, step);
            }
        }
        break;
    case StageGuideNpcPhase::ShowingAbout:
    case StageGuideNpcPhase::ConfirmingExternalAction:
        state.moving = false;
        break;
    case StageGuideNpcPhase::LeavingForExternalAction:
        state.moving = false;
        state.leavingElapsedSeconds += deltaSeconds;
        if (!state.externalActionIssued &&
            state.leavingElapsedSeconds >= GetStageGuideNpcTuning().externalActionLeaveSeconds) {
            state.pendingExternalAction = state.confirmingEntry == StageGuideMenuEntry::Feedback ?
                StageGuideExternalAction::Feedback : StageGuideExternalAction::Support;
            state.externalActionIssued = true;
        }
        break;
    case StageGuideNpcPhase::Dormant:
    default:
        break;
    }
    return step;
}

bool HandleStageGuideClick(
    StageGuideNpcState& state,
    StageGuideHitTarget target)
{
    if (state.phase == StageGuideNpcPhase::PresentingMenu && state.menuProgress >= 0.82) {
        if (target == StageGuideHitTarget::About && state.availability.about) {
            state.phase = StageGuideNpcPhase::ShowingAbout;
            return true;
        }
        if (target == StageGuideHitTarget::Feedback && state.availability.feedback) {
            state.confirmingEntry = StageGuideMenuEntry::Feedback;
            state.phase = StageGuideNpcPhase::ConfirmingExternalAction;
            return true;
        }
        if (target == StageGuideHitTarget::Support && state.availability.support) {
            state.confirmingEntry = StageGuideMenuEntry::Support;
            state.phase = StageGuideNpcPhase::ConfirmingExternalAction;
            return true;
        }
        return false;
    }
    if (state.phase == StageGuideNpcPhase::ShowingAbout) {
        if (target == StageGuideHitTarget::CloseCard || target == StageGuideHitTarget::None) {
            state.phase = StageGuideNpcPhase::PresentingMenu;
            state.pointerOutsideElapsedSeconds = 0.0;
            return true;
        }
        return false;
    }
    if (state.phase == StageGuideNpcPhase::ConfirmingExternalAction) {
        if (target == StageGuideHitTarget::ConfirmExternalAction) {
            state.leavingElapsedSeconds = 0.0;
            state.externalActionIssued = false;
            state.pendingExternalAction = StageGuideExternalAction::None;
            state.phase = StageGuideNpcPhase::LeavingForExternalAction;
            return true;
        }
        if (target == StageGuideHitTarget::ContinueWatching || target == StageGuideHitTarget::None) {
            state.confirmingEntry = StageGuideMenuEntry::None;
            state.phase = StageGuideNpcPhase::PresentingMenu;
            state.pointerOutsideElapsedSeconds = 0.0;
            return true;
        }
    }
    return false;
}

void ClearStageGuidePointerInteraction(
    StageGuideNpcState& state,
    bool dismissPanels)
{
    state.hoverElapsedSeconds = 0.0;
    state.pointerOutsideElapsedSeconds = 0.0;
    state.pointerPresent = false;
    if (!dismissPanels) return;
    if (state.phase == StageGuideNpcPhase::NoticingPointer ||
        state.phase == StageGuideNpcPhase::PresentingMenu ||
        state.phase == StageGuideNpcPhase::ShowingAbout ||
        state.phase == StageGuideNpcPhase::ConfirmingExternalAction) {
        state.phase = StageGuideNpcPhase::Roaming;
        state.menuProgress = 0.0;
        state.confirmingEntry = StageGuideMenuEntry::None;
    }
}

StageGuideExternalAction ConsumeStageGuideExternalAction(StageGuideNpcState& state)
{
    const StageGuideExternalAction action = state.pendingExternalAction;
    state.pendingExternalAction = StageGuideExternalAction::None;
    return action;
}

bool StageGuideNpcIsVisible(const StageGuideNpcState& state)
{
    return state.phase != StageGuideNpcPhase::Dormant;
}

bool StageGuideNpcShowsMenu(const StageGuideNpcState& state)
{
    return state.phase == StageGuideNpcPhase::PresentingMenu ||
        state.phase == StageGuideNpcPhase::ShowingAbout ||
        state.phase == StageGuideNpcPhase::ConfirmingExternalAction;
}

bool StageGuideNpcShowsAbout(const StageGuideNpcState& state)
{
    return state.phase == StageGuideNpcPhase::ShowingAbout;
}

bool StageGuideNpcShowsExternalConfirmation(const StageGuideNpcState& state)
{
    return state.phase == StageGuideNpcPhase::ConfirmingExternalAction;
}

bool StageGuideNpcIsFrozen(const StageGuideNpcState& state)
{
    return state.phase == StageGuideNpcPhase::NoticingPointer ||
        state.phase == StageGuideNpcPhase::PresentingMenu ||
        state.phase == StageGuideNpcPhase::ShowingAbout ||
        state.phase == StageGuideNpcPhase::ConfirmingExternalAction ||
        state.phase == StageGuideNpcPhase::LeavingForExternalAction;
}

bool StageGuidePointIsSafe(
    const StageGuidePoint& point,
    const StageGuideRect& workArea,
    std::span<const StageGuideReservation> reservations,
    double margin)
{
    if (!std::isfinite(point.x) || !std::isfinite(point.y) ||
        !StageGuideRectIsValid(workArea)) {
        return false;
    }
    if (point.x < workArea.left + margin || point.x > workArea.right - margin ||
        point.y < workArea.top + margin || point.y > workArea.bottom - margin) {
        return false;
    }
    return std::none_of(reservations.begin(), reservations.end(), [&](const auto& reservation) {
        return PointInsideReservation(point, reservation, margin);
    });
}

bool StageGuidePathIsSafe(
    const StageGuidePoint& from,
    const StageGuidePoint& to,
    std::span<const StageGuideReservation> reservations,
    double margin)
{
    if (!std::isfinite(from.x) || !std::isfinite(from.y) ||
        !std::isfinite(to.x) || !std::isfinite(to.y)) {
        return false;
    }
    return std::none_of(reservations.begin(), reservations.end(), [&](const auto& reservation) {
        return reservation.radius > 0.0 &&
            DistancePointToSegment(reservation.center, from, to) <
                reservation.radius + margin;
    });
}

} // namespace besktop
