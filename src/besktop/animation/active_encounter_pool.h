#pragma once

#include "besktop/animation/actor_ecosystem.h"
#include "besktop/animation/combat_director.h"
#include "besktop/animation/encounter_arbiter.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace besktop {

inline constexpr std::uint64_t kNoActiveEncounterId = 0;

struct ActiveEncounterActorInput {
    std::size_t actorIndex = kNoEncounterActor;
    double x = 0.0;
    double y = 0.0;
    double extent = 48.0;
    double assessDurationAdjustment = 0.0;
};

struct ActiveEncounter {
    std::uint64_t id = kNoActiveEncounterId;
    std::size_t attackerIndex = kNoEncounterActor;
    std::size_t defenderIndex = kNoEncounterActor;
    CombatScenarioId scenario = CombatScenarioId::None;
    EncounterReservation reservation{};
    EncounterState encounter{};
    CombatPairState combatPair{};
    double stationLeftX = 0.0;
    double stationRightX = 0.0;
    double stationY = 0.0;
    EncounterPhase loggedEncounterPhase = EncounterPhase::Inactive;
    CombatPairPhase loggedCombatPhase = CombatPairPhase::Inactive;
    bool finishingNaturally = false;
    bool released = false;
    bool abnormalRelease = false;
};

struct ActiveEncounterPoolStats {
    std::size_t acceptedRequestCount = 0;
    std::size_t occupiedRejectionCount = 0;
    std::size_t reservationRejectionCount = 0;
    std::size_t completedEncounterCount = 0;
    std::size_t abnormalReleaseCount = 0;
};

struct ActiveEncounterPoolStartResult {
    std::vector<std::uint64_t> startedIds;
    std::size_t occupiedRejectionCount = 0;
    std::size_t reservationRejectionCount = 0;
};

struct ActiveEncounterPoolState {
    std::vector<ActiveEncounter> encounters;
    std::vector<std::uint64_t> actorEncounterIds;
    std::uint64_t nextEncounterId = 1;
    std::uint32_t randomState = 0xA17EC7E1u;
    std::size_t scenarioCursor = 0;
    double openingWanderRemaining = 0.0;
    bool desiredEnabled = false;
    bool previewSuspended = false;
    ActiveEncounterPoolStats stats{};
};

void InitializeActiveEncounterPool(
    ActiveEncounterPoolState& state,
    std::size_t actorCount,
    bool enabled,
    std::uint32_t seed = 0xA17EC7E1u);

void ResetActiveEncounterPool(ActiveEncounterPoolState& state, std::size_t actorCount);
void UpdateActiveEncounterPoolClock(
    ActiveEncounterPoolState& state,
    double deltaSeconds,
    bool ecosystemReady);
void SetActiveEncounterPoolEnabled(ActiveEncounterPoolState& state, bool enabled);
void SetActiveEncounterPoolPreviewSuspended(ActiveEncounterPoolState& state, bool suspended);
bool ActiveEncounterPoolMayAccept(const ActiveEncounterPoolState& state);

ActiveEncounterPoolStartResult StartAcceptedEncounters(
    ActiveEncounterPoolState& state,
    std::span<const LocalEncounterRequest> acceptedRequests,
    std::span<const ActiveEncounterActorInput> actors,
    const EncounterBounds& bounds);
void RecordActiveEncounterArbitrationRejections(
    ActiveEncounterPoolState& state,
    std::span<const EncounterRejectedRequest> rejectedRequests);

ActiveEncounter* FindActiveEncounter(ActiveEncounterPoolState& state, std::uint64_t encounterId);
const ActiveEncounter* FindActiveEncounter(const ActiveEncounterPoolState& state, std::uint64_t encounterId);
bool ActiveEncounterPoolOwnsActor(const ActiveEncounterPoolState& state, std::size_t actorIndex);
std::uint64_t ActiveEncounterIdForActor(const ActiveEncounterPoolState& state, std::size_t actorIndex);
std::vector<EncounterReservation> ActiveEncounterReservations(const ActiveEncounterPoolState& state);

bool ReleaseActiveEncounter(
    ActiveEncounterPoolState& state,
    std::uint64_t encounterId,
    bool abnormalRelease);
void CleanupReleasedEncounters(ActiveEncounterPoolState& state);

bool IsInsideAnyActiveEncounterReservation(
    const ActiveEncounterPoolState& state,
    double x,
    double y,
    double margin = 0.0);
CombatAvoidanceDecision ComputeActiveEncounterAvoidanceTarget(
    const ActiveEncounterPoolState& state,
    const CombatDirectorBounds& bounds,
    const CombatAvoidanceRequest& request);

} // namespace besktop
