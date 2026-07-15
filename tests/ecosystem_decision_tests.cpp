#include "besktop/animation/actor_ecosystem.h"
#include "besktop/animation/encounter_arbiter.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <set>
#include <vector>

namespace {

int failures = 0;

void Check(bool condition, const char* name)
{
    if (!condition) {
        ++failures;
        std::cerr << "FAILED: " << name << '\n';
    }
}

besktop::ActorPerceptionInput Actor(
    std::size_t index,
    double x,
    double y,
    double velocityX = 0.0,
    double velocityY = 0.0)
{
    return {index, x, y, velocityX, velocityY, 1.0, 0.0, 48.0,
        true, true, false, false, false, false};
}

besktop::LocalEncounterRequest Request(
    std::size_t first,
    std::size_t second,
    double centerX,
    double centerY,
    double radius,
    double priority)
{
    return {
        first,
        second,
        besktop::LocalIntent::Challenge,
        besktop::LocalIntent::Respond,
        besktop::EncounterIntent::Combat,
        {centerX, centerY, radius},
        priority,
    };
}

void TestStableTendencies()
{
    using namespace besktop;
    const ActorBehaviorProfile first = GenerateActorBehaviorProfile(0x12345678u);
    const ActorBehaviorProfile second = GenerateActorBehaviorProfile(0x12345678u);
    Check(first.tendency == second.tendency && first.stableSeed == second.stableSeed,
        "same seed produces same behavior profile");

    std::set<ActorTendency> tendencies;
    for (std::uint32_t seed = 1; seed <= 256; ++seed) {
        tendencies.insert(GenerateActorBehaviorProfile(seed).tendency);
    }
    Check(tendencies.size() == 5, "stable tendency generation has basic distribution");
    Check(ActorAssessDurationAdjustment(ActorTendency::Calm) >
            ActorAssessDurationAdjustment(ActorTendency::Energetic),
        "tendencies make small stable waiting-time adjustments");
}

void TestIntentWindowAndRuntimeDecay()
{
    using namespace besktop;
    ActorRuntimeState state;
    InitializeActorRuntimeState(state, 42u);
    const ActorBehaviorProfile profile = GenerateActorBehaviorProfile(42u);
    const LocalPerception perception{true, 1, 150.0, 80.0, true, true};
    UpdateActorLocalIntent(state, profile, perception, LocalIntent::Approach, 0.0);
    const LocalIntent held = state.heldIntent;
    const std::size_t target = state.intentTargetActor;
    for (int frame = 0; frame < 3; ++frame) {
        Check(!UpdateActorLocalIntent(state, profile, perception, LocalIntent::Challenge, 0.10),
            "intent does not resample inside decision window");
        Check(state.heldIntent == held && state.intentTargetActor == target,
            "held intent remains stable inside decision window");
    }

    state.alertness = 0.8;
    state.agitation = 0.7;
    state.stamina = 0.4;
    state.encounterCooldownRemaining = 3.0;
    UpdateActorRuntimeState(state, 1.0);
    Check(state.alertness < 0.8 && state.agitation < 0.7,
        "temporary alertness and agitation decay");
    Check(state.stamina > 0.4 && state.encounterCooldownRemaining < 3.0,
        "stamina recovers and individual cooldown advances");
}

void TestLocalPerceptionEligibility()
{
    using namespace besktop;
    const EncounterBounds bounds{0.0, 0.0, 1200.0, 800.0};
    std::array actors{Actor(0, 400.0, 400.0, 30.0), Actor(1, 610.0, 400.0, -30.0)};
    std::array<ActorRuntimeState, 2> states{};
    InitializeActorRuntimeState(states[0], 1u);
    InitializeActorRuntimeState(states[1], 2u);
    Check(FindLocalPerception(0, actors, states, bounds).perceived,
        "near naturally approaching actor is perceived");

    actors[1].x = 1050.0;
    Check(!FindLocalPerception(0, actors, states, bounds).perceived,
        "far actor is not perceived");
    actors[1] = Actor(1, 610.0, 400.0, -30.0);

    const auto expectUnavailable = [&](const char* name) {
        Check(!FindLocalPerception(0, actors, states, bounds).perceived, name);
    };
    actors[1].awake = false;
    expectUnavailable("sleeping actor is not perceived as candidate");
    actors[1] = Actor(1, 610.0, 400.0, -30.0);
    actors[1].turning = true;
    expectUnavailable("turning actor is not perceived as candidate");
    actors[1] = Actor(1, 610.0, 400.0, -30.0);
    actors[1].actionActive = true;
    expectUnavailable("action actor is not perceived as candidate");
    actors[1] = Actor(1, 610.0, 400.0, -30.0);
    actors[1].recovering = true;
    expectUnavailable("recovering actor is not perceived as candidate");
    actors[1] = Actor(1, 610.0, 400.0, -30.0);
    states[1].encounterCooldownRemaining = 1.0;
    expectUnavailable("cooling actor is not perceived as candidate");
    states[1].encounterCooldownRemaining = 0.0;
    actors[1].controlled = true;
    expectUnavailable("controlled actor is not perceived as candidate");
}

void TestHandshakeRulesAndPDisable()
{
    using namespace besktop;
    Check(ResolveLocalIntentHandshake(LocalIntent::Challenge, LocalIntent::Avoid) ==
            EncounterIntent::Undecided,
        "challenge plus avoid does not force combat");
    Check(ResolveLocalIntentHandshake(LocalIntent::Challenge, LocalIntent::Respond) ==
            EncounterIntent::Combat,
        "challenge plus respond establishes combat handshake");
    Check(ResolveLocalIntentHandshake(LocalIntent::Challenge, LocalIntent::Yield) ==
            EncounterIntent::Yield,
        "challenge plus yield establishes yield handshake");
    Check(ResolveLocalIntentHandshake(LocalIntent::Approach, LocalIntent::Observe) ==
            EncounterIntent::Bluff,
        "approach plus observe establishes noncombat handshake");

    std::array actors{Actor(0, 400.0, 400.0), Actor(1, 600.0, 400.0)};
    std::array<ActorRuntimeState, 2> states{};
    InitializeActorRuntimeState(states[0], 1u);
    InitializeActorRuntimeState(states[1], 2u);
    states[0].heldIntent = LocalIntent::Challenge;
    states[0].intentTargetActor = 1;
    states[0].intentCandidateScore = 0.5;
    states[1].heldIntent = LocalIntent::Respond;
    states[1].intentTargetActor = 0;
    states[1].intentCandidateScore = 0.5;
    Check(BuildLocalEncounterRequests(actors, states, false).empty(),
        "P-disabled interactions create no new requests");
    Check(BuildLocalEncounterRequests(actors, states, true).size() == 1,
        "enabled compatible intentions create one request");
}

void TestArbiterConflictsAndConcurrency()
{
    using namespace besktop;
    const EncounterBounds bounds{0.0, 0.0, 1000.0, 800.0};
    const std::array actors{
        EncounterArbiterActor{0, false},
        EncounterArbiterActor{1, false},
        EncounterArbiterActor{2, false},
        EncounterArbiterActor{3, false},
    };

    std::array competing{
        Request(0, 1, 250.0, 300.0, 70.0, 1.0),
        Request(1, 2, 520.0, 300.0, 70.0, 2.0),
    };
    const EncounterArbitrationResult competition =
        ArbitrateEncounterRequests(competing, actors, bounds);
    Check(competition.accepted.size() == 1 &&
            competition.accepted.front().initiatorIndex == 1 &&
            competition.accepted.front().responderIndex == 2,
        "competing requests deterministically accept only higher priority request");

    std::array independent{
        Request(0, 1, 220.0, 300.0, 70.0, 1.0),
        Request(2, 3, 780.0, 300.0, 70.0, 1.0),
    };
    const EncounterArbitrationResult parallel =
        ArbitrateEncounterRequests(independent, actors, bounds);
    Check(parallel.accepted.size() == 2,
        "arbiter accepts multiple independent non-overlapping requests without fixed cap");

    std::array overlapping{
        Request(0, 1, 400.0, 300.0, 100.0, 2.0),
        Request(2, 3, 550.0, 300.0, 100.0, 1.0),
    };
    Check(ArbitrateEncounterRequests(overlapping, actors, bounds).accepted.size() == 1,
        "overlapping reservations cannot both be accepted");
    std::array outOfBounds{Request(0, 1, 40.0, 300.0, 80.0, 1.0)};
    Check(ArbitrateEncounterRequests(outOfBounds, actors, bounds).accepted.empty(),
        "out-of-bounds reservation is rejected safely");

    auto controlledActors = actors;
    controlledActors[1].controlled = true;
    std::array controlledRequest{Request(0, 1, 220.0, 300.0, 70.0, 1.0)};
    Check(ArbitrateEncounterRequests(controlledRequest, controlledActors, bounds).accepted.empty(),
        "actor owned by another controller cannot be accepted");

    std::array activeReservations{EncounterReservation{220.0, 300.0, 80.0}};
    Check(ArbitrateEncounterRequests(
            controlledRequest, actors, bounds, activeReservations).accepted.empty(),
        "request overlapping an active reservation is rejected");
}

void TestArbiterInputOrderDeterminism()
{
    using namespace besktop;
    const EncounterBounds bounds{0.0, 0.0, 1000.0, 800.0};
    const std::array actors{
        EncounterArbiterActor{0, false},
        EncounterArbiterActor{1, false},
        EncounterArbiterActor{2, false},
        EncounterArbiterActor{3, false},
    };
    std::vector requests{
        Request(0, 1, 230.0, 300.0, 70.0, 1.0),
        Request(1, 2, 500.0, 300.0, 70.0, 1.5),
        Request(2, 3, 770.0, 300.0, 70.0, 1.0),
    };
    const EncounterArbitrationResult first = ArbitrateEncounterRequests(requests, actors, bounds);
    std::reverse(requests.begin(), requests.end());
    const EncounterArbitrationResult second = ArbitrateEncounterRequests(requests, actors, bounds);
    Check(first.accepted.size() == second.accepted.size(),
        "arbiter accepted count is input-order independent");
    bool same = first.accepted.size() == second.accepted.size();
    for (std::size_t index = 0; same && index < first.accepted.size(); ++index) {
        same = first.accepted[index].initiatorIndex == second.accepted[index].initiatorIndex &&
            first.accepted[index].responderIndex == second.accepted[index].responderIndex;
    }
    Check(same, "arbiter accepted actor pairs are input-order independent");
}

} // namespace

int main()
{
    TestStableTendencies();
    TestIntentWindowAndRuntimeDecay();
    TestLocalPerceptionEligibility();
    TestHandshakeRulesAndPDisable();
    TestArbiterConflictsAndConcurrency();
    TestArbiterInputOrderDeterminism();

    if (failures == 0) {
        std::cout << "All ecosystem decision tests passed\n";
        return 0;
    }
    std::cerr << failures << " ecosystem decision tests failed\n";
    return 1;
}
