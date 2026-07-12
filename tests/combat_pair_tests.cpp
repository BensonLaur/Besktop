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

} // namespace

int main()
{
    TestScenarioParsingAndPlans();
    TestPairPhasesAndNoTeleportContract();
    if (failures == 0) std::cout << "besktop_combat_tests: all checks passed\n";
    return failures == 0 ? 0 : 1;
}
