#include "besktop/animation/combat_pair.h"

#include <cmath>
#include <iostream>

namespace {

int failures = 0;

void Check(bool condition, const char* name)
{
    if (!condition) {
        ++failures;
        std::cerr << "FAILED: " << name << '\n';
    }
}

void TestScenarioParsingAndPlans()
{
    using namespace besktop;
    Check(ParseCombatScenarioId(L"lead_parry") == CombatScenarioId::LeadParry, "parse lead_parry");
    Check(ParseCombatScenarioId(L"lead_slip") == CombatScenarioId::LeadSlip, "parse lead_slip");
    Check(ParseCombatScenarioId(L"uppercut_light_hit") == CombatScenarioId::UppercutLightHit,
        "parse uppercut_light_hit");
    Check(ParseCombatScenarioId(L"side_kick_heavy_hit") == CombatScenarioId::SideKickHeavyHit,
        "parse side_kick_heavy_hit");
    Check(ParseCombatScenarioId(L"invalid") == CombatScenarioId::None, "invalid scenario fallback");

    const CombatPairPlan& parry = GetCombatPairPlan(CombatScenarioId::LeadParry);
    const CombatPairPlan& slip = GetCombatPairPlan(CombatScenarioId::LeadSlip);
    Check(parry.attackerAction == ActionId::LeadStraight && parry.defenderAction == ActionId::Parry,
        "lead parry actions");
    Check(slip.attackerAction == ActionId::LeadStraight && slip.defenderAction == ActionId::SlipLeft,
        "lead slip actions");
    const double parryLocalContact = parry.expectedContactTime - parry.defenderStartTime;
    const double slipLocalContact = slip.expectedContactTime - slip.defenderStartTime;
    Check(ActionDefenseWindowAt(GetActionClip(parry.defenderAction), parryLocalContact) ==
        ActionDefenseWindowType::Parry, "parry contact is in defense window");
    Check(ActionDefenseWindowAt(GetActionClip(slip.defenderAction), slipLocalContact) ==
        ActionDefenseWindowType::Evade, "slip contact is in defense window");
}

void TestPairPhasesAndNoTeleportContract()
{
    using namespace besktop;
    CombatPairState state;
    const CombatPairPlan& plan = GetCombatPairPlan(CombatScenarioId::LeadParry);
    CombatPairReadiness readiness;
    UpdateCombatPair(state, plan, readiness, 1.0);
    Check(state.phase == CombatPairPhase::Inactive, "sleeping pair remains inactive");

    readiness.bothAwake = true;
    UpdateCombatPair(state, plan, readiness, 0.1);
    Check(state.phase == CombatPairPhase::Approaching, "awake pair approaches");
    Check(!state.attackerStarted, "approach does not start attack");

    readiness.atStations = true;
    UpdateCombatPair(state, plan, readiness, 0.1);
    Check(state.phase == CombatPairPhase::Aligning, "station arrival enters align");
    Check(!state.attackerStarted, "unaligned pair does not attack");

    readiness.aligned = true;
    UpdateCombatPair(state, plan, readiness, 0.1);
    Check(state.phase == CombatPairPhase::Settling, "alignment enters settle");
    const CombatPairStep step = UpdateCombatPair(state, plan, readiness, 0.25);
    Check(state.phase == CombatPairPhase::Exchanging, "settle enters exchange");
    Check(step.startAttackerAction, "attacker starts on exchange boundary");
}

besktop::CombatContactProbe BaseHitProbe()
{
    besktop::CombatContactProbe probe;
    probe.attackPoint = {8.0, 5.0};
    probe.attackRadius = 2.0;
    probe.attackType = besktop::CombatAttackType::Punch;
    probe.hitStrength = besktop::ActionHitStrength::Light;
    probe.attackDirection = {1.0, 0.0};
    probe.targetAxisTop = {10.0, 0.0};
    probe.targetAxisBottom = {10.0, 10.0};
    probe.targetRadius = 2.0;
    probe.actorAxisDistance = 10.0;
    probe.maximumAxisDistance = 20.0;
    return probe;
}

void TestContactGeometryAndOrdering()
{
    using namespace besktop;
    CombatContactProbe probe = BaseHitProbe();
    Check(ResolveCombatContact(probe) == CombatResult::HitLight, "punch geometry yields light hit");

    probe.attackType = CombatAttackType::Kick;
    probe.hitStrength = ActionHitStrength::Heavy;
    Check(ResolveCombatContact(probe) == CombatResult::HitHeavy, "kick geometry yields heavy hit");

    probe = BaseHitProbe();
    probe.actorAxisDistance = 30.0;
    Check(ResolveCombatContact(probe) == CombatResult::Whiffed, "out of range whiffs");
    probe = BaseHitProbe();
    probe.attackDirection = {-1.0, 0.0};
    Check(ResolveCombatContact(probe) == CombatResult::Whiffed, "wrong direction whiffs");

    probe = BaseHitProbe();
    probe.defenseWindow = ActionDefenseWindowType::Parry;
    Check(ResolveCombatContact(probe) == CombatResult::Blocked, "parry blocks punch");
    probe.attackType = CombatAttackType::Kick;
    probe.hitStrength = ActionHitStrength::Heavy;
    Check(ResolveCombatContact(probe) == CombatResult::HitHeavy, "parry does not block kick");

    probe = BaseHitProbe();
    probe.defenseWindow = ActionDefenseWindowType::Evade;
    probe.attackPoint = {8.0, 30.0};
    Check(ResolveCombatContact(probe) == CombatResult::Evaded, "evade window plus clear line evades");
    probe.attackPoint = {8.0, 5.0};
    Check(ResolveCombatContact(probe) == CombatResult::HitLight,
        "evade window does not fabricate evasion on attack line");
}

void TestLargeDeltaAndSingleContact()
{
    using namespace besktop;
    CombatPairState state;
    const CombatPairPlan& plan = GetCombatPairPlan(CombatScenarioId::LeadSlip);
    CombatPairReadiness ready{true, true, true, false, false};
    UpdateCombatPair(state, plan, ready, 0.0);
    const CombatPairStep exchangeStart = UpdateCombatPair(state, plan, ready, plan.settlingDuration);
    const CombatPairStep crossed = UpdateCombatPair(state, plan, ready, 2.4);
    Check(exchangeStart.startAttackerAction && !crossed.startAttackerAction,
        "exchange boundary starts attacker once");
    Check(crossed.startDefenderAction, "large delta starts defender once");
    Check(crossed.resolveContact, "large delta does not miss contact");
    const CombatPairStep repeated = UpdateCombatPair(state, plan, ready, 2.4);
    Check(!repeated.startAttackerAction && !repeated.startDefenderAction && !repeated.resolveContact,
        "large delta does not repeat starts or contact");
    ApplyCombatResult(state, CombatResult::Evaded);
    ApplyCombatResult(state, CombatResult::HitLight);
    Check(state.result == CombatResult::Evaded, "combat result is consumed once");
}

void TestScenarioResultsAndRootMotionContracts()
{
    using namespace besktop;
    const CombatPairPlan& parry = GetCombatPairPlan(CombatScenarioId::LeadParry);
    const CombatPairPlan& slip = GetCombatPairPlan(CombatScenarioId::LeadSlip);
    const CombatPairPlan& light = GetCombatPairPlan(CombatScenarioId::UppercutLightHit);
    const CombatPairPlan& heavy = GetCombatPairPlan(CombatScenarioId::SideKickHeavyHit);
    Check(parry.expectedResult == CombatResult::Blocked, "lead parry expects blocked");
    Check(slip.expectedResult == CombatResult::Evaded, "lead slip expects evaded");
    Check(light.expectedResult == CombatResult::HitLight, "uppercut expects light hit");
    Check(heavy.expectedResult == CombatResult::HitHeavy, "side kick expects heavy hit");
    Check(std::abs(GetActionClip(ActionId::HeavyStagger).finalRootDisplacementForward + 0.16) < 1e-9,
        "heavy stagger commits B position");
    Check(std::abs(GetActionClip(ActionId::WhiffRecovery).finalRootDisplacementForward) < 1e-9,
        "whiff recovery returns to action origin");
    std::cout << "combat timeline metrics: parry defender start=" << parry.defenderStartTime
              << ", slip defender start=" << slip.defenderStartTime
              << ", lead contact=" << parry.expectedContactTime
              << ", uppercut contact=" << light.expectedContactTime
              << ", side kick contact=" << heavy.expectedContactTime << '\n';
}

} // namespace

int main()
{
    TestScenarioParsingAndPlans();
    TestPairPhasesAndNoTeleportContract();
    TestContactGeometryAndOrdering();
    TestLargeDeltaAndSingleContact();
    TestScenarioResultsAndRootMotionContracts();
    if (failures == 0) std::cout << "besktop_combat_tests: all checks passed\n";
    return failures == 0 ? 0 : 1;
}
