#include "besktop/animation/combat_director.h"

#include <array>
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

besktop::CombatDirectorCandidate Candidate(std::size_t index, double x, double y)
{
    return {index, x, y, 48.0, true, true, false, false};
}

void TestDisabledAndEligibility()
{
    using namespace besktop;
    CombatDirectorState state;
    const CombatDirectorBounds bounds{0.0, 0.0, 1000.0, 800.0};
    std::array candidates{Candidate(0, 350.0, 400.0), Candidate(1, 550.0, 400.0)};
    InitializeCombatDirector(state, false, candidates.size());
    Check(!UpdateCombatDirector(state, candidates, bounds, 5.0).started, "disabled director stays idle");

    InitializeCombatDirector(state, true, candidates.size());
    candidates[0].awake = false;
    Check(!UpdateCombatDirector(state, candidates, bounds, 5.0).started, "sleeping actor rejected");
    candidates[0].awake = true;
    candidates[0].turning = true;
    Check(!UpdateCombatDirector(state, candidates, bounds, 2.0).started, "turning actor rejected");
    candidates[0].turning = false;
    candidates[0].wandering = false;
    Check(!UpdateCombatDirector(state, candidates, bounds, 2.0).started, "non-wandering actor rejected");
    candidates[0].wandering = true;
    candidates[0].actionActive = true;
    Check(!UpdateCombatDirector(state, candidates, bounds, 2.0).started, "active action rejected");
}

void TestSelectionReservationAndSinglePair()
{
    using namespace besktop;
    CombatDirectorState state;
    InitializeCombatDirector(state, true, 3, 4u);
    const CombatDirectorBounds bounds{0.0, 0.0, 1000.0, 800.0};
    std::array candidates{
        Candidate(0, 340.0, 400.0),
        Candidate(1, 540.0, 400.0),
        Candidate(2, 800.0, 400.0),
    };
    const CombatDirectorSelection selected = UpdateCombatDirector(state, candidates, bounds, 2.0);
    Check(selected.started, "eligible pair selected");
    Check(selected.attackerIndex != selected.defenderIndex, "pair actors differ");
    Check(selected.reservation.active, "reservation created");
    Check(CombatDirectorOwnsActor(state, selected.attackerIndex), "attacker occupied");
    Check(CombatDirectorOwnsActor(state, selected.defenderIndex), "defender occupied");
    Check(!UpdateCombatDirector(state, candidates, bounds, 20.0).started, "only one active pair");
    Check(IsInsideCombatReservation(selected.reservation, selected.reservation.centerX,
        selected.reservation.centerY), "reservation contains center");
}

void TestSpaceFailureAndCooldownRelease()
{
    using namespace besktop;
    CombatDirectorState state;
    InitializeCombatDirector(state, true, 2, 8u);
    std::array candidates{Candidate(0, 80.0, 80.0), Candidate(1, 220.0, 80.0)};
    const CombatDirectorBounds cramped{0.0, 0.0, 260.0, 160.0};
    Check(!UpdateCombatDirector(state, candidates, cramped, 2.0).started, "insufficient space rejected");

    const CombatDirectorBounds roomy{0.0, 0.0, 1000.0, 800.0};
    candidates[0].x = 350.0;
    candidates[0].y = 400.0;
    candidates[1].x = 550.0;
    candidates[1].y = 400.0;
    const CombatDirectorSelection selected = UpdateCombatDirector(state, candidates, roomy, 2.0);
    Check(selected.started, "selection retries after space failure");
    CompleteCombatDirectorInteraction(state);
    Check(state.phase == CombatDirectorPhase::Cooldown, "completion enters cooldown");
    Check(!state.reservation.active, "reservation released");
    Check(!CombatDirectorOwnsActor(state, selected.attackerIndex), "actors released");
    Check(!UpdateCombatDirector(state, candidates, roomy, 5.0).started, "global cooldown blocks immediate pair");
    Check(!UpdateCombatDirector(state, candidates, roomy, 3.0).started, "actor cooldown survives global cooldown");
    Check(UpdateCombatDirector(state, candidates, roomy, 3.0).started, "pair reusable after actor cooldown");
}

void TestScenarioRotation()
{
    using namespace besktop;
    CombatDirectorState state;
    InitializeCombatDirector(state, true, 10, 4u);
    const CombatDirectorBounds bounds{0.0, 0.0, 1000.0, 800.0};
    std::array candidates{Candidate(0, 350.0, 400.0), Candidate(1, 550.0, 400.0)};
    std::array<CombatScenarioId, 4> seen{};
    for (std::size_t index = 0; index < seen.size(); ++index) {
        const CombatDirectorSelection selected = UpdateCombatDirector(state, candidates, bounds, 20.0);
        Check(selected.started, "rotation selection starts");
        seen[index] = selected.scenario;
        CompleteCombatDirectorInteraction(state);
    }
    Check(seen[0] == CombatScenarioId::LeadParry, "rotation starts deterministically");
    Check(seen[1] == CombatScenarioId::LeadSlip, "rotation includes slip");
    Check(seen[2] == CombatScenarioId::UppercutLightHit, "rotation includes light hit");
    Check(seen[3] == CombatScenarioId::SideKickHeavyHit, "rotation includes heavy hit");
}

} // namespace

int main()
{
    TestDisabledAndEligibility();
    TestSelectionReservationAndSinglePair();
    TestSpaceFailureAndCooldownRelease();
    TestScenarioRotation();
    if (failures != 0) return 1;
    std::cout << "besktop_combat_director_tests: all checks passed\n";
    return 0;
}
