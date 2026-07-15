#pragma once

#include "besktop/animation/actor_ecosystem.h"

#include <cstddef>
#include <span>
#include <vector>

namespace besktop {

enum class EncounterRejectionReason {
    InvalidRequest,
    ActorUnavailable,
    ReservationOutOfBounds,
    ActorConflict,
    ReservationConflict,
};

struct EncounterArbiterActor {
    std::size_t actorIndex = 0;
    bool controlled = false;
};

struct EncounterRejectedRequest {
    LocalEncounterRequest request{};
    EncounterRejectionReason reason = EncounterRejectionReason::InvalidRequest;
};

struct EncounterArbitrationResult {
    std::vector<LocalEncounterRequest> accepted;
    std::vector<EncounterRejectedRequest> rejected;
};

EncounterArbitrationResult ArbitrateEncounterRequests(
    std::span<const LocalEncounterRequest> requests,
    std::span<const EncounterArbiterActor> actors,
    const EncounterBounds& bounds,
    std::span<const EncounterReservation> activeReservations = {});

} // namespace besktop
