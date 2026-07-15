#include "besktop/animation/encounter_director.h"
#include "besktop/animation/combat_director.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>

namespace {

int failures = 0;

void Check(bool condition, const char* message)
{
    if (condition) return;
    ++failures;
    std::cerr << "FAILED: " << message << '\n';
}

bool Near(double first, double second, double tolerance = 1e-9)
{
    return std::abs(first - second) <= tolerance;
}

constexpr besktop::EncounterReservation kReservation{500.0, 400.0, 150.0};
constexpr besktop::EncounterBounds kBounds{50.0, 50.0, 950.0, 750.0};

std::uint32_t FindSeed(besktop::EncounterIntent intent)
{
    for (std::uint32_t seed = 1; seed < 10000; ++seed) {
        besktop::EncounterState state;
        besktop::BeginEncounter(state, seed, kReservation, kBounds, 40.0);
        if (state.intent == intent) return seed;
    }
    return 0;
}

besktop::EncounterState BeginForIntent(besktop::EncounterIntent intent)
{
    besktop::EncounterState state;
    const std::uint32_t seed = FindSeed(intent);
    Check(seed != 0, "intent seed found");
    besktop::BeginEncounter(state, seed, kReservation, kBounds, 40.0);
    return state;
}

void ReachIntent(besktop::EncounterState& state)
{
    besktop::EncounterReadiness ready;
    ready.atStations = true;
    ready.aligned = true;
    besktop::UpdateEncounter(state, ready, 8.0);
}

void TestDeterministicPlan()
{
    besktop::EncounterState first;
    besktop::EncounterState second;
    besktop::BeginEncounter(first, 71u, kReservation, kBounds, 40.0);
    besktop::BeginEncounter(second, 71u, kReservation, kBounds, 40.0);
    Check(first.intent == second.intent, "same seed keeps intent");
    Check(Near(first.assessDuration, second.assessDuration), "same seed keeps assess duration");
    Check(first.attackerActsFirst == second.attackerActsFirst, "same seed keeps acting side");
    Check(Near(first.attackerIntentTarget.x, second.attackerIntentTarget.x) &&
        Near(first.defenderIntentTarget.y, second.defenderIntentTarget.y),
        "same seed keeps intent targets");
    if (first.intent != besktop::EncounterIntent::Combat) {
        Check(besktop::IsEncounterPointSafe(
            first.attackerIntentTarget, kReservation, kBounds, 40.0),
            "attacker intent target remains safe");
        Check(besktop::IsEncounterPointSafe(
            first.defenderIntentTarget, kReservation, kBounds, 40.0),
            "defender intent target remains safe");
    }

    const auto firstPlan = besktop::BuildEncounterAftermathPlan(
        first.intent, besktop::CombatResult::Blocked, first.attackerActsFirst,
        kReservation, kBounds, 40.0);
    const auto secondPlan = besktop::BuildEncounterAftermathPlan(
        second.intent, besktop::CombatResult::Blocked, second.attackerActsFirst,
        kReservation, kBounds, 40.0);
    Check(Near(firstPlan.holdSeconds, secondPlan.holdSeconds) &&
        Near(firstPlan.attackerExit.x, secondPlan.attackerExit.x) &&
        Near(firstPlan.defenderExit.y, secondPlan.defenderExit.y),
        "same inputs keep aftermath plan");
}

void TestIntentIsSampledOnce()
{
    besktop::EncounterState state;
    besktop::BeginEncounter(state, 912u, kReservation, kBounds, 40.0);
    const auto intent = state.intent;
    const double assess = state.assessDuration;
    besktop::EncounterReadiness waiting;
    for (int i = 0; i < 120; ++i) besktop::UpdateEncounter(state, waiting, 1.0 / 60.0);
    Check(state.intent == intent, "intent is not resampled per frame");
    Check(Near(state.assessDuration, assess), "assess duration is not resampled per frame");
}

void TestOrderedProgressionAndLargeDelta()
{
    auto yield = BeginForIntent(besktop::EncounterIntent::Yield);
    besktop::EncounterReadiness waiting;
    besktop::UpdateEncounter(yield, waiting, 8.0);
    Check(yield.phase == besktop::EncounterPhase::Approaching,
        "large delta cannot skip station readiness");
    waiting.atStations = true;
    besktop::UpdateEncounter(yield, waiting, 8.0);
    Check(yield.phase == besktop::EncounterPhase::Facing,
        "large delta cannot skip facing readiness");
    waiting.aligned = true;
    besktop::UpdateEncounter(yield, waiting, 8.0);
    Check(yield.phase == besktop::EncounterPhase::Separating,
        "large delta crosses timed assess intent and aftermath in order");
    Check(!besktop::UpdateEncounter(yield, waiting, 8.0).completed,
        "separation readiness cannot be skipped");
    waiting.separated = true;
    besktop::UpdateEncounter(yield, waiting, 0.0);
    const auto completed = besktop::UpdateEncounter(yield, waiting, 0.0);
    Check(completed.completed, "completion emitted after separation");
    Check(!besktop::UpdateEncounter(yield, waiting, 8.0).completed,
        "completion event emitted once");
}

void TestCombatOnlyRequestsPair()
{
    for (const auto intent : {
            besktop::EncounterIntent::Combat,
            besktop::EncounterIntent::Yield,
            besktop::EncounterIntent::Bluff}) {
        auto state = BeginForIntent(intent);
        besktop::EncounterReadiness ready;
        ready.atStations = true;
        ready.aligned = true;
        const auto step = besktop::UpdateEncounter(state, ready, 8.0);
        Check(step.requestCombatStart == (intent == besktop::EncounterIntent::Combat),
            "only combat branch requests CombatPair");
        Check(!besktop::UpdateEncounter(state, ready, 8.0).requestCombatStart,
            "CombatPair start request emitted once");
        if (intent != besktop::EncounterIntent::Combat) {
            Check(state.aftermath.result == besktop::CombatResult::None,
                "non-combat branch has no contact result");
        }
    }
}

void TestCombatResultsProduceDistinctSafeAftermath()
{
    double previousHold = -1.0;
    for (const auto result : {
            besktop::CombatResult::HitLight,
            besktop::CombatResult::HitHeavy,
            besktop::CombatResult::Blocked,
            besktop::CombatResult::Evaded,
            besktop::CombatResult::Whiffed}) {
        const auto plan = besktop::BuildEncounterAftermathPlan(
            besktop::EncounterIntent::Combat, result, true,
            kReservation, kBounds, 40.0);
        Check(plan.result == result, "combat result preserved in aftermath");
        Check(plan.holdSeconds > 0.0, "aftermath has readable hold");
        Check(!Near(plan.holdSeconds, previousHold), "combat results use distinct hold timings");
        Check(besktop::IsEncounterPointSafe(plan.attackerExit, kReservation, kBounds, 40.0),
            "attacker exit remains safe");
        Check(besktop::IsEncounterPointSafe(plan.defenderExit, kReservation, kBounds, 40.0),
            "defender exit remains safe");
        Check(plan.heavyRootMotionAlreadyApplied == (result == besktop::CombatResult::HitHeavy),
            "only heavy result suppresses duplicate root motion");
        previousHold = plan.holdSeconds;
    }
}

void TestNonCombatHasNoActionOrContactSignal()
{
    for (const auto intent : {besktop::EncounterIntent::Yield, besktop::EncounterIntent::Bluff}) {
        auto state = BeginForIntent(intent);
        besktop::EncounterReadiness ready;
        ready.atStations = true;
        ready.aligned = true;
        bool requestedCombat = false;
        for (int i = 0; i < 30; ++i) {
            const auto step = besktop::UpdateEncounter(state, ready, 0.25);
            requestedCombat = requestedCombat || step.requestCombatStart;
            if (state.phase == besktop::EncounterPhase::Separating) break;
        }
        Check(!requestedCombat, "yield and bluff never request actions or contact");
        Check(state.aftermath.result == besktop::CombatResult::None,
            "yield and bluff keep combat result empty");
    }

    const auto yield = BeginForIntent(besktop::EncounterIntent::Yield);
    if (yield.attackerActsFirst) {
        Check(yield.attackerIntentTarget.x < kReservation.centerX &&
            yield.defenderIntentTarget.x > kReservation.centerX,
            "attacker yield backs away while defender observes");
    } else {
        Check(yield.defenderIntentTarget.x > kReservation.centerX &&
            yield.attackerIntentTarget.x < kReservation.centerX,
            "defender yield backs away while attacker observes");
    }
    const auto bluff = BeginForIntent(besktop::EncounterIntent::Bluff);
    if (bluff.attackerActsFirst) {
        Check(std::abs(bluff.attackerIntentTarget.x - kReservation.centerX) <
            std::abs(bluff.defenderIntentTarget.x - kReservation.centerX),
            "attacker bluff advances while defender yields space");
    } else {
        Check(std::abs(bluff.defenderIntentTarget.x - kReservation.centerX) <
            std::abs(bluff.attackerIntentTarget.x - kReservation.centerX),
            "defender bluff advances while attacker yields space");
    }
}

void TestCombatCompletionAndCancellation()
{
    auto combat = BeginForIntent(besktop::EncounterIntent::Combat);
    besktop::EncounterReadiness ready;
    ready.atStations = true;
    ready.aligned = true;
    ReachIntent(combat);
    Check(combat.phase == besktop::EncounterPhase::Combat, "combat enters shared pair phase");
    ready.combatComplete = true;
    ready.combatResult = besktop::CombatResult::HitLight;
    besktop::UpdateEncounter(combat, ready, 0.0);
    Check(combat.phase == besktop::EncounterPhase::Aftermath,
        "combat result enters aftermath");

    auto cancelled = BeginForIntent(besktop::EncounterIntent::Bluff);
    ready = {};
    ready.reservationSafe = false;
    const auto first = besktop::UpdateEncounter(cancelled, ready, 0.1);
    Check(first.cancelled && cancelled.phase == besktop::EncounterPhase::Cancelled,
        "unsafe reservation cancels encounter");
    Check(!besktop::UpdateEncounter(cancelled, ready, 8.0).cancelled,
        "cancellation emitted once");
}

void TestPoseChannelIsIndependent()
{
    auto bluff = BeginForIntent(besktop::EncounterIntent::Bluff);
    besktop::EncounterReadiness ready;
    ready.atStations = true;
    ready.aligned = true;
    besktop::UpdateEncounter(
        bluff, ready, besktop::GetEncounterTuning().noticeSeconds + bluff.assessDuration + 0.2);
    const auto actor = besktop::SampleEncounterPose(bluff, besktop::EncounterActorRole::Attacker);
    const auto defender = besktop::SampleEncounterPose(bluff, besktop::EncounterActorRole::Defender);
    Check(std::abs(actor.bodyRotateX) > 1e-4 || std::abs(defender.bodyRotateX) > 1e-4,
        "bluff samples additive encounter pose");
}

void TestDeferredDisableCompletesAllIntents()
{
    for (const auto intent : {
            besktop::EncounterIntent::Combat,
            besktop::EncounterIntent::Yield,
            besktop::EncounterIntent::Bluff}) {
        besktop::CombatDirectorState director;
        besktop::InitializeCombatDirector(director, true, 2, 19u);
        director.phase = besktop::CombatDirectorPhase::Active;
        director.attackerIndex = 0;
        director.defenderIndex = 1;
        director.reservation = {true, 500.0, 400.0, 150.0};
        Check(besktop::SetCombatDirectorEnabled(director, false) ==
            besktop::CombatDirectorModeChange::DisableDeferred,
            "P requests deferred disable while encounter is active");

        auto encounter = BeginForIntent(intent);
        besktop::EncounterReadiness ready;
        ready.atStations = true;
        ready.aligned = true;
        for (int update = 0; update < 32 && encounter.phase != besktop::EncounterPhase::Complete; ++update) {
            if (encounter.phase == besktop::EncounterPhase::Combat) {
                ready.combatComplete = true;
                ready.combatResult = besktop::CombatResult::Blocked;
            }
            if (encounter.phase == besktop::EncounterPhase::Separating) ready.separated = true;
            besktop::UpdateEncounter(encounter, ready, 1.0);
        }
        ready.separated = true;
        besktop::UpdateEncounter(encounter, ready, 0.0);
        besktop::CompleteCombatDirectorInteraction(director);
        Check(director.phase == besktop::CombatDirectorPhase::Disabled &&
            !director.reservation.active,
            "deferred P disable releases reservation after each intent");
    }
}

} // namespace

int main()
{
    TestDeterministicPlan();
    TestIntentIsSampledOnce();
    TestOrderedProgressionAndLargeDelta();
    TestCombatOnlyRequestsPair();
    TestCombatResultsProduceDistinctSafeAftermath();
    TestNonCombatHasNoActionOrContactSignal();
    TestCombatCompletionAndCancellation();
    TestPoseChannelIsIndependent();
    TestDeferredDisableCompletesAllIntents();
    if (failures != 0) {
        std::cerr << failures << " encounter test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "encounter director tests passed\n";
    return EXIT_SUCCESS;
}
