#include "besktop/animation/combat_director.h"

#include <algorithm>
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

void SkipOpening(besktop::CombatDirectorState& state)
{
    state.openingWanderRemaining = 0.0;
    state.retryRemaining = 0.0;
}

void TestOpeningDelayAndEligibility()
{
    using namespace besktop;
    CombatDirectorState state;
    const CombatDirectorBounds bounds{0.0, 0.0, 1000.0, 800.0};
    std::array candidates{Candidate(0, 350.0, 400.0), Candidate(1, 550.0, 400.0)};
    InitializeCombatDirector(state, false, candidates.size());
    Check(!UpdateCombatDirector(state, candidates, bounds, 20.0).started, "disabled director stays idle");

    InitializeCombatDirector(state, true, candidates.size(), 4u);
    Check(!UpdateCombatDirector(state, candidates, bounds, 5.5).started, "opening wander blocks early selection");
    Check(UpdateCombatDirector(state, candidates, bounds, 0.5).started, "selection starts after opening wander");

    InitializeCombatDirector(state, true, candidates.size(), 4u);
    candidates[0].awake = false;
    Check(!UpdateCombatDirector(state, candidates, bounds, 20.0).started, "opening waits until all actors awake");
    candidates[0].awake = true;
    Check(!UpdateCombatDirector(state, candidates, bounds, 5.5).started, "opening begins after all actors awake");
    Check(UpdateCombatDirector(state, candidates, bounds, 0.5).started, "post-awakening opening completes");

    InitializeCombatDirector(state, true, candidates.size());
    SkipOpening(state);
    candidates[0].turning = true;
    Check(!UpdateCombatDirector(state, candidates, bounds, 0.0).started, "turning actor rejected");
    candidates[0].turning = false;
    candidates[0].wandering = false;
    state.retryRemaining = 0.0;
    Check(!UpdateCombatDirector(state, candidates, bounds, 0.0).started, "non-wandering actor rejected");
    candidates[0].wandering = true;
    candidates[0].actionActive = true;
    state.retryRemaining = 0.0;
    Check(!UpdateCombatDirector(state, candidates, bounds, 0.0).started, "active action rejected");

    std::array oneCandidate{Candidate(0, 500.0, 400.0)};
    InitializeCombatDirector(state, true, oneCandidate.size());
    SkipOpening(state);
    Check(!UpdateCombatDirector(state, oneCandidate, bounds, 0.0).started,
        "insufficient actor count stays idle");
}

void TestReservationAvoidance()
{
    using namespace besktop;
    const CombatReservation reservation{true, 500.0, 400.0, 100.0};
    const CombatDirectorBounds bounds{100.0, 100.0, 900.0, 700.0};
    const CombatAvoidanceDecision crossing = ComputeCombatAvoidanceTarget(
        reservation, bounds, {2, 250.0, 400.0, 700.0, 400.0, 35.0, 0.0});
    Check(crossing.reselectTarget, "path through reservation replans");
    Check(!IsInsideCombatReservation(reservation, crossing.targetX, crossing.targetY, 35.0),
        "avoidance target remains outside reservation");
    Check(crossing.targetX >= 135.0 && crossing.targetX <= 865.0 &&
        crossing.targetY >= 135.0 && crossing.targetY <= 665.0,
        "avoidance target remains inside safe work bounds");

    const CombatAvoidanceDecision insideTarget = ComputeCombatAvoidanceTarget(
        reservation, bounds, {3, 250.0, 350.0, 500.0, 400.0, 35.0, 5.0});
    Check(insideTarget.reselectTarget, "target inside reservation overrides cooldown");

    const CombatAvoidanceDecision safe = ComputeCombatAvoidanceTarget(
        reservation, bounds, {1, 200.0, 200.0, 250.0, 180.0, 35.0, 0.0});
    Check(!safe.reselectTarget, "safe target is not needlessly replanned");

    const CombatAvoidanceDecision hysteresis = ComputeCombatAvoidanceTarget(
        reservation, bounds, {1, 360.0, 400.0, 250.0, 400.0, 35.0, 0.5});
    Check(!hysteresis.reselectTarget, "avoidance cooldown prevents per-frame boundary replans");

    const CombatAvoidanceDecision movingAway = ComputeCombatAvoidanceTarget(
        reservation, bounds, {1, 360.0, 400.0, 220.0, 400.0, 35.0, 0.0});
    Check(!movingAway.reselectTarget, "nearby actor already moving away keeps safe target");
}

void TestSelectionFairnessAndSinglePair()
{
    using namespace besktop;
    CombatDirectorState state;
    InitializeCombatDirector(state, true, 4, 4u);
    SkipOpening(state);
    const CombatDirectorBounds bounds{0.0, 0.0, 1200.0, 800.0};
    std::array candidates{
        Candidate(0, 270.0, 360.0),
        Candidate(1, 470.0, 360.0),
        Candidate(2, 710.0, 440.0),
        Candidate(3, 910.0, 440.0),
    };
    state.actorParticipationCounts[0] = 4u;
    state.actorParticipationCounts[1] = 4u;
    const CombatDirectorSelection first = UpdateCombatDirector(state, candidates, bounds, 0.0);
    Check(first.started, "eligible pair selected");
    Check(first.reservation.active, "reservation created");
    Check(first.attackerIndex >= 2 && first.defenderIndex >= 2,
        "recent participation count penalizes overused actors");
    Check(!UpdateCombatDirector(state, candidates, bounds, 60.0).started, "only one pair active");
    const auto firstPair = std::minmax(first.attackerIndex, first.defenderIndex);
    CompleteCombatDirectorInteraction(state);
    std::fill(state.actorParticipationCounts.begin(), state.actorParticipationCounts.end(), 0u);
    const CombatDirectorSelection second = UpdateCombatDirector(state, candidates, bounds, 60.0);
    Check(second.started, "second round starts after cooldown");
    const auto secondPair = std::minmax(second.attackerIndex, second.defenderIndex);
    Check(firstPair != secondPair, "recent pair is not immediately repeated when alternatives exist");
    Check(state.recentPairCount == 1u, "recent pair history retained after release");
}

void TestSpaceFailureReleaseAndResultHold()
{
    using namespace besktop;
    CombatDirectorState state;
    InitializeCombatDirector(state, true, 2, 8u);
    SkipOpening(state);
    std::array candidates{Candidate(0, 80.0, 80.0), Candidate(1, 220.0, 80.0)};
    const CombatDirectorBounds cramped{0.0, 0.0, 260.0, 160.0};
    Check(!UpdateCombatDirector(state, candidates, cramped, 0.0).started, "insufficient space rejected");
    Check(state.spaceRejectedTotal > 0, "space rejection summarized");

    const CombatDirectorBounds roomy{0.0, 0.0, 1000.0, 800.0};
    candidates[0].x = 350.0;
    candidates[0].y = 400.0;
    candidates[1].x = 550.0;
    candidates[1].y = 400.0;
    const CombatDirectorSelection selected = UpdateCombatDirector(state, candidates, roomy, 2.0);
    Check(selected.started, "selection retries quietly after space failure");
    Check(!AdvanceCombatDirectorResultHold(state, 0.30), "result hold keeps result readable");
    Check(AdvanceCombatDirectorResultHold(state, 0.20), "result hold completes deterministically");
    CompleteCombatDirectorInteraction(state);
    Check(state.phase == CombatDirectorPhase::Cooldown, "completion enters cooldown");
    Check(!state.reservation.active, "reservation released");
    Check(!CombatDirectorOwnsActor(state, selected.attackerIndex), "actors released");
    Check(!UpdateCombatDirector(state, candidates, roomy, 12.0).started, "sparse actor cooldown lowers frequency");
    Check(UpdateCombatDirector(state, candidates, roomy, 10.0).started, "actors become selectable after personal cooldown");
}

void TestScenarioRotationAndLargeDelta()
{
    using namespace besktop;
    CombatDirectorState state;
    InitializeCombatDirector(state, true, 10, 4u);
    SkipOpening(state);
    const CombatDirectorBounds bounds{0.0, 0.0, 1000.0, 800.0};
    std::array candidates{Candidate(0, 350.0, 400.0), Candidate(1, 550.0, 400.0)};
    std::array<CombatScenarioId, 4> seen{};
    for (std::size_t index = 0; index < seen.size(); ++index) {
        const CombatDirectorSelection selected = UpdateCombatDirector(state, candidates, bounds, 60.0);
        Check(selected.started, "large delta starts at most one round");
        Check(!UpdateCombatDirector(state, candidates, bounds, 60.0).started, "active round cannot restart");
        seen[index] = selected.scenario;
        CompleteCombatDirectorInteraction(state);
    }
    Check(seen[0] == CombatScenarioId::LeadParry, "rotation starts deterministically");
    Check(seen[1] == CombatScenarioId::LeadSlip, "rotation includes slip");
    Check(seen[2] == CombatScenarioId::UppercutLightHit, "rotation includes light hit");
    Check(seen[3] == CombatScenarioId::SideKickHeavyHit, "rotation includes heavy hit");
}

void TestParticipationSpreadsAcrossActors()
{
    using namespace besktop;
    CombatDirectorState state;
    InitializeCombatDirector(state, true, 6, 17u);
    SkipOpening(state);
    const CombatDirectorBounds bounds{0.0, 0.0, 1500.0, 900.0};
    std::array candidates{
        Candidate(0, 250.0, 300.0),
        Candidate(1, 450.0, 300.0),
        Candidate(2, 650.0, 300.0),
        Candidate(3, 850.0, 560.0),
        Candidate(4, 1050.0, 560.0),
        Candidate(5, 1250.0, 560.0),
    };
    for (int round = 0; round < 8; ++round) {
        const CombatDirectorSelection selected = UpdateCombatDirector(state, candidates, bounds, 60.0);
        Check(selected.started, "fairness simulation round selected");
        CompleteCombatDirectorInteraction(state);
    }
    const std::size_t represented = static_cast<std::size_t>(std::count_if(
        state.actorParticipationCounts.begin(), state.actorParticipationCounts.end(),
        [](unsigned int count) { return count > 0; }));
    Check(represented >= 5, "multi-round selection gives most actors an opportunity");
}

} // namespace

int main()
{
    TestOpeningDelayAndEligibility();
    TestReservationAvoidance();
    TestSelectionFairnessAndSinglePair();
    TestSpaceFailureReleaseAndResultHold();
    TestScenarioRotationAndLargeDelta();
    TestParticipationSpreadsAcrossActors();
    if (failures != 0) return 1;
    std::cout << "besktop_combat_director_tests: all checks passed\n";
    return 0;
}
