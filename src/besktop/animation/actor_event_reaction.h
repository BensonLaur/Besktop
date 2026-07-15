#pragma once

#include "besktop/animation/actor_ecosystem.h"
#include "besktop/animation/combat_pair.h"
#include "besktop/animation/encounter_director.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string_view>
#include <vector>

namespace besktop {

inline constexpr std::uint64_t kNoReactionEncounterId = 0;

enum class ActorEventReactionKind {
    None,
    Glancing,
    Observing,
    Avoiding,
    Recovering,
};

enum class ActorEventReactionFinishReason {
    None,
    Completed,
    EventEnded,
    EventCancelled,
    UnsafeObservationPoint,
};

struct EncounterEventSnapshot {
    std::uint64_t encounterId = kNoReactionEncounterId;
    EncounterPoint center{};
    EncounterReservation reservation{};
    EncounterPhase phase = EncounterPhase::Inactive;
    bool combatEpisodeActive = false;
    std::size_t exchangeIndex = 0;
    bool contactOccurred = false;
    CombatResult result = CombatResult::None;
    double salience = 0.0;
    bool ending = false;
    bool cancelled = false;
};

struct ActorEventReactionInput {
    std::size_t actorIndex = kNoEncounterActor;
    double x = 0.0;
    double y = 0.0;
    double velocityX = 0.0;
    double velocityY = 0.0;
    double facingX = 1.0;
    double facingY = 0.0;
    double extent = 48.0;
    ActorTendency tendency = ActorTendency::Calm;
    double alertness = 0.0;
    double agitation = 0.0;
    bool awake = false;
    bool wandering = false;
    bool turning = false;
    bool actionActive = false;
    bool recovering = false;
    bool controlled = false;
};

struct ActorEventReactionState {
    ActorEventReactionKind kind = ActorEventReactionKind::None;
    std::uint64_t encounterId = kNoReactionEncounterId;
    EncounterPoint eventCenter{};
    EncounterPoint target{};
    double elapsedSeconds = 0.0;
    double remainingSeconds = 0.0;
    double intensity = 0.0;
    double reselectCooldownRemaining = 0.0;
    double individualCooldownRemaining = 0.0;
    std::size_t lastContactSequence = 0;
    bool reactedToCurrentEvent = false;
    bool targetValid = false;
    bool keepMoving = false;
    ActorEventReactionFinishReason finishReason = ActorEventReactionFinishReason::None;
    std::uint32_t randomState = 1;
};

struct ActorEventReactionStep {
    bool started = false;
    bool changed = false;
    bool finished = false;
    bool targetChanged = false;
    bool unsafeObservationPointRejected = false;
    ActorEventReactionFinishReason finishReason = ActorEventReactionFinishReason::None;
};

struct ActorEventReactionBatchStats {
    std::size_t activeCount = 0;
    std::size_t glancingCount = 0;
    std::size_t observingCount = 0;
    std::size_t avoidingCount = 0;
    std::size_t unsafeObservationPointRejectionCount = 0;
};

std::wstring_view ActorEventReactionKindName(ActorEventReactionKind kind);
std::wstring_view ActorEventReactionFinishReasonName(ActorEventReactionFinishReason reason);

void InitializeActorEventReactionState(
    ActorEventReactionState& state,
    std::uint32_t stableSeed);

double ComputeEncounterEventSalience(
    EncounterPhase phase,
    bool combatEpisodeActive,
    CombatResult result,
    bool contactOccurred);

bool IsReactionObservationPointSafe(
    const EncounterPoint& point,
    double actorMargin,
    std::span<const EncounterReservation> reservations,
    const EncounterBounds& bounds);

ActorEventReactionBatchStats UpdateActorEventReactions(
    std::span<const ActorEventReactionInput> actors,
    std::span<ActorEventReactionState> states,
    std::span<ActorEventReactionStep> steps,
    std::span<const EncounterEventSnapshot> events,
    std::span<const EncounterReservation> reservations,
    const EncounterBounds& bounds,
    bool allowNewReactions,
    double deltaSeconds);

bool ActorEventReactionBlocksEncounter(const ActorEventReactionState& state);

} // namespace besktop
