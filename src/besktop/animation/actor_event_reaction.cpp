#include "besktop/animation/actor_event_reaction.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace {

constexpr double kReactionReselectSeconds = 0.70;
constexpr double kReactionCooldownMinimumSeconds = 3.0;
constexpr double kReactionCooldownMaximumSeconds = 6.0;
constexpr double kReactionRecoverySeconds = 0.35;

std::uint32_t MixSeed(std::uint32_t value)
{
    value ^= value >> 16;
    value *= 0x7FEB352Du;
    value ^= value >> 15;
    value *= 0x846CA68Bu;
    value ^= value >> 16;
    return value == 0 ? 1u : value;
}

double NextUnit(std::uint32_t& state)
{
    state = state * 1664525u + 1013904223u;
    return static_cast<double>(state & 0x00FFFFFFu) / 16777215.0;
}

double ClampUnit(double value)
{
    return std::clamp(std::isfinite(value) ? value : 0.0, 0.0, 1.0);
}

bool IsAvailableForNewReaction(const besktop::ActorEventReactionInput& actor)
{
    return actor.awake && actor.wandering && !actor.turning && !actor.actionActive &&
        !actor.recovering && !actor.controlled && std::isfinite(actor.x) &&
        std::isfinite(actor.y) && std::isfinite(actor.extent) && actor.extent > 0.0;
}

const besktop::EncounterEventSnapshot* FindEvent(
    std::span<const besktop::EncounterEventSnapshot> events,
    std::uint64_t encounterId)
{
    const auto found = std::find_if(events.begin(), events.end(), [encounterId](const auto& event) {
        return event.encounterId == encounterId;
    });
    return found == events.end() ? nullptr : &*found;
}

double EventScore(
    const besktop::ActorEventReactionInput& actor,
    const besktop::EncounterEventSnapshot& event,
    bool* inView,
    double* distanceOut)
{
    const double dx = event.center.x - actor.x;
    const double dy = event.center.y - actor.y;
    const double distance = std::hypot(dx, dy);
    const double extent = std::max(24.0, actor.extent);
    const double maximumDistance = event.reservation.radius + extent * (3.0 + event.salience * 2.4);
    if (!std::isfinite(distance) || distance > maximumDistance) return -1.0;

    double viewX = actor.velocityX;
    double viewY = actor.velocityY;
    double viewLength = std::hypot(viewX, viewY);
    if (viewLength <= 1e-6) {
        viewX = actor.facingX;
        viewY = actor.facingY;
        viewLength = std::hypot(viewX, viewY);
    }
    const double viewDot = viewLength > 1e-6 && distance > 1e-6 ?
        (viewX * dx + viewY * dy) / (viewLength * distance) : 1.0;
    const bool visible = viewDot >= -0.20 ||
        (event.salience >= 0.78 && distance <= event.reservation.radius + extent * 2.6);
    if (!visible) return -1.0;

    const double distanceFactor = 1.0 - std::clamp(
        (distance - event.reservation.radius) /
            std::max(extent, maximumDistance - event.reservation.radius),
        0.0,
        1.0);
    double tendencyBias = 0.0;
    switch (actor.tendency) {
    case besktop::ActorTendency::Curious: tendencyBias = 0.26; break;
    case besktop::ActorTendency::Timid: tendencyBias = event.salience >= 0.52 ? 0.18 : -0.04; break;
    case besktop::ActorTendency::Bold: tendencyBias = 0.14; break;
    case besktop::ActorTendency::Energetic: tendencyBias = 0.09; break;
    case besktop::ActorTendency::Calm: tendencyBias = -0.24; break;
    }
    if (inView != nullptr) *inView = visible;
    if (distanceOut != nullptr) *distanceOut = distance;
    return event.salience * 1.35 + distanceFactor * 0.78 + tendencyBias +
        ClampUnit(actor.alertness) * 0.15 + ClampUnit(actor.agitation) * 0.10;
}

bool SegmentCrossesReservation(
    double startX,
    double startY,
    double endX,
    double endY,
    const besktop::EncounterReservation& reservation,
    double margin)
{
    const double dx = endX - startX;
    const double dy = endY - startY;
    const double lengthSquared = dx * dx + dy * dy;
    double t = 0.0;
    if (lengthSquared > 1e-9) {
        t = std::clamp(
            ((reservation.centerX - startX) * dx + (reservation.centerY - startY) * dy) /
                lengthSquared,
            0.0,
            1.0);
    }
    return std::hypot(
        startX + dx * t - reservation.centerX,
        startY + dy * t - reservation.centerY) < reservation.radius + margin;
}

bool FindObservationPoint(
    const besktop::ActorEventReactionInput& actor,
    const besktop::EncounterEventSnapshot& event,
    std::span<const besktop::EncounterReservation> reservations,
    std::span<const besktop::ActorEventReactionState> states,
    const besktop::EncounterBounds& bounds,
    double distanceScale,
    besktop::EncounterPoint* point)
{
    const double margin = std::max(20.0, actor.extent * 0.62);
    double radialX = actor.x - event.center.x;
    double radialY = actor.y - event.center.y;
    double radialLength = std::hypot(radialX, radialY);
    if (radialLength <= 1e-6) {
        radialX = actor.facingX == 0.0 ? 1.0 : -actor.facingX;
        radialY = 0.0;
        radialLength = std::hypot(radialX, radialY);
    }
    const double startAngle = std::atan2(radialY / radialLength, radialX / radialLength);
    const double radius = event.reservation.radius + margin * distanceScale;
    constexpr std::array<int, 12> kOffsets{0, 1, -1, 2, -2, 3, -3, 4, -4, 5, -5, 6};
    for (const int offset : kOffsets) {
        const double angle = startAngle + static_cast<double>(offset) * (std::numbers::pi / 12.0);
        const besktop::EncounterPoint candidate{
            event.center.x + std::cos(angle) * radius,
            event.center.y + std::sin(angle) * radius,
        };
        if (!besktop::IsReactionObservationPointSafe(candidate, margin, reservations, bounds)) continue;
        if (SegmentCrossesReservation(
                actor.x, actor.y, candidate.x, candidate.y, event.reservation, margin * 0.18)) {
            continue;
        }
        const bool crowded = std::any_of(states.begin(), states.end(), [&](const auto& state) {
            return state.kind == besktop::ActorEventReactionKind::Observing && state.targetValid &&
                std::hypot(candidate.x - state.target.x, candidate.y - state.target.y) < margin * 1.25;
        });
        if (crowded) continue;
        *point = candidate;
        return true;
    }
    return false;
}

besktop::EncounterPoint BuildAvoidanceTarget(
    const besktop::ActorEventReactionInput& actor,
    const besktop::EncounterEventSnapshot& event,
    const besktop::EncounterBounds& bounds)
{
    double dx = actor.x - event.center.x;
    double dy = actor.y - event.center.y;
    double length = std::hypot(dx, dy);
    if (length <= 1e-6) {
        dx = actor.facingX == 0.0 ? -1.0 : -actor.facingX;
        dy = 0.0;
        length = std::hypot(dx, dy);
    }
    const double margin = std::max(24.0, actor.extent * 0.75);
    const double targetDistance = event.reservation.radius + actor.extent * 2.1;
    return {
        std::clamp(event.center.x + dx / length * targetDistance, bounds.left + margin, bounds.right - margin),
        std::clamp(event.center.y + dy / length * targetDistance, bounds.top + margin, bounds.bottom - margin),
    };
}

void BeginRecovering(
    besktop::ActorEventReactionState& state,
    besktop::ActorEventReactionFinishReason reason,
    besktop::ActorEventReactionStep& step)
{
    state.kind = besktop::ActorEventReactionKind::Recovering;
    state.remainingSeconds = kReactionRecoverySeconds;
    state.targetValid = false;
    state.keepMoving = true;
    state.individualCooldownRemaining = std::max(
        state.individualCooldownRemaining, 2.5);
    state.finishReason = reason;
    step.changed = true;
    step.finished = true;
    step.finishReason = reason;
}

} // namespace

namespace besktop {

std::wstring_view ActorEventReactionKindName(ActorEventReactionKind kind)
{
    switch (kind) {
    case ActorEventReactionKind::Glancing: return L"Glancing";
    case ActorEventReactionKind::Observing: return L"Observing";
    case ActorEventReactionKind::Avoiding: return L"Avoiding";
    case ActorEventReactionKind::Recovering: return L"Recovering";
    default: return L"None";
    }
}

std::wstring_view ActorEventReactionFinishReasonName(ActorEventReactionFinishReason reason)
{
    switch (reason) {
    case ActorEventReactionFinishReason::Completed: return L"completed";
    case ActorEventReactionFinishReason::EventEnded: return L"event_ended";
    case ActorEventReactionFinishReason::EventCancelled: return L"event_cancelled";
    case ActorEventReactionFinishReason::UnsafeObservationPoint: return L"unsafe_observation_point";
    default: return L"none";
    }
}

void InitializeActorEventReactionState(
    ActorEventReactionState& state,
    std::uint32_t stableSeed)
{
    state = {};
    state.randomState = MixSeed(stableSeed ^ 0xE7E17A51u);
}

double ComputeEncounterEventSalience(
    EncounterPhase phase,
    bool combatEpisodeActive,
    CombatResult result,
    bool contactOccurred)
{
    double salience = 0.0;
    switch (phase) {
    case EncounterPhase::Assessing: salience = 0.22; break;
    case EncounterPhase::Intent: salience = 0.34; break;
    case EncounterPhase::Combat: salience = combatEpisodeActive ? 0.55 : 0.46; break;
    case EncounterPhase::Aftermath: salience = 0.40; break;
    case EncounterPhase::Separating: salience = 0.22; break;
    default: break;
    }
    if (contactOccurred) {
        switch (result) {
        case CombatResult::HitHeavy: salience = 1.0; break;
        case CombatResult::HitLight: salience = 0.82; break;
        case CombatResult::Blocked:
        case CombatResult::Evaded:
        case CombatResult::Whiffed: salience = 0.66; break;
        default: salience = std::max(salience, 0.58); break;
        }
    } else if (phase == EncounterPhase::Aftermath) {
        salience = std::max(salience,
            result == CombatResult::HitHeavy ? 0.72 :
            result == CombatResult::HitLight ? 0.58 : 0.44);
    }
    return salience;
}

bool IsReactionObservationPointSafe(
    const EncounterPoint& point,
    double actorMargin,
    std::span<const EncounterReservation> reservations,
    const EncounterBounds& bounds)
{
    if (!std::isfinite(point.x) || !std::isfinite(point.y) ||
        !std::isfinite(actorMargin) || actorMargin < 0.0 ||
        bounds.right <= bounds.left || bounds.bottom <= bounds.top ||
        point.x < bounds.left + actorMargin || point.x > bounds.right - actorMargin ||
        point.y < bounds.top + actorMargin || point.y > bounds.bottom - actorMargin) {
        return false;
    }
    return std::none_of(reservations.begin(), reservations.end(), [&](const auto& reservation) {
        return std::hypot(point.x - reservation.centerX, point.y - reservation.centerY) <
            reservation.radius + actorMargin * 0.18;
    });
}

ActorEventReactionBatchStats UpdateActorEventReactions(
    std::span<const ActorEventReactionInput> actors,
    std::span<ActorEventReactionState> states,
    std::span<ActorEventReactionStep> steps,
    std::span<const EncounterEventSnapshot> events,
    std::span<const EncounterReservation> reservations,
    const EncounterBounds& bounds,
    bool allowNewReactions,
    double deltaSeconds)
{
    ActorEventReactionBatchStats stats;
    if (actors.size() != states.size() || actors.size() != steps.size()) return stats;
    const double delta = std::isfinite(deltaSeconds) && deltaSeconds > 0.0 ?
        std::min(deltaSeconds, 1.0) : 0.0;
    std::fill(steps.begin(), steps.end(), ActorEventReactionStep{});

    for (std::size_t index = 0; index < actors.size(); ++index) {
        const ActorEventReactionInput& actor = actors[index];
        ActorEventReactionState& state = states[index];
        ActorEventReactionStep& step = steps[index];
        state.elapsedSeconds += state.kind == ActorEventReactionKind::None ? 0.0 : delta;
        state.reselectCooldownRemaining = std::max(0.0, state.reselectCooldownRemaining - delta);
        state.individualCooldownRemaining = std::max(0.0, state.individualCooldownRemaining - delta);

        if (state.kind == ActorEventReactionKind::Recovering) {
            state.remainingSeconds = std::max(0.0, state.remainingSeconds - delta);
            if (state.remainingSeconds <= 0.0) {
                state.kind = ActorEventReactionKind::None;
                state.encounterId = kNoReactionEncounterId;
                state.reactedToCurrentEvent = false;
                state.keepMoving = false;
                step.changed = true;
            }
            continue;
        }

        const EncounterEventSnapshot* focused = state.encounterId != kNoReactionEncounterId ?
            FindEvent(events, state.encounterId) : nullptr;
        if (state.kind != ActorEventReactionKind::None) {
            if (focused == nullptr || focused->cancelled) {
                BeginRecovering(state,
                    focused != nullptr && focused->cancelled ?
                        ActorEventReactionFinishReason::EventCancelled :
                        ActorEventReactionFinishReason::EventEnded,
                    step);
                continue;
            }
            state.eventCenter = focused->center;
            state.intensity = std::max(state.intensity * std::max(0.0, 1.0 - delta * 0.18), focused->salience);
            const bool newContact = focused->contactOccurred &&
                focused->exchangeIndex > state.lastContactSequence;
            if (newContact) {
                state.lastContactSequence = focused->exchangeIndex;
                if (focused->result == CombatResult::HitHeavy && actor.tendency == ActorTendency::Timid &&
                    state.kind != ActorEventReactionKind::Avoiding) {
                    state.kind = ActorEventReactionKind::Avoiding;
                    state.target = BuildAvoidanceTarget(actor, *focused, bounds);
                    state.targetValid = true;
                    state.keepMoving = true;
                    state.remainingSeconds = 1.5 + NextUnit(state.randomState) * 0.8;
                    step.changed = true;
                    step.targetChanged = true;
                }
            }
            const bool travellingToObservation =
                state.kind == ActorEventReactionKind::Observing && state.targetValid &&
                std::hypot(actor.x - state.target.x, actor.y - state.target.y) >
                    std::max(8.0, actor.extent * 0.22) && state.elapsedSeconds < 4.0;
            if (!travellingToObservation) {
                state.remainingSeconds = std::max(0.0, state.remainingSeconds - delta);
            }
            if (focused->ending || state.remainingSeconds <= 0.0 ||
                state.elapsedSeconds >= 6.0) {
                BeginRecovering(state,
                    focused->ending ? ActorEventReactionFinishReason::EventEnded :
                        ActorEventReactionFinishReason::Completed,
                    step);
            }
            continue;
        }

        if (!allowNewReactions || state.individualCooldownRemaining > 0.0 ||
            !IsAvailableForNewReaction(actor)) {
            continue;
        }
        if (state.reselectCooldownRemaining > 0.0) continue;
        state.reselectCooldownRemaining = 0.35 + NextUnit(state.randomState) * 0.45;

        const EncounterEventSnapshot* selected = nullptr;
        double selectedScore = -1.0;
        double selectedDistance = 0.0;
        for (const EncounterEventSnapshot& event : events) {
            if (event.encounterId == kNoReactionEncounterId || event.ending || event.cancelled ||
                event.salience <= 0.0) {
                continue;
            }
            double distance = 0.0;
            const double score = EventScore(actor, event, nullptr, &distance);
            if (score > selectedScore + 1e-9 ||
                (std::abs(score - selectedScore) <= 1e-9 && selected != nullptr &&
                    event.encounterId < selected->encounterId)) {
                selected = &event;
                selectedScore = score;
                selectedDistance = distance;
            }
        }
        if (selected == nullptr || selectedScore < 0.0) continue;

        const double extent = std::max(24.0, actor.extent);
        const double closeness = 1.0 - std::clamp(
            (selectedDistance - selected->reservation.radius) / (extent * 5.0), 0.0, 1.0);
        double probability = selected->salience * 0.52 + closeness * 0.32 +
            ClampUnit(actor.alertness) * 0.08;
        switch (actor.tendency) {
        case ActorTendency::Curious: probability += 0.28; break;
        case ActorTendency::Timid: probability += selected->salience >= 0.52 ? 0.20 : -0.08; break;
        case ActorTendency::Bold: probability += 0.14; break;
        case ActorTendency::Energetic: probability += 0.08; break;
        case ActorTendency::Calm: probability -= 0.30; break;
        }
        const std::size_t nearbyActors = static_cast<std::size_t>(std::count_if(
            actors.begin(), actors.end(), [&](const auto& other) {
                return other.actorIndex != actor.actorIndex && other.awake &&
                    std::hypot(other.x - selected->center.x, other.y - selected->center.y) <
                        selected->reservation.radius + extent * 2.4;
            }));
        probability -= std::min(0.24, static_cast<double>(nearbyActors) * 0.025);
        const std::size_t currentEventReactions = static_cast<std::size_t>(std::count_if(
            states.begin(), states.end(), [&](const auto& otherState) {
                return otherState.encounterId == selected->encounterId &&
                    otherState.kind != ActorEventReactionKind::None &&
                    otherState.kind != ActorEventReactionKind::Recovering;
            }));
        probability -= std::min(0.54, static_cast<double>(currentEventReactions) * 0.16);
        if (NextUnit(state.randomState) > std::clamp(probability, 0.04, 0.92)) continue;

        ActorEventReactionKind kind = ActorEventReactionKind::Glancing;
        if (actor.tendency == ActorTendency::Timid && selected->salience >= 0.52) {
            kind = ActorEventReactionKind::Avoiding;
        } else if (actor.tendency == ActorTendency::Curious || actor.tendency == ActorTendency::Bold) {
            kind = ActorEventReactionKind::Observing;
        } else if (actor.tendency == ActorTendency::Energetic && selected->salience >= 0.78 &&
            NextUnit(state.randomState) > 0.58) {
            kind = ActorEventReactionKind::Observing;
        }

        state.kind = kind;
        state.encounterId = selected->encounterId;
        state.eventCenter = selected->center;
        state.elapsedSeconds = 0.0;
        state.intensity = selected->salience;
        state.reselectCooldownRemaining = kReactionReselectSeconds;
        state.lastContactSequence = selected->contactOccurred ? selected->exchangeIndex : 0;
        state.reactedToCurrentEvent = true;
        state.finishReason = ActorEventReactionFinishReason::None;
        state.keepMoving = actor.tendency == ActorTendency::Energetic;
        state.targetValid = false;

        if (kind == ActorEventReactionKind::Observing) {
            const double distanceScale = actor.tendency == ActorTendency::Bold ? 1.00 : 1.35;
            if (FindObservationPoint(
                    actor, *selected, reservations, states, bounds,
                    distanceScale, &state.target)) {
                state.targetValid = true;
                state.remainingSeconds = 1.2 + NextUnit(state.randomState) * 1.8;
                step.targetChanged = true;
            } else {
                state.kind = ActorEventReactionKind::Glancing;
                state.remainingSeconds = 0.45 + NextUnit(state.randomState) * 0.55;
                state.finishReason = ActorEventReactionFinishReason::UnsafeObservationPoint;
                step.unsafeObservationPointRejected = true;
                ++stats.unsafeObservationPointRejectionCount;
            }
        } else if (kind == ActorEventReactionKind::Avoiding) {
            state.target = BuildAvoidanceTarget(actor, *selected, bounds);
            state.targetValid = true;
            state.keepMoving = true;
            state.remainingSeconds = 1.2 + NextUnit(state.randomState) * 1.1;
            step.targetChanged = true;
        } else {
            state.remainingSeconds = 0.45 + NextUnit(state.randomState) *
                (actor.tendency == ActorTendency::Calm ? 0.45 : 0.75);
        }

        state.individualCooldownRemaining = kReactionCooldownMinimumSeconds +
            NextUnit(state.randomState) *
                (kReactionCooldownMaximumSeconds - kReactionCooldownMinimumSeconds);
        step.started = true;
        step.changed = true;
    }

    for (const ActorEventReactionState& state : states) {
        if (state.kind == ActorEventReactionKind::None ||
            state.kind == ActorEventReactionKind::Recovering) {
            continue;
        }
        ++stats.activeCount;
        if (state.kind == ActorEventReactionKind::Glancing) ++stats.glancingCount;
        if (state.kind == ActorEventReactionKind::Observing) ++stats.observingCount;
        if (state.kind == ActorEventReactionKind::Avoiding) ++stats.avoidingCount;
    }
    return stats;
}

bool ActorEventReactionBlocksEncounter(const ActorEventReactionState& state)
{
    return state.kind != ActorEventReactionKind::None;
}

} // namespace besktop
