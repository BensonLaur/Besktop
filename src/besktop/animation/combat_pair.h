#pragma once

#include "besktop/animation/action_clip.h"

#include <string_view>

namespace besktop {

enum class CombatScenarioId {
    None,
    LeadParry,
    LeadSlip,
    UppercutLightHit,
    SideKickHeavyHit,
};

enum class CombatResult {
    None,
    HitLight,
    HitHeavy,
    Blocked,
    Evaded,
    Whiffed,
};

enum class CombatPairPhase {
    Inactive,
    Approaching,
    Aligning,
    Settling,
    Exchanging,
    Recovering,
    Returning,
    Paused,
    Complete,
};

struct CombatPairPlan {
    CombatScenarioId scenario = CombatScenarioId::None;
    ActionId attackerAction = ActionId::None;
    ActionId defenderAction = ActionId::None;
    double attackerStartTime = 0.0;
    double defenderStartTime = 0.0;
    double expectedContactTime = 0.0;
    CombatResult expectedResult = CombatResult::None;
    double desiredAxisDistanceScale = 1.0;
    double targetHeightScale = 0.0;
    double settlingDuration = 0.30;
    double recoveryDuration = 0.30;
};

struct CombatPairReadiness {
    bool bothAwake = false;
    bool atStations = false;
    bool aligned = false;
    bool actionsComplete = false;
    bool returnedToStations = false;
};

struct CombatPairStep {
    bool startAttackerAction = false;
    bool startDefenderAction = false;
    bool resolveContact = false;
    bool startWhiffRecovery = false;
    bool startLightHitReact = false;
    bool startHeavyStagger = false;
};

struct CombatPairState {
    CombatPairPhase phase = CombatPairPhase::Inactive;
    double phaseTime = 0.0;
    double interactionTime = 0.0;
    bool attackerStarted = false;
    bool defenderStarted = false;
    bool contactConsumed = false;
    bool resultActionStarted = false;
    CombatResult result = CombatResult::None;
};

enum class CombatAttackType {
    None,
    Punch,
    Kick,
};

struct CombatPoint {
    double x = 0.0;
    double y = 0.0;
};

struct CombatContactProbe {
    CombatPoint attackPoint{};
    double attackRadius = 0.0;
    CombatAttackType attackType = CombatAttackType::None;
    ActionHitStrength hitStrength = ActionHitStrength::None;
    CombatPoint attackDirection{1.0, 0.0};
    CombatPoint targetAxisTop{};
    CombatPoint targetAxisBottom{};
    double targetRadius = 0.0;
    double actorAxisDistance = 0.0;
    double maximumAxisDistance = 0.0;
    ActionDefenseWindowType defenseWindow = ActionDefenseWindowType::None;
};

CombatScenarioId ParseCombatScenarioId(std::wstring_view name);
std::wstring_view CombatScenarioIdName(CombatScenarioId id);
std::wstring_view CombatResultName(CombatResult result);
std::wstring_view CombatPairPhaseName(CombatPairPhase phase);
const CombatPairPlan& GetCombatPairPlan(CombatScenarioId id);

void ResetCombatPair(CombatPairState& state);
CombatPairStep UpdateCombatPair(
    CombatPairState& state,
    const CombatPairPlan& plan,
    const CombatPairReadiness& readiness,
    double deltaSeconds);
void ApplyCombatResult(CombatPairState& state, CombatResult result);

double DistancePointToSegment(CombatPoint point, CombatPoint start, CombatPoint end);
CombatResult ResolveCombatContact(const CombatContactProbe& probe);

} // namespace besktop
