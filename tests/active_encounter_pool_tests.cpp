#include "besktop/animation/active_encounter_pool.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <vector>

namespace {

int failures = 0;

void Expect(bool value, const char* message)
{
    if (!value) {
        std::cerr << "FAILED: " << message << '\n';
        ++failures;
    }
}

besktop::LocalEncounterRequest Request(
    std::size_t a, std::size_t b, double x, double y, double priority = 1.0)
{
    return {a, b, besktop::LocalIntent::Approach, besktop::LocalIntent::Observe,
        besktop::EncounterIntent::Bluff, {x, y, 55.0}, priority};
}

std::vector<besktop::ActiveEncounterActorInput> Actors(std::size_t count)
{
    std::vector<besktop::ActiveEncounterActorInput> actors;
    for (std::size_t i = 0; i < count; ++i) {
        actors.push_back({i, 120.0 + i * 35.0, 180.0, 48.0, 0.0});
    }
    return actors;
}

besktop::ActiveEncounterPoolState ReadyPool(std::size_t count, std::uint32_t seed = 7)
{
    besktop::ActiveEncounterPoolState state;
    besktop::InitializeActiveEncounterPool(state, count, true, seed);
    state.openingWanderRemaining = 0.0;
    return state;
}

void TestParallelAndNoFixedCap()
{
    const besktop::EncounterBounds bounds{0.0, 0.0, 1200.0, 700.0};
    const auto actors = Actors(8);
    const std::vector<besktop::LocalEncounterRequest> requests{
        Request(0, 1, 140.0, 220.0), Request(2, 3, 420.0, 220.0),
        Request(4, 5, 700.0, 220.0), Request(6, 7, 980.0, 220.0)};
    auto state = ReadyPool(8);
    const auto result = besktop::StartAcceptedEncounters(state, requests, actors, bounds);
    Expect(result.startedIds.size() == 4, "four independent encounters start without a cap");
    Expect(state.encounters.size() == 4, "pool owns all four encounters");
    for (std::size_t i = 0; i < 8; ++i) {
        Expect(besktop::ActiveEncounterPoolOwnsActor(state, i), "each accepted actor is occupied");
    }
}

void TestConflictsAndIndependentRelease()
{
    const besktop::EncounterBounds bounds{0.0, 0.0, 900.0, 600.0};
    const auto actors = Actors(6);
    auto state = ReadyPool(6);
    std::vector<besktop::LocalEncounterRequest> requests{
        Request(0, 1, 150.0, 200.0, 3.0), Request(0, 2, 420.0, 200.0, 2.0),
        Request(3, 4, 165.0, 205.0, 1.0), Request(4, 5, 700.0, 200.0, 0.5)};
    const auto result = besktop::StartAcceptedEncounters(state, requests, actors, bounds);
    Expect(result.startedIds.size() == 2, "actor and reservation conflicts reject only unsafe requests");
    const auto first = result.startedIds.front();
    const auto second = result.startedIds.back();
    auto* a = besktop::FindActiveEncounter(state, first);
    auto* b = besktop::FindActiveEncounter(state, second);
    besktop::UpdateEncounter(a->encounter, {true, true, false, false, false, false, {}}, 0.10);
    besktop::UpdateEncounter(b->encounter, {true, true, false, false, false, false, {}}, 0.40);
    Expect(a->encounter.phaseTime != b->encounter.phaseTime ||
        a->encounter.phase != b->encounter.phase, "encounter timers advance independently");
    Expect(besktop::ReleaseActiveEncounter(state, first, false), "first encounter releases once");
    Expect(besktop::FindActiveEncounter(state, second) != nullptr, "second encounter survives release");
    Expect(!besktop::ReleaseActiveEncounter(state, first, false), "release is idempotent");
    besktop::CleanupReleasedEncounters(state);
    Expect(state.encounters.size() == 1, "cleanup removes only released item");
    Expect(besktop::ReleaseActiveEncounter(state, second, true), "abnormal release succeeds");
    besktop::CleanupReleasedEncounters(state);
    Expect(state.encounters.empty(), "pool becomes empty");
    Expect(std::none_of(state.actorEncounterIds.begin(), state.actorEncounterIds.end(),
        [](auto id) { return id != besktop::kNoActiveEncounterId; }), "no busy actor leaks");
}

void TestAvoidanceAndToggle()
{
    const besktop::EncounterBounds bounds{0.0, 0.0, 900.0, 600.0};
    const auto actors = Actors(4);
    auto state = ReadyPool(4);
    const std::vector<besktop::LocalEncounterRequest> requests{
        Request(0, 1, 180.0, 220.0), Request(2, 3, 620.0, 220.0)};
    besktop::StartAcceptedEncounters(state, requests, actors, bounds);
    const auto avoidance = besktop::ComputeActiveEncounterAvoidanceTarget(
        state, {0.0, 0.0, 900.0, 600.0},
        {9, 50.0, 220.0, 620.0, 220.0, 30.0, 0.0});
    Expect(avoidance.reselectTarget, "non-participant avoids every active reservation");
    Expect(!besktop::IsInsideAnyActiveEncounterReservation(
        state, avoidance.targetX, avoidance.targetY, 30.0), "avoidance target is outside all reservations");
    besktop::SetActiveEncounterPoolEnabled(state, false);
    Expect(!besktop::ActiveEncounterPoolMayAccept(state), "P off prevents new encounters");
    Expect(state.encounters.size() == 2 && state.encounters[0].finishingNaturally,
        "existing encounters remain for natural completion");
    for (const auto& encounter : state.encounters) {
        besktop::ReleaseActiveEncounter(state, encounter.id, false);
    }
    besktop::CleanupReleasedEncounters(state);
    Expect(state.encounters.empty(), "P off drains to pure wandering");
    besktop::SetActiveEncounterPoolEnabled(state, true);
    Expect(!besktop::ActiveEncounterPoolMayAccept(state), "P on keeps resume wander delay");
    for (int i = 0; i < 20; ++i) {
        besktop::UpdateActiveEncounterPoolClock(state, 1.0, true);
    }
    Expect(besktop::ActiveEncounterPoolMayAccept(state), "pool accepts after resume delay");
    besktop::SetActiveEncounterPoolPreviewSuspended(state, true);
    Expect(!besktop::ActiveEncounterPoolMayAccept(state), "fixed preview suspends product pool");
}

void TestDeterminism()
{
    const besktop::EncounterBounds bounds{0.0, 0.0, 1000.0, 600.0};
    const auto actors = Actors(6);
    std::vector<besktop::LocalEncounterRequest> first{
        Request(4, 5, 760.0, 200.0, 1.0), Request(0, 1, 160.0, 200.0, 3.0),
        Request(2, 3, 460.0, 200.0, 2.0)};
    auto second = first;
    std::reverse(second.begin(), second.end());
    auto a = ReadyPool(6, 99);
    auto b = ReadyPool(6, 99);
    besktop::StartAcceptedEncounters(a, first, actors, bounds);
    besktop::StartAcceptedEncounters(b, second, actors, bounds);
    Expect(a.encounters.size() == b.encounters.size(), "deterministic pool sizes");
    for (std::size_t i = 0; i < a.encounters.size(); ++i) {
        Expect(a.encounters[i].attackerIndex == b.encounters[i].attackerIndex &&
            a.encounters[i].defenderIndex == b.encounters[i].defenderIndex &&
            a.encounters[i].scenario == b.encounters[i].scenario,
            "input order does not change deterministic result");
    }
}

} // namespace

int main()
{
    TestParallelAndNoFixedCap();
    TestConflictsAndIndependentRelease();
    TestAvoidanceAndToggle();
    TestDeterminism();
    if (failures != 0) return 1;
    std::cout << "active encounter pool tests passed\n";
    return 0;
}
