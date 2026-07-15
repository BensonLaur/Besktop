#include "besktop/animation/combat_episode.h"

#include <array>
#include <cmath>
#include <iostream>
#include <set>
#include <tuple>
#include <vector>

namespace {

int failures = 0;

void Check(bool condition, const char* message)
{
    if (condition) return;
    ++failures;
    std::cerr << "FAIL: " << message << '\n';
}

struct ExchangeRecord {
    std::size_t attacker = 0;
    besktop::CombatScenarioId scenario = besktop::CombatScenarioId::None;
    bool operator==(const ExchangeRecord&) const = default;
};

std::vector<ExchangeRecord> RunEpisode(
    std::uint32_t seed,
    besktop::ActorTendency first = besktop::ActorTendency::Calm,
    besktop::ActorTendency second = besktop::ActorTendency::Calm,
    besktop::CombatResult result = besktop::CombatResult::Blocked)
{
    using namespace besktop;
    CombatEpisodeState state;
    BeginCombatEpisode(state, seed, first, second, true, CombatScenarioId::LeadParry);
    std::vector<ExchangeRecord> records;
    for (int guard = 0; guard < 20 && !state.completed; ++guard) {
        const CombatEpisodeStep started = UpdateCombatEpisode(state, {true, true}, 0.0);
        if (started.startExchange) records.push_back({started.attackerRole, started.scenario});
        UpdateCombatEpisode(state, {true, true, true, result, false}, 0.0);
        const CombatEpisodeStep regrouped = UpdateCombatEpisode(state, {true, true}, 1.0);
        if (regrouped.startExchange) records.push_back({regrouped.attackerRole, regrouped.scenario});
    }
    return records;
}

void TestDeterminismAndDistribution()
{
    using namespace besktop;
    Check(RunEpisode(91) == RunEpisode(91), "same seed keeps exchange roles and scenarios");
    std::array<int, 8> counts{};
    for (std::uint32_t seed = 1; seed <= 500; ++seed) {
        CombatEpisodeState state;
        BeginCombatEpisode(state, seed, ActorTendency::Calm, ActorTendency::Calm, true);
        Check(state.plannedExchangeCount >= 3 && state.plannedExchangeCount <= 7,
            "planned exchange count stays in 3..7");
        ++counts[state.plannedExchangeCount];
    }
    const int common = counts[4] + counts[5] + counts[6];
    Check(common > counts[3] + counts[7], "4..6 exchanges dominate ordinary distribution");
    Check(counts[3] > 0 && counts[7] > 0, "rare 3 and 7 exchange plans remain reachable");
}

void TestScenarioAndRoleRules()
{
    using namespace besktop;
    CombatEpisodeState heavySeeded;
    BeginCombatEpisode(
        heavySeeded, 42, ActorTendency::Calm, ActorTendency::Calm, true,
        CombatScenarioId::SideKickHeavyHit);
    Check(heavySeeded.currentScenario != CombatScenarioId::SideKickHeavyHit,
        "heavy scene is not used as an opening exchange even when pool rotation points to it");
    for (std::uint32_t seed = 10; seed < 100; ++seed) {
        const auto records = RunEpisode(seed);
        Check(!records.empty(), "episode starts exchanges");
        int sameAttackerRun = 1;
        std::array<int, 2> attacks{};
        for (std::size_t i = 0; i < records.size(); ++i) {
            ++attacks[records[i].attacker];
            if (i > 0) {
                Check(records[i].scenario != records[i - 1].scenario,
                    "scenario never repeats immediately");
                sameAttackerRun = records[i].attacker == records[i - 1].attacker ?
                    sameAttackerRun + 1 : 1;
                Check(sameAttackerRun <= 2, "one actor attacks at most twice in succession");
            }
            if (records[i].scenario == CombatScenarioId::SideKickHeavyHit) {
                Check(i >= 2, "heavy side kick is reserved for the later episode");
            }
        }
        Check(attacks[0] > 0 && attacks[1] > 0,
            "both actors normally receive an attacking turn");
    }
}

void TestDefensiveResultsTransferInitiative()
{
    using namespace besktop;
    for (const CombatResult result :
        {CombatResult::Blocked, CombatResult::Evaded, CombatResult::Whiffed}) {
        CombatEpisodeState state;
        BeginCombatEpisode(state, 177, ActorTendency::Calm, ActorTendency::Calm, true,
            CombatScenarioId::LeadParry);
        UpdateCombatEpisode(state, {true, true}, 0.0);
        UpdateCombatEpisode(state, {true, true, true, result, false}, 0.0);
        const CombatEpisodeStep next = UpdateCombatEpisode(state, {true, true}, 1.0);
        Check(next.startExchange && next.attackerRole == 1,
            "block evade and whiff give the defender immediate initiative");
    }
}

void TestInternalStateAndFinishWeights()
{
    using namespace besktop;
    CombatEpisodeState light;
    BeginCombatEpisode(light, 9, ActorTendency::Calm, ActorTendency::Calm, true);
    UpdateCombatEpisode(light, {true, true}, 0.0);
    UpdateCombatEpisode(light, {true, true, true, CombatResult::HitLight, false}, 0.0);
    Check(std::abs(light.stamina[1] - 0.82) < 1e-9,
        "light hit reduces defender stamina by the documented amount");
    Check(light.pressure[1] > 0.0 && light.initiative[0] > 0.0,
        "light hit updates pressure and initiative");

    int lightEarlyFinishes = 0;
    int heavyEarlyFinishes = 0;
    for (std::uint32_t seed = 1; seed <= 300; ++seed) {
        for (const auto [result, counter] : {
            std::pair{CombatResult::HitLight, &lightEarlyFinishes},
            std::pair{CombatResult::HitHeavy, &heavyEarlyFinishes}}) {
            CombatEpisodeState state;
            BeginCombatEpisode(state, seed, ActorTendency::Calm, ActorTendency::Calm, true);
            for (int exchange = 0; exchange < 3; ++exchange) {
                UpdateCombatEpisode(state, {true, true}, 1.0);
                UpdateCombatEpisode(state, {true, true, true, result, false}, 0.0);
                UpdateCombatEpisode(state, {true, true}, 1.0);
            }
            if (state.completed || state.finishAfterRegroup) ++*counter;
        }
    }
    Check(heavyEarlyFinishes > lightEarlyFinishes * 2,
        "heavy hit raises early finish probability substantially");

    int timidFinishes = 0;
    int boldFinishes = 0;
    for (std::uint32_t seed = 1; seed <= 250; ++seed) {
        for (const auto [tendency, counter] : {
            std::pair{ActorTendency::Timid, &timidFinishes},
            std::pair{ActorTendency::Bold, &boldFinishes}}) {
            CombatEpisodeState state;
            BeginCombatEpisode(state, seed, ActorTendency::Calm, tendency, true);
            for (int exchange = 0; exchange < 3; ++exchange) {
                UpdateCombatEpisode(state, {true, true}, 1.0);
                UpdateCombatEpisode(
                    state, {true, true, true, CombatResult::HitLight, false}, 0.0);
                UpdateCombatEpisode(state, {true, true}, 1.0);
            }
            if (state.completed || state.finishAfterRegroup) ++*counter;
        }
    }
    Check(timidFinishes > boldFinishes,
        "timid and bold have distinct deterministic continuation tendencies");
}

void TestRecoveryContactAndAftermathBoundary()
{
    using namespace besktop;
    CombatEpisodeState state;
    BeginCombatEpisode(state, 31, ActorTendency::Calm, ActorTendency::Calm, true);
    UpdateCombatEpisode(state, {true, true}, 0.0);
    const std::size_t before = state.completedExchangeCount;
    UpdateCombatEpisode(state, {true, true, false, CombatResult::HitLight, false}, 3.0);
    Check(state.completedExchangeCount == before,
        "next exchange cannot start before CombatPair fully recovers");
    UpdateCombatEpisode(state, {true, true, true, CombatResult::HitLight, false}, 0.0);
    const std::size_t consumed = state.completedExchangeCount;
    UpdateCombatEpisode(state, {true, true, true, CombatResult::HitLight, false}, 0.0);
    Check(state.completedExchangeCount == consumed,
        "one recovered pair result is consumed once");
    Check(!state.completed, "intermediate exchanges do not complete Encounter aftermath");

    state.finishRequested = true;
    const CombatEpisodeStep completed = UpdateCombatEpisode(state, {true, true}, 1.0);
    Check(completed.episodeCompleted, "episode emits final completion once after regroup");
    Check(!UpdateCombatEpisode(state, {true, true}, 1.0).episodeCompleted,
        "final aftermath completion cannot be emitted twice");
}

void TestStopTimeoutAndLargeDelta()
{
    using namespace besktop;
    CombatEpisodeState stopped;
    BeginCombatEpisode(stopped, 2, ActorTendency::Calm, ActorTendency::Calm, true);
    UpdateCombatEpisode(stopped, {true, true}, 0.0);
    UpdateCombatEpisode(
        stopped, {true, true, false, CombatResult::None, true}, 8.0);
    Check(stopped.phase == CombatEpisodePhase::ExchangeActive,
        "P waits for the current pair instead of cutting it");
    UpdateCombatEpisode(
        stopped, {true, true, true, CombatResult::Blocked, true}, 0.0);
    const CombatEpisodeStep stoppedDone = UpdateCombatEpisode(stopped, {true, true}, 8.0);
    Check(stoppedDone.episodeCompleted &&
        stopped.finishReason == CombatEpisodeFinishReason::StopRequested,
        "P finishes after current exchange without enforcing minimum count");

    CombatEpisodeState timed;
    BeginCombatEpisode(timed, 3, ActorTendency::Calm, ActorTendency::Calm, true);
    UpdateCombatEpisode(timed, {true, true}, 0.0);
    for (int i = 0; i < 6; ++i) {
        UpdateCombatEpisode(timed, {true, true, false, CombatResult::None, false}, 4.0);
    }
    Check(timed.timedOut && timed.phase == CombatEpisodePhase::ExchangeActive,
        "hard timeout requests a safe finish without interrupting action");
    UpdateCombatEpisode(timed, {true, true, true, CombatResult::Whiffed, false}, 0.0);
    UpdateCombatEpisode(timed, {true, true}, 4.0);
    Check(timed.completed && timed.finishReason == CombatEpisodeFinishReason::Timeout,
        "timed out episode completes safely after recovery");
}

void TestParallelIndependenceAndCancellation()
{
    using namespace besktop;
    CombatEpisodeState first;
    CombatEpisodeState second;
    BeginCombatEpisode(first, 11, ActorTendency::Bold, ActorTendency::Timid, true);
    BeginCombatEpisode(second, 29, ActorTendency::Curious, ActorTendency::Energetic, false);
    const std::uint32_t secondRandom = second.randomState;
    const std::size_t secondCount = second.completedExchangeCount;
    UpdateCombatEpisode(first, {true, true}, 0.0);
    UpdateCombatEpisode(first, {true, true, true, CombatResult::Blocked, false}, 0.0);
    Check(second.randomState == secondRandom && second.completedExchangeCount == secondCount,
        "parallel episodes do not share random or exchange state");
    CancelCombatEpisode(first);
    Check(first.cancelled && second.phase == CombatEpisodePhase::Ready,
        "cancelling one episode leaves the other untouched");
}

void TestFixedPreviewRemainsOnePair()
{
    using namespace besktop;
    CombatPairState pair;
    CombatPairReadiness readiness{true, true, true, false, false};
    const CombatPairPlan& plan = GetCombatPairPlan(CombatScenarioId::LeadParry);
    UpdateCombatPair(pair, plan, readiness, 1.0);
    Check(pair.attackerStarted && pair.contactConsumed,
        "fixed preview still drives the original one-exchange CombatPair directly");
}

void TestFinalAttackerMapsToPhysicalAftermath()
{
    using namespace besktop;
    const EncounterReservation reservation{500.0, 400.0, 180.0};
    const EncounterBounds bounds{0.0, 0.0, 1000.0, 800.0};
    const EncounterAftermathPlan primary = BuildEncounterAftermathPlan(
        EncounterIntent::Combat, CombatResult::HitLight, true,
        reservation, bounds, 30.0, true);
    const EncounterAftermathPlan counterpart = BuildEncounterAftermathPlan(
        EncounterIntent::Combat, CombatResult::HitLight, true,
        reservation, bounds, 30.0, false);
    Check(primary.attackerExit.x < reservation.centerX &&
        primary.defenderExit.x > reservation.centerX,
        "primary final attacker preserves physical left and right stations");
    Check(counterpart.attackerExit.x < reservation.centerX &&
        counterpart.defenderExit.x > reservation.centerX,
        "counterattack aftermath does not swap or cross physical stations");
    Check(primary.attackerDepartureDelaySeconds ==
            counterpart.defenderDepartureDelaySeconds &&
        primary.defenderDepartureDelaySeconds ==
            counterpart.attackerDepartureDelaySeconds,
        "final counterattacker swaps semantic departure priority");
}

} // namespace

int main()
{
    TestDeterminismAndDistribution();
    TestScenarioAndRoleRules();
    TestDefensiveResultsTransferInitiative();
    TestInternalStateAndFinishWeights();
    TestRecoveryContactAndAftermathBoundary();
    TestStopTimeoutAndLargeDelta();
    TestParallelIndependenceAndCancellation();
    TestFixedPreviewRemainsOnePair();
    TestFinalAttackerMapsToPhysicalAftermath();
    if (failures == 0) std::cout << "besktop_combat_episode_tests: all checks passed\n";
    return failures == 0 ? 0 : 1;
}
