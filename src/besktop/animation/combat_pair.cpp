#include "besktop/animation/combat_pair.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace {

besktop::CombatPairPlan MakePlan(
    besktop::CombatScenarioId scenario,
    besktop::ActionId attacker,
    besktop::ActionId defender,
    double defenderWindowTarget,
    besktop::CombatResult result,
    double distanceScale,
    double targetHeightScale)
{
    const besktop::ActionClip& attackClip = besktop::GetActionClip(attacker);
    return besktop::CombatPairPlan{
        scenario,
        attacker,
        defender,
        0.0,
        defender == besktop::ActionId::None ? 0.0 :
            std::max(0.0, attackClip.activeEnd - defenderWindowTarget),
        attackClip.activeEnd,
        result,
        result == besktop::CombatResult::HitHeavy ?
            besktop::ActionHitStrength::Heavy : besktop::ActionHitStrength::Light,
        distanceScale,
        targetHeightScale,
        0.30,
        0.30,
    };
}

const besktop::CombatPairPlan& NonePlan()
{
    static const besktop::CombatPairPlan plan{};
    return plan;
}

const besktop::CombatPairPlan& LeadParryPlan()
{
    static const besktop::CombatPairPlan plan = MakePlan(
        besktop::CombatScenarioId::LeadParry,
        besktop::ActionId::LeadStraight,
        besktop::ActionId::Parry,
        0.20,
        besktop::CombatResult::Blocked,
        1.18,
        -0.16);
    return plan;
}

const besktop::CombatPairPlan& LeadSlipPlan()
{
    static const besktop::CombatPairPlan plan = MakePlan(
        besktop::CombatScenarioId::LeadSlip,
        besktop::ActionId::LeadStraight,
        besktop::ActionId::SlipLeft,
        0.24,
        besktop::CombatResult::Evaded,
        1.18,
        -0.16);
    return plan;
}

const besktop::CombatPairPlan& UppercutLightHitPlan()
{
    static const besktop::CombatPairPlan plan = MakePlan(
        besktop::CombatScenarioId::UppercutLightHit,
        besktop::ActionId::Uppercut,
        besktop::ActionId::None,
        0.0,
        besktop::CombatResult::HitLight,
        0.62,
        -0.20);
    return plan;
}

const besktop::CombatPairPlan& SideKickHeavyHitPlan()
{
    static const besktop::CombatPairPlan plan = MakePlan(
        besktop::CombatScenarioId::SideKickHeavyHit,
        besktop::ActionId::SideKick,
        besktop::ActionId::None,
        0.0,
        besktop::CombatResult::HitHeavy,
        1.35,
        0.04);
    return plan;
}

bool Crossed(double previous, double next, double threshold)
{
    return threshold >= previous && threshold <= next;
}

} // namespace

namespace besktop {

CombatScenarioId ParseCombatScenarioId(std::wstring_view name)
{
    if (name == L"lead_parry") return CombatScenarioId::LeadParry;
    if (name == L"lead_slip") return CombatScenarioId::LeadSlip;
    if (name == L"uppercut_light_hit") return CombatScenarioId::UppercutLightHit;
    if (name == L"side_kick_heavy_hit") return CombatScenarioId::SideKickHeavyHit;
    return CombatScenarioId::None;
}

std::wstring_view CombatScenarioIdName(CombatScenarioId id)
{
    switch (id) {
    case CombatScenarioId::LeadParry: return L"lead_parry";
    case CombatScenarioId::LeadSlip: return L"lead_slip";
    case CombatScenarioId::UppercutLightHit: return L"uppercut_light_hit";
    case CombatScenarioId::SideKickHeavyHit: return L"side_kick_heavy_hit";
    default: return L"none";
    }
}

std::wstring_view CombatResultName(CombatResult result)
{
    switch (result) {
    case CombatResult::HitLight: return L"HitLight";
    case CombatResult::HitHeavy: return L"HitHeavy";
    case CombatResult::Blocked: return L"Blocked";
    case CombatResult::Evaded: return L"Evaded";
    case CombatResult::Whiffed: return L"Whiffed";
    default: return L"None";
    }
}

std::wstring_view CombatPairPhaseName(CombatPairPhase phase)
{
    switch (phase) {
    case CombatPairPhase::Approaching: return L"Approaching";
    case CombatPairPhase::Aligning: return L"Aligning";
    case CombatPairPhase::Settling: return L"Settling";
    case CombatPairPhase::Exchanging: return L"Exchanging";
    case CombatPairPhase::Recovering: return L"Recovering";
    case CombatPairPhase::Returning: return L"Returning";
    case CombatPairPhase::Paused: return L"Paused";
    case CombatPairPhase::Complete: return L"Complete";
    default: return L"Inactive";
    }
}

const CombatPairPlan& GetCombatPairPlan(CombatScenarioId id)
{
    switch (id) {
    case CombatScenarioId::LeadParry: return LeadParryPlan();
    case CombatScenarioId::LeadSlip: return LeadSlipPlan();
    case CombatScenarioId::UppercutLightHit: return UppercutLightHitPlan();
    case CombatScenarioId::SideKickHeavyHit: return SideKickHeavyHitPlan();
    default: return NonePlan();
    }
}

void ResetCombatPair(CombatPairState& state)
{
    state = {};
}

CombatPairStep UpdateCombatPair(
    CombatPairState& state,
    const CombatPairPlan& plan,
    const CombatPairReadiness& readiness,
    double deltaSeconds)
{
    CombatPairStep step;
    if (plan.scenario == CombatScenarioId::None || !std::isfinite(deltaSeconds) || deltaSeconds < 0.0) {
        return step;
    }
    double remaining = std::min(deltaSeconds, 4.0);
    for (int transitionCount = 0; transitionCount < 12; ++transitionCount) {
        switch (state.phase) {
        case CombatPairPhase::Inactive:
            if (!readiness.bothAwake) return step;
            state.phase = CombatPairPhase::Approaching;
            state.phaseTime = 0.0;
            continue;
        case CombatPairPhase::Approaching:
            if (!readiness.atStations) { state.phaseTime += remaining; return step; }
            state.phase = CombatPairPhase::Aligning;
            state.phaseTime = 0.0;
            continue;
        case CombatPairPhase::Aligning:
            if (!readiness.aligned) { state.phaseTime += remaining; return step; }
            state.phase = CombatPairPhase::Settling;
            state.phaseTime = 0.0;
            continue;
        case CombatPairPhase::Settling: {
            const double needed = std::max(0.0, plan.settlingDuration - state.phaseTime);
            const double consumed = std::min(remaining, needed);
            state.phaseTime += consumed;
            remaining -= consumed;
            if (state.phaseTime + 1e-9 < plan.settlingDuration) return step;
            state.phase = CombatPairPhase::Exchanging;
            state.phaseTime = 0.0;
            state.interactionTime = 0.0;
            continue;
        }
        case CombatPairPhase::Exchanging: {
            const double previous = state.interactionTime;
            const double next = previous + remaining;
            if (!state.attackerStarted && Crossed(previous, next, plan.attackerStartTime)) {
                state.attackerStarted = true;
                step.startAttackerAction = true;
            }
            if (plan.defenderAction != ActionId::None && !state.defenderStarted &&
                Crossed(previous, next, plan.defenderStartTime)) {
                state.defenderStarted = true;
                step.startDefenderAction = true;
            }
            if (!state.contactConsumed && Crossed(previous, next, plan.expectedContactTime)) {
                state.contactConsumed = true;
                step.resolveContact = true;
            }
            state.interactionTime = next;
            state.phaseTime += remaining;
            return step;
        }
        case CombatPairPhase::Recovering:
            if (!readiness.actionsComplete) { state.phaseTime += remaining; return step; }
            state.phase = CombatPairPhase::Returning;
            state.phaseTime = 0.0;
            continue;
        case CombatPairPhase::Returning:
            if (!readiness.returnedToStations) { state.phaseTime += remaining; return step; }
            state.phase = CombatPairPhase::Paused;
            state.phaseTime = 0.0;
            continue;
        case CombatPairPhase::Paused: {
            constexpr double pauseDuration = 0.70;
            const double consumed = std::min(remaining, std::max(0.0, pauseDuration - state.phaseTime));
            state.phaseTime += consumed;
            remaining -= consumed;
            if (state.phaseTime + 1e-9 < pauseDuration) return step;
            state = {};
            state.phase = CombatPairPhase::Approaching;
            continue;
        }
        case CombatPairPhase::Complete:
            return step;
        }
        if (remaining <= 1e-9) return step;
    }
    return step;
}

void ApplyCombatResult(CombatPairState& state, CombatResult result)
{
    if (state.result != CombatResult::None) return;
    state.result = result;
    state.resultActionStarted = result == CombatResult::Blocked;
    state.phase = CombatPairPhase::Recovering;
    state.phaseTime = 0.0;
}

double DistancePointToSegment(CombatPoint point, CombatPoint start, CombatPoint end)
{
    const double dx = end.x - start.x;
    const double dy = end.y - start.y;
    const double lengthSquared = dx * dx + dy * dy;
    if (lengthSquared <= 1e-12) return std::hypot(point.x - start.x, point.y - start.y);
    const double t = std::clamp(
        ((point.x - start.x) * dx + (point.y - start.y) * dy) / lengthSquared,
        0.0,
        1.0);
    return std::hypot(point.x - (start.x + t * dx), point.y - (start.y + t * dy));
}

CombatResult ResolveCombatContact(const CombatContactProbe& probe)
{
    const double directionLength = std::hypot(probe.attackDirection.x, probe.attackDirection.y);
    const CombatPoint toTarget{
        (probe.targetAxisTop.x + probe.targetAxisBottom.x) * 0.5 - probe.attackPoint.x,
        (probe.targetAxisTop.y + probe.targetAxisBottom.y) * 0.5 - probe.attackPoint.y,
    };
    const double facingDot = directionLength > 1e-9 ?
        (probe.attackDirection.x * toTarget.x + probe.attackDirection.y * toTarget.y) / directionLength : -1.0;
    const bool inBroadRange = probe.maximumAxisDistance > 0.0 &&
        probe.actorAxisDistance <= probe.maximumAxisDistance;
    if (!inBroadRange || facingDot <= 0.0) return CombatResult::Whiffed;

    if (probe.defenseWindow == ActionDefenseWindowType::Parry &&
        probe.attackType == CombatAttackType::Punch) {
        return CombatResult::Blocked;
    }

    const double targetDistance = DistancePointToSegment(
        probe.attackPoint, probe.targetAxisTop, probe.targetAxisBottom);
    const bool geometryHit = targetDistance <= probe.attackRadius + probe.targetRadius;
    if (probe.defenseWindow == ActionDefenseWindowType::Evade && !geometryHit) {
        return CombatResult::Evaded;
    }
    if (!geometryHit) return CombatResult::Whiffed;
    return probe.hitStrength == ActionHitStrength::Heavy ?
        CombatResult::HitHeavy : CombatResult::HitLight;
}

} // namespace besktop
