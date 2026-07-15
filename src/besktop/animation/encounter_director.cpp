#include "besktop/animation/encounter_director.h"

#include <algorithm>
#include <cmath>

namespace {

constexpr double kPi = 3.14159265358979323846;

double NextUnit(std::uint32_t& state)
{
    state = state * 1664525u + 1013904223u;
    return static_cast<double>(state & 0x00FFFFFFu) / 16777215.0;
}

double SmoothStep(double value)
{
    const double t = std::clamp(value, 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

besktop::EncounterPoint SafePoint(
    const besktop::EncounterReservation& reservation,
    const besktop::EncounterBounds& bounds,
    double actorMargin,
    double normalizedX,
    double normalizedY)
{
    const double margin = std::max(0.0, actorMargin);
    const double usableRadius = std::max(0.0, reservation.radius - margin);
    const double length = std::hypot(normalizedX, normalizedY);
    if (length > 0.82 && length > 1e-9) {
        normalizedX *= 0.82 / length;
        normalizedY *= 0.82 / length;
    }
    const double minimumX = bounds.left + margin;
    const double maximumX = bounds.right - margin;
    const double minimumY = bounds.top + margin;
    const double maximumY = bounds.bottom - margin;
    return {
        std::clamp(reservation.centerX + normalizedX * usableRadius, minimumX, maximumX),
        std::clamp(reservation.centerY + normalizedY * usableRadius, minimumY, maximumY),
    };
}

besktop::EncounterIntent ChooseIntent(std::uint32_t& state)
{
    const besktop::EncounterTuning& tuning = besktop::GetEncounterTuning();
    const double sample = NextUnit(state);
    if (sample < tuning.combatWeight) return besktop::EncounterIntent::Combat;
    if (sample < tuning.combatWeight + tuning.yieldWeight) return besktop::EncounterIntent::Yield;
    return besktop::EncounterIntent::Bluff;
}

void SetPhase(besktop::EncounterState& state, besktop::EncounterPhase phase)
{
    state.phase = phase;
    state.phaseTime = 0.0;
}

double ConsumeTimedPhase(double& phaseTime, double duration, double& remaining)
{
    const double needed = std::max(0.0, duration - phaseTime);
    const double consumed = std::min(needed, remaining);
    phaseTime += consumed;
    remaining -= consumed;
    return duration - phaseTime;
}

} // namespace

namespace besktop {

const EncounterTuning& GetEncounterTuning()
{
    static const EncounterTuning tuning{};
    return tuning;
}

std::wstring_view EncounterPhaseName(EncounterPhase phase)
{
    switch (phase) {
    case EncounterPhase::Notice: return L"Notice";
    case EncounterPhase::Approaching: return L"Approach";
    case EncounterPhase::Facing: return L"FaceEachOther";
    case EncounterPhase::Assessing: return L"Assess";
    case EncounterPhase::Intent: return L"Intent";
    case EncounterPhase::Combat: return L"Combat";
    case EncounterPhase::Aftermath: return L"Aftermath";
    case EncounterPhase::Separating: return L"Separate";
    case EncounterPhase::Complete: return L"Complete";
    case EncounterPhase::Cancelled: return L"Cancelled";
    default: return L"Inactive";
    }
}

std::wstring_view EncounterIntentName(EncounterIntent intent)
{
    switch (intent) {
    case EncounterIntent::Combat: return L"Combat";
    case EncounterIntent::Yield: return L"Yield";
    case EncounterIntent::Bluff: return L"Bluff";
    default: return L"Undecided";
    }
}

void BeginEncounter(
    EncounterState& state,
    std::uint32_t seed,
    const EncounterReservation& reservation,
    const EncounterBounds& bounds,
    double actorMargin,
    EncounterIntent resolvedIntent,
    std::optional<bool> attackerActsFirst)
{
    state = {};
    state.phase = EncounterPhase::Notice;
    state.randomState = seed == 0 ? 0xE11C0A7u : seed;
    state.reservation = reservation;
    state.bounds = bounds;
    state.actorMargin = std::max(0.0, actorMargin);
    state.intent = resolvedIntent == EncounterIntent::Undecided ?
        ChooseIntent(state.randomState) : resolvedIntent;
    state.attackerActsFirst = attackerActsFirst.value_or(NextUnit(state.randomState) < 0.5);
    const EncounterTuning& tuning = GetEncounterTuning();
    state.assessDuration = tuning.assessMinimumSeconds + NextUnit(state.randomState) *
        (tuning.assessMaximumSeconds - tuning.assessMinimumSeconds);
    state.intentDuration = state.intent == EncounterIntent::Yield ?
        tuning.yieldIntentSeconds : (state.intent == EncounterIntent::Bluff ? tuning.bluffIntentSeconds : 0.0);

    if (state.intent == EncounterIntent::Yield) {
        const double yieldSide = state.attackerActsFirst ? -1.0 : 1.0;
        state.attackerIntentTarget = SafePoint(reservation, bounds, state.actorMargin,
            state.attackerActsFirst ? -0.60 : -0.28, yieldSide * 0.22);
        state.defenderIntentTarget = SafePoint(reservation, bounds, state.actorMargin,
            state.attackerActsFirst ? 0.28 : 0.60, -yieldSide * 0.10);
    } else if (state.intent == EncounterIntent::Bluff) {
        state.attackerIntentTarget = SafePoint(reservation, bounds, state.actorMargin,
            state.attackerActsFirst ? -0.04 : -0.52, state.attackerActsFirst ? 0.06 : -0.06);
        state.defenderIntentTarget = SafePoint(reservation, bounds, state.actorMargin,
            state.attackerActsFirst ? 0.52 : 0.04, state.attackerActsFirst ? -0.06 : 0.06);
    }
}

EncounterAftermathPlan BuildEncounterAftermathPlan(
    EncounterIntent intent,
    CombatResult result,
    bool attackerActsFirst,
    const EncounterReservation& reservation,
    const EncounterBounds& bounds,
    double actorMargin)
{
    EncounterAftermathPlan plan;
    plan.result = result;
    double attackerX = -0.50;
    double attackerY = -0.10;
    double defenderX = 0.50;
    double defenderY = 0.10;

    if (intent == EncounterIntent::Yield) {
        plan.holdSeconds = 0.42;
        plan.attackerDepartureDelaySeconds = attackerActsFirst ? 0.0 : 0.24;
        plan.defenderDepartureDelaySeconds = attackerActsFirst ? 0.24 : 0.0;
        attackerX = attackerActsFirst ? -0.66 : -0.44;
        defenderX = attackerActsFirst ? 0.44 : 0.66;
        attackerY = attackerActsFirst ? 0.24 : -0.12;
        defenderY = attackerActsFirst ? -0.12 : 0.24;
    } else if (intent == EncounterIntent::Bluff) {
        plan.holdSeconds = 0.48;
        plan.attackerDepartureDelaySeconds = attackerActsFirst ? 0.08 : 0.18;
        plan.defenderDepartureDelaySeconds = attackerActsFirst ? 0.18 : 0.08;
        attackerX = -0.56;
        defenderX = 0.56;
        attackerY = attackerActsFirst ? -0.18 : 0.18;
        defenderY = -attackerY;
    } else {
        switch (result) {
        case CombatResult::HitLight:
            plan.holdSeconds = 0.54;
            plan.attackerDepartureDelaySeconds = 0.26;
            plan.defenderDepartureDelaySeconds = 0.0;
            attackerX = -0.44;
            defenderX = 0.68;
            defenderY = 0.16;
            break;
        case CombatResult::HitHeavy:
            plan.holdSeconds = 0.78;
            plan.attackerDepartureDelaySeconds = 0.40;
            plan.defenderDepartureDelaySeconds = 0.10;
            attackerX = -0.40;
            defenderX = 0.78;
            defenderY = 0.12;
            plan.heavyRootMotionAlreadyApplied = true;
            break;
        case CombatResult::Blocked:
            plan.holdSeconds = 0.40;
            plan.attackerDepartureDelaySeconds = 0.08;
            plan.defenderDepartureDelaySeconds = 0.08;
            attackerX = -0.52;
            defenderX = 0.52;
            break;
        case CombatResult::Evaded:
            plan.holdSeconds = 0.52;
            plan.attackerDepartureDelaySeconds = 0.28;
            plan.defenderDepartureDelaySeconds = 0.0;
            attackerX = -0.58;
            attackerY = 0.08;
            defenderX = 0.38;
            defenderY = -0.62;
            break;
        case CombatResult::Whiffed:
        default:
            plan.result = CombatResult::Whiffed;
            plan.holdSeconds = 0.60;
            plan.attackerDepartureDelaySeconds = 0.16;
            plan.defenderDepartureDelaySeconds = 0.28;
            attackerX = -0.68;
            defenderX = 0.44;
            defenderY = -0.18;
            break;
        }
    }

    plan.attackerExit = SafePoint(reservation, bounds, actorMargin, attackerX, attackerY);
    plan.defenderExit = SafePoint(reservation, bounds, actorMargin, defenderX, defenderY);
    return plan;
}

EncounterStep UpdateEncounter(
    EncounterState& state,
    const EncounterReadiness& readiness,
    double deltaSeconds)
{
    EncounterStep step;
    if (state.phase == EncounterPhase::Inactive || !std::isfinite(deltaSeconds) || deltaSeconds < 0.0) {
        return step;
    }
    if (!readiness.actorsValid || !readiness.reservationSafe) {
        CancelEncounter(state);
    }
    if (state.phase == EncounterPhase::Cancelled) {
        if (!state.cancellationEmitted) {
            state.cancellationEmitted = true;
            step.cancelled = true;
        }
        return step;
    }

    double remaining = std::min(deltaSeconds, 8.0);
    for (int transitions = 0; transitions < 12; ++transitions) {
        const EncounterPhase previous = state.phase;
        switch (state.phase) {
        case EncounterPhase::Notice:
            if (ConsumeTimedPhase(state.phaseTime, GetEncounterTuning().noticeSeconds, remaining) > 1e-9) return step;
            SetPhase(state, EncounterPhase::Approaching);
            break;
        case EncounterPhase::Approaching:
            if (!readiness.atStations) { state.phaseTime += remaining; return step; }
            SetPhase(state, EncounterPhase::Facing);
            break;
        case EncounterPhase::Facing:
            if (!readiness.aligned) { state.phaseTime += remaining; return step; }
            SetPhase(state, EncounterPhase::Assessing);
            break;
        case EncounterPhase::Assessing:
            if (ConsumeTimedPhase(state.phaseTime, state.assessDuration, remaining) > 1e-9) return step;
            SetPhase(state, EncounterPhase::Intent);
            break;
        case EncounterPhase::Intent:
            if (state.intent == EncounterIntent::Combat) {
                SetPhase(state, EncounterPhase::Combat);
                if (!state.combatStartRequested) {
                    state.combatStartRequested = true;
                    step.requestCombatStart = true;
                }
                step.phaseChanged = true;
                return step;
            }
            if (ConsumeTimedPhase(state.phaseTime, state.intentDuration, remaining) > 1e-9) return step;
            state.aftermath = BuildEncounterAftermathPlan(
                state.intent, CombatResult::None, state.attackerActsFirst,
                state.reservation, state.bounds, state.actorMargin);
            SetPhase(state, EncounterPhase::Aftermath);
            break;
        case EncounterPhase::Combat:
            if (!readiness.combatComplete || readiness.combatResult == CombatResult::None) return step;
            state.aftermath = BuildEncounterAftermathPlan(
                state.intent, readiness.combatResult, state.attackerActsFirst,
                state.reservation, state.bounds, state.actorMargin);
            SetPhase(state, EncounterPhase::Aftermath);
            break;
        case EncounterPhase::Aftermath:
            if (ConsumeTimedPhase(state.phaseTime, state.aftermath.holdSeconds, remaining) > 1e-9) return step;
            SetPhase(state, EncounterPhase::Separating);
            break;
        case EncounterPhase::Separating:
            state.phaseTime += remaining;
            if (!readiness.separated) return step;
            SetPhase(state, EncounterPhase::Complete);
            break;
        case EncounterPhase::Complete:
            if (!state.completionEmitted) {
                state.completionEmitted = true;
                step.completed = true;
            }
            return step;
        case EncounterPhase::Cancelled:
        case EncounterPhase::Inactive:
            return step;
        }
        step.phaseChanged = step.phaseChanged || state.phase != previous;
        if (remaining <= 1e-9) return step;
    }
    return step;
}

void CancelEncounter(EncounterState& state)
{
    if (state.phase == EncounterPhase::Inactive || state.phase == EncounterPhase::Complete) return;
    SetPhase(state, EncounterPhase::Cancelled);
}

EncounterPoseSample SampleEncounterPose(
    const EncounterState& state,
    EncounterActorRole role)
{
    EncounterPoseSample sample;
    const double roleSign = role == EncounterActorRole::Attacker ? 1.0 : -1.0;
    if (state.phase == EncounterPhase::Assessing && state.assessDuration > 1e-9) {
        const double weight = std::sin(std::clamp(state.phaseTime / state.assessDuration, 0.0, 1.0) * kPi);
        sample.bodyRotateX = roleSign * 0.025 * weight;
        sample.rootOffsetY = 0.012 * weight;
    } else if (state.phase == EncounterPhase::Intent && state.intent == EncounterIntent::Yield) {
        const double weight = SmoothStep(state.intentDuration > 1e-9 ? state.phaseTime / state.intentDuration : 1.0);
        const bool yielding = (role == EncounterActorRole::Attacker) == state.attackerActsFirst;
        sample.bodyRotateZ = (yielding ? -roleSign * 0.10 : roleSign * 0.025) * weight;
        sample.rootOffsetY = yielding ? 0.035 * weight : 0.0;
    } else if (state.phase == EncounterPhase::Intent && state.intent == EncounterIntent::Bluff) {
        const double pulse = std::sin(std::clamp(state.phaseTime / state.intentDuration, 0.0, 1.0) * kPi);
        const bool bluffing = (role == EncounterActorRole::Attacker) == state.attackerActsFirst;
        sample.bodyRotateX = (bluffing ? 0.13 : -0.045) * pulse;
        sample.rootOffsetForward = (bluffing ? 0.055 : -0.025) * pulse;
    }
    return sample;
}

bool EncounterActorMayDepart(const EncounterState& state, EncounterActorRole role)
{
    if (state.phase != EncounterPhase::Separating) return false;
    const double delay = role == EncounterActorRole::Attacker ?
        state.aftermath.attackerDepartureDelaySeconds : state.aftermath.defenderDepartureDelaySeconds;
    return state.phaseTime + 1e-9 >= delay;
}

bool IsEncounterPointSafe(
    const EncounterPoint& point,
    const EncounterReservation& reservation,
    const EncounterBounds& bounds,
    double actorMargin)
{
    const double margin = std::max(0.0, actorMargin);
    const bool insideBounds = point.x >= bounds.left + margin && point.x <= bounds.right - margin &&
        point.y >= bounds.top + margin && point.y <= bounds.bottom - margin;
    const double usableRadius = std::max(0.0, reservation.radius - margin);
    return insideBounds && std::hypot(point.x - reservation.centerX, point.y - reservation.centerY) <= usableRadius + 1e-6;
}

} // namespace besktop
