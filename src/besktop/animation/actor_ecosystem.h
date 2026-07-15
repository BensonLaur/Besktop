#pragma once

#include "besktop/animation/encounter_director.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string_view>
#include <vector>

namespace besktop {

inline constexpr std::size_t kNoEncounterActor = std::numeric_limits<std::size_t>::max();

enum class ActorTendency {
    Bold,
    Timid,
    Curious,
    Calm,
    Energetic,
};

enum class LocalIntent {
    Ignore,
    Avoid,
    Observe,
    Approach,
    Challenge,
    Respond,
    Yield,
};

enum class ActorEncounterOutcome {
    None,
    Combat,
    Yielded,
    CounterpartYielded,
    Bluff,
    Avoided,
};

struct ActorBehaviorProfile {
    ActorTendency tendency = ActorTendency::Calm;
    std::uint32_t stableSeed = 1;
};

struct ActorRuntimeState {
    double alertness = 0.0;
    double agitation = 0.0;
    double stamina = 1.0;
    std::size_t lastEncounterActor = kNoEncounterActor;
    ActorEncounterOutcome lastEncounterOutcome = ActorEncounterOutcome::None;
    double encounterCooldownRemaining = 0.0;
    double recentEncounterMemoryRemaining = 0.0;
    LocalIntent heldIntent = LocalIntent::Ignore;
    std::size_t intentTargetActor = kNoEncounterActor;
    double intentDecisionRemaining = 0.0;
    double intentCandidateScore = 0.0;
    std::uint32_t randomState = 1;
};

struct ActorPerceptionInput {
    std::size_t actorIndex = 0;
    double x = 0.0;
    double y = 0.0;
    double velocityX = 0.0;
    double velocityY = 0.0;
    double facingX = 1.0;
    double facingY = 0.0;
    double extent = 48.0;
    bool awake = false;
    bool wandering = false;
    bool turning = false;
    bool actionActive = false;
    bool recovering = false;
    bool controlled = false;
};

struct LocalPerception {
    bool perceived = false;
    std::size_t actorIndex = kNoEncounterActor;
    double distance = 0.0;
    double score = 0.0;
    bool naturallyApproaching = false;
    bool inView = false;
};

struct LocalEncounterRequest {
    std::size_t initiatorIndex = kNoEncounterActor;
    std::size_t responderIndex = kNoEncounterActor;
    LocalIntent initiatorIntent = LocalIntent::Ignore;
    LocalIntent responderIntent = LocalIntent::Ignore;
    EncounterIntent encounterIntent = EncounterIntent::Undecided;
    EncounterReservation reservation{};
    double priority = 0.0;
};

std::wstring_view ActorTendencyName(ActorTendency tendency);
std::wstring_view LocalIntentName(LocalIntent intent);

ActorBehaviorProfile GenerateActorBehaviorProfile(std::uint32_t stableSeed);
double ActorAssessDurationAdjustment(ActorTendency tendency);
void InitializeActorRuntimeState(ActorRuntimeState& state, std::uint32_t stableSeed);
void UpdateActorRuntimeState(ActorRuntimeState& state, double deltaSeconds);

LocalPerception FindLocalPerception(
    std::size_t selfIndex,
    std::span<const ActorPerceptionInput> actors,
    std::span<const ActorRuntimeState> runtimeStates,
    const EncounterBounds& bounds);

bool UpdateActorLocalIntent(
    ActorRuntimeState& state,
    const ActorBehaviorProfile& profile,
    const LocalPerception& perception,
    LocalIntent observedOpponentIntent,
    double deltaSeconds);

EncounterIntent ResolveLocalIntentHandshake(LocalIntent first, LocalIntent second);

std::vector<LocalEncounterRequest> BuildLocalEncounterRequests(
    std::span<const ActorPerceptionInput> actors,
    std::span<const ActorRuntimeState> runtimeStates,
    bool interactionsEnabled);

void RecordActorEncounter(
    ActorRuntimeState& state,
    std::size_t counterpartIndex,
    ActorEncounterOutcome outcome,
    double cooldownSeconds);

} // namespace besktop
