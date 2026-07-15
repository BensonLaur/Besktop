#include "besktop/animation/encounter_arbiter.h"

#include <algorithm>
#include <cmath>

namespace {

bool IsValidRequest(const besktop::LocalEncounterRequest& request)
{
    return request.initiatorIndex != besktop::kNoEncounterActor &&
        request.responderIndex != besktop::kNoEncounterActor &&
        request.initiatorIndex != request.responderIndex &&
        request.encounterIntent != besktop::EncounterIntent::Undecided &&
        std::isfinite(request.reservation.centerX) &&
        std::isfinite(request.reservation.centerY) &&
        std::isfinite(request.reservation.radius) && request.reservation.radius > 0.0 &&
        std::isfinite(request.priority);
}

bool ReservationFits(
    const besktop::EncounterReservation& reservation,
    const besktop::EncounterBounds& bounds)
{
    return bounds.right > bounds.left && bounds.bottom > bounds.top &&
        reservation.centerX - reservation.radius >= bounds.left &&
        reservation.centerX + reservation.radius <= bounds.right &&
        reservation.centerY - reservation.radius >= bounds.top &&
        reservation.centerY + reservation.radius <= bounds.bottom;
}

bool ReservationsConflict(
    const besktop::EncounterReservation& first,
    const besktop::EncounterReservation& second)
{
    return std::hypot(first.centerX - second.centerX, first.centerY - second.centerY) <
        first.radius + second.radius - 1e-9;
}

std::pair<std::size_t, std::size_t> CanonicalActors(
    const besktop::LocalEncounterRequest& request)
{
    return std::minmax(request.initiatorIndex, request.responderIndex);
}

} // namespace

namespace besktop {

EncounterArbitrationResult ArbitrateEncounterRequests(
    std::span<const LocalEncounterRequest> requests,
    std::span<const EncounterArbiterActor> actors,
    const EncounterBounds& bounds,
    std::span<const EncounterReservation> activeReservations)
{
    EncounterArbitrationResult result;
    std::vector<LocalEncounterRequest> ordered(requests.begin(), requests.end());
    std::sort(ordered.begin(), ordered.end(), [](const LocalEncounterRequest& first, const LocalEncounterRequest& second) {
        if (first.priority != second.priority) return first.priority > second.priority;
        const auto firstActors = CanonicalActors(first);
        const auto secondActors = CanonicalActors(second);
        if (firstActors != secondActors) return firstActors < secondActors;
        if (first.initiatorIndex != second.initiatorIndex) return first.initiatorIndex < second.initiatorIndex;
        if (first.encounterIntent != second.encounterIntent) {
            return static_cast<int>(first.encounterIntent) < static_cast<int>(second.encounterIntent);
        }
        if (first.reservation.centerX != second.reservation.centerX) {
            return first.reservation.centerX < second.reservation.centerX;
        }
        if (first.reservation.centerY != second.reservation.centerY) {
            return first.reservation.centerY < second.reservation.centerY;
        }
        return first.reservation.radius < second.reservation.radius;
    });

    std::vector<std::size_t> occupiedActors;
    occupiedActors.reserve(actors.size());
    for (const EncounterArbiterActor& actor : actors) {
        if (actor.controlled) occupiedActors.push_back(actor.actorIndex);
    }
    std::sort(occupiedActors.begin(), occupiedActors.end());
    occupiedActors.erase(std::unique(occupiedActors.begin(), occupiedActors.end()), occupiedActors.end());

    std::vector<EncounterReservation> occupiedReservations(activeReservations.begin(), activeReservations.end());
    result.accepted.reserve(ordered.size());
    result.rejected.reserve(ordered.size());
    for (const LocalEncounterRequest& request : ordered) {
        if (!IsValidRequest(request)) {
            result.rejected.push_back({request, EncounterRejectionReason::InvalidRequest});
            continue;
        }
        const auto actorKnownAndFree = [&](std::size_t actorIndex) {
            const auto actor = std::find_if(actors.begin(), actors.end(), [actorIndex](const EncounterArbiterActor& value) {
                return value.actorIndex == actorIndex;
            });
            return actor != actors.end() && !actor->controlled;
        };
        if (!actorKnownAndFree(request.initiatorIndex) || !actorKnownAndFree(request.responderIndex)) {
            result.rejected.push_back({request, EncounterRejectionReason::ActorUnavailable});
            continue;
        }
        if (!ReservationFits(request.reservation, bounds)) {
            result.rejected.push_back({request, EncounterRejectionReason::ReservationOutOfBounds});
            continue;
        }
        const auto isOccupied = [&](std::size_t actorIndex) {
            return std::binary_search(occupiedActors.begin(), occupiedActors.end(), actorIndex);
        };
        if (isOccupied(request.initiatorIndex) || isOccupied(request.responderIndex)) {
            result.rejected.push_back({request, EncounterRejectionReason::ActorConflict});
            continue;
        }
        if (std::any_of(occupiedReservations.begin(), occupiedReservations.end(), [&](const EncounterReservation& reservation) {
                return ReservationsConflict(request.reservation, reservation);
            })) {
            result.rejected.push_back({request, EncounterRejectionReason::ReservationConflict});
            continue;
        }

        result.accepted.push_back(request);
        occupiedActors.push_back(request.initiatorIndex);
        occupiedActors.push_back(request.responderIndex);
        std::sort(occupiedActors.begin(), occupiedActors.end());
        occupiedReservations.push_back(request.reservation);
    }
    return result;
}

} // namespace besktop
