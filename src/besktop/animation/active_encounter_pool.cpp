#include "besktop/animation/active_encounter_pool.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace {

constexpr std::array<besktop::CombatScenarioId, 4> kScenarios{
    besktop::CombatScenarioId::LeadParry,
    besktop::CombatScenarioId::LeadSlip,
    besktop::CombatScenarioId::UppercutLightHit,
    besktop::CombatScenarioId::SideKickHeavyHit,
};

std::pair<std::size_t, std::size_t> CanonicalActors(
    const besktop::LocalEncounterRequest& request)
{
    return std::minmax(request.initiatorIndex, request.responderIndex);
}

bool ReservationsConflict(
    const besktop::EncounterReservation& first,
    const besktop::EncounterReservation& second)
{
    return std::hypot(first.centerX - second.centerX, first.centerY - second.centerY) <
        first.radius + second.radius - 1e-9;
}

bool ReservationFits(
    const besktop::EncounterReservation& reservation,
    const besktop::EncounterBounds& bounds)
{
    return std::isfinite(reservation.centerX) && std::isfinite(reservation.centerY) &&
        std::isfinite(reservation.radius) && reservation.radius > 0.0 &&
        bounds.right > bounds.left && bounds.bottom > bounds.top &&
        reservation.centerX - reservation.radius >= bounds.left &&
        reservation.centerX + reservation.radius <= bounds.right &&
        reservation.centerY - reservation.radius >= bounds.top &&
        reservation.centerY + reservation.radius <= bounds.bottom;
}

const besktop::ActiveEncounterActorInput* FindActor(
    std::span<const besktop::ActiveEncounterActorInput> actors,
    std::size_t actorIndex)
{
    const auto found = std::find_if(actors.begin(), actors.end(), [actorIndex](const auto& actor) {
        return actor.actorIndex == actorIndex;
    });
    return found == actors.end() ? nullptr : &*found;
}

} // namespace

namespace besktop {

void InitializeActiveEncounterPool(
    ActiveEncounterPoolState& state,
    std::size_t actorCount,
    bool enabled,
    std::uint32_t seed)
{
    state = {};
    state.actorEncounterIds.assign(actorCount, kNoActiveEncounterId);
    state.randomState = seed == 0 ? 0xA17EC7E1u : seed;
    state.scenarioCursor = static_cast<std::size_t>(state.randomState % kScenarios.size());
    state.desiredEnabled = enabled;
    state.openingWanderRemaining = enabled ? GetCombatDirectorTuning().openingWanderSeconds : 0.0;
}

void ResetActiveEncounterPool(ActiveEncounterPoolState& state, std::size_t actorCount)
{
    const bool enabled = state.desiredEnabled;
    const std::uint32_t seed = state.randomState;
    InitializeActiveEncounterPool(state, actorCount, enabled, seed);
}

void UpdateActiveEncounterPoolClock(
    ActiveEncounterPoolState& state,
    double deltaSeconds,
    bool ecosystemReady)
{
    if (!state.desiredEnabled || state.previewSuspended || !ecosystemReady ||
        !std::isfinite(deltaSeconds) || deltaSeconds <= 0.0) {
        return;
    }
    state.openingWanderRemaining = std::max(
        0.0,
        state.openingWanderRemaining - std::min(deltaSeconds, 1.0));
}

void SetActiveEncounterPoolEnabled(ActiveEncounterPoolState& state, bool enabled)
{
    if (state.desiredEnabled == enabled) return;
    state.desiredEnabled = enabled;
    if (enabled) {
        state.openingWanderRemaining = GetCombatDirectorTuning().resumeWanderSeconds;
    }
    for (ActiveEncounter& encounter : state.encounters) {
        if (!encounter.released) encounter.finishingNaturally = !enabled;
    }
}

void SetActiveEncounterPoolPreviewSuspended(ActiveEncounterPoolState& state, bool suspended)
{
    state.previewSuspended = suspended;
}

bool ActiveEncounterPoolMayAccept(const ActiveEncounterPoolState& state)
{
    return state.desiredEnabled && !state.previewSuspended &&
        state.openingWanderRemaining <= 0.0;
}

ActiveEncounterPoolStartResult StartAcceptedEncounters(
    ActiveEncounterPoolState& state,
    std::span<const LocalEncounterRequest> acceptedRequests,
    std::span<const ActiveEncounterActorInput> actors,
    const EncounterBounds& bounds)
{
    ActiveEncounterPoolStartResult result;
    if (!ActiveEncounterPoolMayAccept(state)) return result;

    std::vector<LocalEncounterRequest> ordered(acceptedRequests.begin(), acceptedRequests.end());
    std::sort(ordered.begin(), ordered.end(), [](const auto& first, const auto& second) {
        if (first.priority != second.priority) return first.priority > second.priority;
        const auto firstActors = CanonicalActors(first);
        const auto secondActors = CanonicalActors(second);
        if (firstActors != secondActors) return firstActors < secondActors;
        return first.initiatorIndex < second.initiatorIndex;
    });

    result.startedIds.reserve(ordered.size());
    for (const LocalEncounterRequest& request : ordered) {
        const ActiveEncounterActorInput* initiator = FindActor(actors, request.initiatorIndex);
        const ActiveEncounterActorInput* responder = FindActor(actors, request.responderIndex);
        if (initiator == nullptr || responder == nullptr ||
            request.initiatorIndex == request.responderIndex ||
            ActiveEncounterPoolOwnsActor(state, request.initiatorIndex) ||
            ActiveEncounterPoolOwnsActor(state, request.responderIndex)) {
            ++result.occupiedRejectionCount;
            continue;
        }
        if (!ReservationFits(request.reservation, bounds) ||
            std::any_of(state.encounters.begin(), state.encounters.end(), [&](const ActiveEncounter& encounter) {
                return !encounter.released && ReservationsConflict(request.reservation, encounter.reservation);
            })) {
            ++result.reservationRejectionCount;
            continue;
        }

        const bool initiatorIsLeft = initiator->x <= responder->x;
        const ActiveEncounterActorInput& attacker = initiatorIsLeft ? *initiator : *responder;
        const ActiveEncounterActorInput& defender = initiatorIsLeft ? *responder : *initiator;
        ActiveEncounter encounter;
        encounter.id = state.nextEncounterId++;
        if (encounter.id == kNoActiveEncounterId) encounter.id = state.nextEncounterId++;
        encounter.attackerIndex = attacker.actorIndex;
        encounter.defenderIndex = defender.actorIndex;
        encounter.scenario = kScenarios[state.scenarioCursor % kScenarios.size()];
        state.scenarioCursor = (state.scenarioCursor + 1) % kScenarios.size();
        encounter.reservation = request.reservation;

        const CombatPairPlan& plan = GetCombatPairPlan(encounter.scenario);
        const double largestExtent = std::max({48.0, attacker.extent, defender.extent});
        const double halfDistance = largestExtent * plan.desiredAxisDistanceScale * 0.5;
        encounter.stationLeftX = std::clamp(
            request.reservation.centerX - halfDistance,
            bounds.left + largestExtent * 1.25,
            bounds.right - largestExtent * 1.25);
        encounter.stationRightX = std::clamp(
            request.reservation.centerX + halfDistance,
            bounds.left + largestExtent * 1.25,
            bounds.right - largestExtent * 1.25);
        encounter.stationY = std::clamp(
            request.reservation.centerY,
            bounds.top + largestExtent * 1.1,
            bounds.bottom - largestExtent * 1.8);

        std::size_t leadingActor = request.initiatorIndex;
        if (request.encounterIntent == EncounterIntent::Yield) {
            leadingActor = request.initiatorIntent == LocalIntent::Yield ?
                request.initiatorIndex : request.responderIndex;
        }
        const bool attackerActsFirst = leadingActor == encounter.attackerIndex;
        const double actorMargin = std::max(28.0, largestExtent * 0.85);
        const std::uint32_t encounterSeed = state.randomState ^
            static_cast<std::uint32_t>(encounter.id * 0x9E3779B9u) ^
            static_cast<std::uint32_t>((encounter.attackerIndex + 1) * 0x85EBCA6Bu) ^
            static_cast<std::uint32_t>((encounter.defenderIndex + 1) * 0xC2B2AE35u);
        BeginEncounter(
            encounter.encounter,
            encounterSeed,
            request.reservation,
            bounds,
            actorMargin,
            request.encounterIntent,
            attackerActsFirst);
        encounter.encounter.assessDuration = std::clamp(
            encounter.encounter.assessDuration +
                (attacker.assessDurationAdjustment + defender.assessDurationAdjustment) * 0.5,
            0.52,
            1.08);
        encounter.loggedEncounterPhase = encounter.encounter.phase;

        if (encounter.attackerIndex >= state.actorEncounterIds.size() ||
            encounter.defenderIndex >= state.actorEncounterIds.size()) {
            ++result.occupiedRejectionCount;
            continue;
        }
        state.actorEncounterIds[encounter.attackerIndex] = encounter.id;
        state.actorEncounterIds[encounter.defenderIndex] = encounter.id;
        result.startedIds.push_back(encounter.id);
        state.encounters.push_back(std::move(encounter));
    }

    state.stats.acceptedRequestCount += result.startedIds.size();
    state.stats.occupiedRejectionCount += result.occupiedRejectionCount;
    state.stats.reservationRejectionCount += result.reservationRejectionCount;
    return result;
}

void RecordActiveEncounterArbitrationRejections(
    ActiveEncounterPoolState& state,
    std::span<const EncounterRejectedRequest> rejectedRequests)
{
    for (const EncounterRejectedRequest& rejected : rejectedRequests) {
        switch (rejected.reason) {
        case EncounterRejectionReason::ActorUnavailable:
        case EncounterRejectionReason::ActorConflict:
            ++state.stats.occupiedRejectionCount;
            break;
        case EncounterRejectionReason::ReservationOutOfBounds:
        case EncounterRejectionReason::ReservationConflict:
            ++state.stats.reservationRejectionCount;
            break;
        default:
            break;
        }
    }
}

ActiveEncounter* FindActiveEncounter(ActiveEncounterPoolState& state, std::uint64_t encounterId)
{
    const auto found = std::find_if(state.encounters.begin(), state.encounters.end(), [encounterId](const auto& value) {
        return value.id == encounterId && !value.released;
    });
    return found == state.encounters.end() ? nullptr : &*found;
}

const ActiveEncounter* FindActiveEncounter(const ActiveEncounterPoolState& state, std::uint64_t encounterId)
{
    const auto found = std::find_if(state.encounters.begin(), state.encounters.end(), [encounterId](const auto& value) {
        return value.id == encounterId && !value.released;
    });
    return found == state.encounters.end() ? nullptr : &*found;
}

bool ActiveEncounterPoolOwnsActor(const ActiveEncounterPoolState& state, std::size_t actorIndex)
{
    return ActiveEncounterIdForActor(state, actorIndex) != kNoActiveEncounterId;
}

std::uint64_t ActiveEncounterIdForActor(const ActiveEncounterPoolState& state, std::size_t actorIndex)
{
    return actorIndex < state.actorEncounterIds.size() ?
        state.actorEncounterIds[actorIndex] : kNoActiveEncounterId;
}

std::vector<EncounterReservation> ActiveEncounterReservations(const ActiveEncounterPoolState& state)
{
    std::vector<EncounterReservation> reservations;
    reservations.reserve(state.encounters.size());
    for (const ActiveEncounter& encounter : state.encounters) {
        if (!encounter.released) reservations.push_back(encounter.reservation);
    }
    return reservations;
}

bool ReleaseActiveEncounter(
    ActiveEncounterPoolState& state,
    std::uint64_t encounterId,
    bool abnormalRelease)
{
    auto found = std::find_if(state.encounters.begin(), state.encounters.end(), [encounterId](const auto& value) {
        return value.id == encounterId;
    });
    if (found == state.encounters.end() || found->released) return false;
    found->released = true;
    found->abnormalRelease = abnormalRelease;
    for (const std::size_t actorIndex : {found->attackerIndex, found->defenderIndex}) {
        if (actorIndex < state.actorEncounterIds.size() &&
            state.actorEncounterIds[actorIndex] == encounterId) {
            state.actorEncounterIds[actorIndex] = kNoActiveEncounterId;
        }
    }
    if (abnormalRelease) {
        ++state.stats.abnormalReleaseCount;
    } else {
        ++state.stats.completedEncounterCount;
    }
    return true;
}

void CleanupReleasedEncounters(ActiveEncounterPoolState& state)
{
    state.encounters.erase(
        std::remove_if(state.encounters.begin(), state.encounters.end(), [](const ActiveEncounter& encounter) {
            return encounter.released;
        }),
        state.encounters.end());
}

bool IsInsideAnyActiveEncounterReservation(
    const ActiveEncounterPoolState& state,
    double x,
    double y,
    double margin)
{
    return std::any_of(state.encounters.begin(), state.encounters.end(), [&](const ActiveEncounter& encounter) {
        if (encounter.released) return false;
        const CombatReservation reservation{
            true,
            encounter.reservation.centerX,
            encounter.reservation.centerY,
            encounter.reservation.radius,
        };
        return IsInsideCombatReservation(reservation, x, y, margin);
    });
}

CombatAvoidanceDecision ComputeActiveEncounterAvoidanceTarget(
    const ActiveEncounterPoolState& state,
    const CombatDirectorBounds& bounds,
    const CombatAvoidanceRequest& request)
{
    CombatAvoidanceDecision combined{false, request.targetX, request.targetY};
    if (state.encounters.empty()) return combined;

    CombatAvoidanceRequest working = request;
    const std::size_t maximumPasses = state.encounters.size() + 1;
    for (std::size_t pass = 0; pass < maximumPasses; ++pass) {
        bool changed = false;
        for (const ActiveEncounter& encounter : state.encounters) {
            if (encounter.released) continue;
            const CombatReservation reservation{
                true,
                encounter.reservation.centerX,
                encounter.reservation.centerY,
                encounter.reservation.radius,
            };
            const CombatAvoidanceDecision decision = ComputeCombatAvoidanceTarget(
                reservation, bounds, working);
            if (!decision.reselectTarget) continue;
            combined.reselectTarget = true;
            combined.targetX = decision.targetX;
            combined.targetY = decision.targetY;
            working.targetX = decision.targetX;
            working.targetY = decision.targetY;
            working.replanCooldownRemaining = 0.0;
            changed = true;
        }
        if (!changed) break;
    }
    return combined;
}

} // namespace besktop
