#include "besktop/animation/actor_event_reaction.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace {

void Expect(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

besktop::ActorEventReactionInput Actor(
    std::size_t index,
    besktop::ActorTendency tendency,
    double x = 250.0,
    double y = 300.0)
{
    return {
        index, x, y, 30.0, 0.0, 1.0, 0.0, 48.0, tendency,
        0.25, 0.15, true, true, false, false, false, false,
    };
}

besktop::EncounterEventSnapshot Event(
    std::uint64_t id,
    double x = 400.0,
    double y = 300.0,
    double salience = 1.0)
{
    return {
        id, {x, y}, {x, y, 105.0}, besktop::EncounterPhase::Combat,
        true, 1, true, besktop::CombatResult::HitHeavy, salience, false, false,
    };
}

besktop::EncounterBounds Bounds()
{
    return {0.0, 0.0, 1000.0, 700.0};
}

besktop::ActorEventReactionBatchStats Update(
    std::vector<besktop::ActorEventReactionInput>& actors,
    std::vector<besktop::ActorEventReactionState>& states,
    std::vector<besktop::ActorEventReactionStep>& steps,
    const std::vector<besktop::EncounterEventSnapshot>& events,
    const std::vector<besktop::EncounterReservation>& reservations,
    bool enabled = true,
    double delta = 0.1,
    besktop::EncounterBounds bounds = Bounds())
{
    return besktop::UpdateActorEventReactions(
        actors, states, steps, events, reservations, bounds, enabled, delta);
}

void Initialize(
    std::vector<besktop::ActorEventReactionState>& states,
    std::uint32_t seed = 1)
{
    for (std::size_t index = 0; index < states.size(); ++index) {
        besktop::InitializeActorEventReactionState(
            states[index], seed + static_cast<std::uint32_t>(index * 101));
    }
}

void RunUntilDecision(
    std::vector<besktop::ActorEventReactionInput>& actors,
    std::vector<besktop::ActorEventReactionState>& states,
    std::vector<besktop::ActorEventReactionStep>& steps,
    const std::vector<besktop::EncounterEventSnapshot>& events,
    const std::vector<besktop::EncounterReservation>& reservations,
    bool enabled = true)
{
    for (int frame = 0; frame < 30; ++frame) {
        Update(actors, states, steps, events, reservations, enabled, 0.1);
        if (states[0].kind != besktop::ActorEventReactionKind::None) return;
    }
}

void TestDeterminismAndAvailability()
{
    std::vector actors{Actor(0, besktop::ActorTendency::Curious)};
    std::vector events{Event(11)};
    std::vector reservations{events[0].reservation};
    std::vector<besktop::ActorEventReactionState> first(1), second(1);
    std::vector<besktop::ActorEventReactionStep> firstSteps(1), secondSteps(1);
    Initialize(first, 77);
    Initialize(second, 77);
    for (int frame = 0; frame < 12; ++frame) {
        Update(actors, first, firstSteps, events, reservations);
        Update(actors, second, secondSteps, events, reservations);
    }
    Expect(first[0].kind == second[0].kind && first[0].encounterId == second[0].encounterId &&
        std::abs(first[0].remainingSeconds - second[0].remainingSeconds) < 1e-9,
        "same seed and input produce identical reaction");

    for (int unavailable = 0; unavailable < 5; ++unavailable) {
        auto input = Actor(0, besktop::ActorTendency::Curious);
        if (unavailable == 0) input.awake = false;
        if (unavailable == 1) input.controlled = true;
        if (unavailable == 2) input.turning = true;
        if (unavailable == 3) input.actionActive = true;
        if (unavailable == 4) input.recovering = true;
        std::vector unavailableActors{input};
        std::vector<besktop::ActorEventReactionState> states(1);
        std::vector<besktop::ActorEventReactionStep> steps(1);
        Initialize(states, 12);
        for (int frame = 0; frame < 20; ++frame) {
            Update(unavailableActors, states, steps, events, reservations);
        }
        Expect(states[0].kind == besktop::ActorEventReactionKind::None,
            "unavailable actor is not taken over");
    }

    actors[0].x = 0.0;
    events[0].center = {900.0, 650.0};
    events[0].reservation = {900.0, 650.0, 40.0};
    reservations[0] = events[0].reservation;
    std::vector<besktop::ActorEventReactionState> farStates(1);
    std::vector<besktop::ActorEventReactionStep> farSteps(1);
    Initialize(farStates, 15);
    for (int frame = 0; frame < 20; ++frame) {
        Update(actors, farStates, farSteps, events, reservations);
    }
    Expect(farStates[0].kind == besktop::ActorEventReactionKind::None,
        "far actor ignores event");
}

void TestTendencyDifferences()
{
    int curiousObserves = 0;
    int calmObserves = 0;
    int timidAvoids = 0;
    int energeticMoving = 0;
    for (std::uint32_t seed = 1; seed <= 160; ++seed) {
        for (const auto tendency : {
                 besktop::ActorTendency::Curious,
                 besktop::ActorTendency::Calm,
                 besktop::ActorTendency::Timid,
                 besktop::ActorTendency::Energetic}) {
            std::vector actors{Actor(0, tendency)};
            std::vector states(1, besktop::ActorEventReactionState{});
            std::vector steps(1, besktop::ActorEventReactionStep{});
            std::vector events{Event(21)};
            std::vector reservations{events[0].reservation};
            Initialize(states, seed);
            Update(actors, states, steps, events, reservations);
            if (tendency == besktop::ActorTendency::Curious &&
                states[0].kind == besktop::ActorEventReactionKind::Observing) ++curiousObserves;
            if (tendency == besktop::ActorTendency::Calm &&
                states[0].kind == besktop::ActorEventReactionKind::Observing) ++calmObserves;
            if (tendency == besktop::ActorTendency::Timid &&
                states[0].kind == besktop::ActorEventReactionKind::Avoiding) ++timidAvoids;
            if (tendency == besktop::ActorTendency::Energetic &&
                states[0].kind != besktop::ActorEventReactionKind::None &&
                states[0].keepMoving) ++energeticMoving;
        }
    }
    Expect(curiousObserves > calmObserves * 2 + 10,
        "Curious observes much more often than Calm");
    Expect(timidAvoids > 50, "Timid commonly avoids heavy hits");
    Expect(energeticMoving > 35, "Energetic can react while moving");
}

void TestSafeObservationAndFallback()
{
    std::vector actors{Actor(0, besktop::ActorTendency::Bold, 220.0, 300.0)};
    std::vector events{Event(31)};
    std::vector reservations{events[0].reservation, {660.0, 300.0, 90.0}};
    std::vector<besktop::ActorEventReactionState> states(1);
    std::vector<besktop::ActorEventReactionStep> steps(1);
    Initialize(states, 4);
    RunUntilDecision(actors, states, steps, events, reservations);
    Expect(states[0].kind == besktop::ActorEventReactionKind::Observing,
        "Bold can approach a safe observation point");
    Expect(besktop::IsReactionObservationPointSafe(
        states[0].target, 29.76, reservations, Bounds()),
        "observation point stays outside all reservations and bounds");
    Expect(std::hypot(states[0].target.x - events[0].center.x,
        states[0].target.y - events[0].center.y) > events[0].reservation.radius,
        "observation point stays outside target reservation");

    const besktop::EncounterBounds tiny{300.0, 220.0, 500.0, 380.0};
    actors[0].x = 310.0;
    actors[0].y = 300.0;
    events[0].center = {400.0, 300.0};
    events[0].reservation = {400.0, 300.0, 90.0};
    reservations = {events[0].reservation, {400.0, 300.0, 150.0}};
    Initialize(states, 4);
    bool rejected = false;
    for (int frame = 0; frame < 30; ++frame) {
        const auto stats = Update(
            actors, states, steps, events, reservations, true, 0.1, tiny);
        rejected = rejected || stats.unsafeObservationPointRejectionCount > 0;
    }
    Expect(rejected, "unsafe observation point is rejected");
    Expect(states[0].kind != besktop::ActorEventReactionKind::Observing,
        "unsafe observation safely degrades instead of entering reservation");
}

void TestSelectionHysteresisAndContactDeduplication()
{
    std::vector actors{Actor(0, besktop::ActorTendency::Curious, 300.0, 300.0)};
    std::vector events{Event(41, 430.0, 300.0, 0.90), Event(42, 650.0, 300.0, 1.0)};
    std::vector reservations{events[0].reservation, events[1].reservation};
    std::vector<besktop::ActorEventReactionState> states(1);
    std::vector<besktop::ActorEventReactionStep> steps(1);
    Initialize(states, 7);
    RunUntilDecision(actors, states, steps, events, reservations);
    Expect(states[0].encounterId == 41, "nearest significant event is selected");
    const auto heldId = states[0].encounterId;
    events[1].center = {440.0, 300.0};
    events[1].reservation = {440.0, 300.0, 105.0};
    for (int frame = 0; frame < 4; ++frame) {
        Update(actors, states, steps, events, reservations);
    }
    Expect(states[0].encounterId == heldId, "active focus does not switch every frame");

    states[0].kind = besktop::ActorEventReactionKind::Glancing;
    states[0].encounterId = 41;
    states[0].remainingSeconds = 2.0;
    states[0].lastContactSequence = 0;
    actors[0].tendency = besktop::ActorTendency::Timid;
    events.resize(1);
    events[0].result = besktop::CombatResult::HitHeavy;
    events[0].exchangeIndex = 3;
    events[0].contactOccurred = true;
    reservations.resize(1);
    Update(actors, states, steps, events, reservations);
    Expect(states[0].kind == besktop::ActorEventReactionKind::Avoiding && steps[0].changed,
        "new heavy contact upgrades Timid reaction once");
    Update(actors, states, steps, events, reservations);
    Expect(!steps[0].changed && states[0].lastContactSequence == 3,
        "same Contact does not retrigger reaction");
}

void TestLifecyclePAndMultiEventIsolation()
{
    std::vector actors{
        Actor(0, besktop::ActorTendency::Curious, 250.0, 250.0),
        Actor(1, besktop::ActorTendency::Timid, 750.0, 450.0),
    };
    std::vector events{Event(51, 400.0, 250.0), Event(52, 650.0, 450.0)};
    std::vector reservations{events[0].reservation, events[1].reservation};
    std::vector<besktop::ActorEventReactionState> states(2);
    std::vector<besktop::ActorEventReactionStep> steps(2);
    Initialize(states, 13);
    for (int frame = 0; frame < 30; ++frame) {
        Update(actors, states, steps, events, reservations);
        if (states[0].kind != besktop::ActorEventReactionKind::None &&
            states[1].kind != besktop::ActorEventReactionKind::None) break;
    }
    Expect(states[0].encounterId == 51 && states[1].encounterId == 52,
        "different actors can react to different simultaneous events");
    Expect(besktop::ActorEventReactionBlocksEncounter(states[0]) &&
        besktop::ActorEventReactionBlocksEncounter(states[1]),
        "reaction actor cannot be taken by a new encounter");

    Update(actors, states, steps, events, reservations, false, 0.1);
    Expect(states[0].kind != besktop::ActorEventReactionKind::None,
        "P off lets existing reaction finish naturally");

    events.clear();
    reservations.clear();
    Update(actors, states, steps, events, reservations, false, 0.1);
    Expect(states[0].kind == besktop::ActorEventReactionKind::Recovering &&
        states[1].kind == besktop::ActorEventReactionKind::Recovering,
        "event disappearance enters safe recovery");
    for (int frame = 0; frame < 8; ++frame) {
        Update(actors, states, steps, events, reservations, false, 0.1);
    }
    Expect(states[0].kind == besktop::ActorEventReactionKind::None &&
        states[1].kind == besktop::ActorEventReactionKind::None,
        "reaction eventually returns to wandering eligibility");

    std::vector<besktop::ActorEventReactionState> disabledStates(2);
    Initialize(disabledStates, 19);
    events = {Event(53)};
    reservations = {events[0].reservation};
    for (int frame = 0; frame < 20; ++frame) {
        Update(actors, disabledStates, steps, events, reservations, false, 0.1);
    }
    Expect(disabledStates[0].kind == besktop::ActorEventReactionKind::None &&
        disabledStates[1].kind == besktop::ActorEventReactionKind::None,
        "P off or fixed preview does not create new product reactions");
}

} // namespace

int main()
{
    TestDeterminismAndAvailability();
    TestTendencyDifferences();
    TestSafeObservationAndFallback();
    TestSelectionHysteresisAndContactDeduplication();
    TestLifecyclePAndMultiEventIsolation();
    std::cout << "actor event reaction tests passed\n";
    return 0;
}
