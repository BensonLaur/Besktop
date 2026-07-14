#include "besktop/animation/combat_director.h"

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

double NextUnit(std::uint32_t& state)
{
    state = state * 1664525u + 1013904223u;
    return static_cast<double>(state & 0x00FFFFFFu) / 16777215.0;
}

bool ReservationFits(
    const besktop::CombatDirectorBounds& bounds,
    double centerX,
    double centerY,
    double radius)
{
    return bounds.right > bounds.left && bounds.bottom > bounds.top &&
        centerX - radius >= bounds.left && centerX + radius <= bounds.right &&
        centerY - radius >= bounds.top && centerY + radius <= bounds.bottom;
}

double ClampUnit(double value)
{
    return std::clamp(value, 0.0, 1.0);
}

double PointSegmentDistance(
    double pointX,
    double pointY,
    double startX,
    double startY,
    double endX,
    double endY)
{
    const double dx = endX - startX;
    const double dy = endY - startY;
    const double lengthSquared = dx * dx + dy * dy;
    if (lengthSquared <= 1e-9) return std::hypot(pointX - startX, pointY - startY);
    const double t = ClampUnit(((pointX - startX) * dx + (pointY - startY) * dy) / lengthSquared);
    return std::hypot(pointX - (startX + dx * t), pointY - (startY + dy * t));
}

bool IsRecentPair(
    const besktop::CombatDirectorState& state,
    std::size_t first,
    std::size_t second)
{
    const auto pair = std::minmax(first, second);
    for (std::size_t index = 0; index < state.recentPairCount; ++index) {
        if (state.recentPairs[index].first == pair.first && state.recentPairs[index].second == pair.second) {
            return true;
        }
    }
    return false;
}

} // namespace

namespace besktop {

const CombatDirectorTuning& GetCombatDirectorTuning()
{
    static const CombatDirectorTuning tuning{};
    return tuning;
}

void InitializeCombatDirector(
    CombatDirectorState& state,
    bool enabled,
    std::size_t actorCount,
    std::uint32_t seed)
{
    state = {};
    state.phase = enabled ? CombatDirectorPhase::Idle : CombatDirectorPhase::Disabled;
    state.retryRemaining = 0.0;
    state.openingWanderRemaining = enabled ? GetCombatDirectorTuning().openingWanderSeconds : 0.0;
    state.randomState = seed == 0 ? 0xC001D00Du : seed;
    state.scenarioCursor = static_cast<std::size_t>(state.randomState % kScenarios.size());
    state.actorCooldowns.assign(actorCount, 0.0);
    state.actorParticipationCounts.assign(actorCount, 0u);
}

CombatDirectorSelection UpdateCombatDirector(
    CombatDirectorState& state,
    std::span<const CombatDirectorCandidate> candidates,
    const CombatDirectorBounds& bounds,
    double deltaSeconds)
{
    CombatDirectorSelection selection;
    if (state.phase == CombatDirectorPhase::Disabled || !std::isfinite(deltaSeconds) || deltaSeconds < 0.0) {
        return selection;
    }

    const double delta = std::min(deltaSeconds, 60.0);
    for (double& cooldown : state.actorCooldowns) {
        cooldown = std::max(0.0, cooldown - delta);
    }
    state.globalCooldownRemaining = std::max(0.0, state.globalCooldownRemaining - delta);

    if (state.phase == CombatDirectorPhase::Active) return selection;
    if (state.phase == CombatDirectorPhase::Cooldown) {
        if (state.globalCooldownRemaining > 0.0) return selection;
        state.phase = CombatDirectorPhase::Idle;
        state.retryRemaining = GetCombatDirectorTuning().retryMinimumSeconds;
    }

    std::size_t awakeCount = 0;
    for (const CombatDirectorCandidate& candidate : candidates) {
        if (candidate.awake) ++awakeCount;
    }
    if (state.completedInteractionCount == 0 && awakeCount < candidates.size()) return selection;
    if (state.openingWanderRemaining > 0.0) {
        state.openingWanderRemaining = std::max(0.0, state.openingWanderRemaining - delta);
        if (state.openingWanderRemaining > 0.0) return selection;
    }
    state.retryRemaining = std::max(0.0, state.retryRemaining - delta);
    if (state.retryRemaining > 0.0) return selection;

    double bestScore = std::numeric_limits<double>::infinity();
    const CombatDirectorCandidate* bestA = nullptr;
    const CombatDirectorCandidate* bestB = nullptr;
    CombatReservation bestReservation;
    for (std::size_t a = 0; a < candidates.size(); ++a) {
        const CombatDirectorCandidate& first = candidates[a];
        if (first.actorIndex >= state.actorCooldowns.size() || state.actorCooldowns[first.actorIndex] > 0.0 ||
            !first.awake || !first.wandering || first.turning || first.actionActive) continue;
        ++selection.eligibleActorCount;
        for (std::size_t b = a + 1; b < candidates.size(); ++b) {
            const CombatDirectorCandidate& second = candidates[b];
            if (second.actorIndex >= state.actorCooldowns.size() || state.actorCooldowns[second.actorIndex] > 0.0 ||
                !second.awake || !second.wandering || second.turning || second.actionActive ||
                first.actorIndex == second.actorIndex) continue;
            const double extent = std::max({36.0, first.extent, second.extent});
            const double distance = std::hypot(second.x - first.x, second.y - first.y);
            if (distance < extent * 1.8 || distance > extent * 7.5) continue;
            const double centerX = (first.x + second.x) * 0.5;
            const double centerY = (first.y + second.y) * 0.5;
            const double radius = extent * 2.35;
            if (!ReservationFits(bounds, centerX, centerY, radius)) {
                ++selection.spaceRejectedCount;
                continue;
            }
            ++selection.eligiblePairCount;
            const double preferredDistance = extent * 4.0;
            const unsigned int firstCount = first.actorIndex < state.actorParticipationCounts.size() ?
                state.actorParticipationCounts[first.actorIndex] : 0u;
            const unsigned int secondCount = second.actorIndex < state.actorParticipationCounts.size() ?
                state.actorParticipationCounts[second.actorIndex] : 0u;
            const double participationPenalty = static_cast<double>(firstCount + secondCount) * extent * 0.95;
            const double repeatPairPenalty = IsRecentPair(state, first.actorIndex, second.actorIndex) ? extent * 6.0 : 0.0;
            const double edgeClearance = std::min({
                centerX - radius - bounds.left,
                bounds.right - centerX - radius,
                centerY - radius - bounds.top,
                bounds.bottom - centerY - radius,
            });
            const double edgePenalty = edgeClearance < extent ? (extent - edgeClearance) * 0.65 : 0.0;
            const double score = std::abs(distance - preferredDistance) + participationPenalty +
                repeatPairPenalty + edgePenalty + NextUnit(state.randomState) * extent * 0.08;
            if (score < bestScore) {
                bestScore = score;
                bestA = &first;
                bestB = &second;
                bestReservation = {true, centerX, centerY, radius};
            }
        }
    }

    if (bestA == nullptr || bestB == nullptr) {
        state.spaceRejectedTotal += selection.spaceRejectedCount;
        const CombatDirectorTuning& tuning = GetCombatDirectorTuning();
        state.retryRemaining = tuning.retryMinimumSeconds +
            NextUnit(state.randomState) * (tuning.retryMaximumSeconds - tuning.retryMinimumSeconds);
        return selection;
    }

    // Keep the attacker on the left station in this first controlled version.
    // That avoids crossing paths while CombatPair's shared contact geometry
    // continues to use its established right-facing attacker contract.
    const bool firstIsLeft = bestA->x <= bestB->x;
    state.attackerIndex = firstIsLeft ? bestA->actorIndex : bestB->actorIndex;
    state.defenderIndex = firstIsLeft ? bestB->actorIndex : bestA->actorIndex;
    state.scenario = kScenarios[state.scenarioCursor % kScenarios.size()];
    state.scenarioCursor = (state.scenarioCursor + 1) % kScenarios.size();
    state.reservation = bestReservation;
    state.resultHoldRemaining = GetCombatDirectorTuning().resultHoldSeconds;
    state.phase = CombatDirectorPhase::Active;
    selection.started = true;
    selection.attackerIndex = state.attackerIndex;
    selection.defenderIndex = state.defenderIndex;
    selection.scenario = state.scenario;
    selection.reservation = state.reservation;
    return selection;
}

void CompleteCombatDirectorInteraction(CombatDirectorState& state)
{
    if (state.phase != CombatDirectorPhase::Active) return;
    const CombatDirectorTuning& tuning = GetCombatDirectorTuning();
    const double actorCooldown = tuning.actorCooldownSeconds +
        (state.actorCooldowns.size() <= 6 ? tuning.sparseActorCooldownBonusSeconds : 0.0);
    if (state.attackerIndex < state.actorCooldowns.size()) state.actorCooldowns[state.attackerIndex] = actorCooldown;
    if (state.defenderIndex < state.actorCooldowns.size()) state.actorCooldowns[state.defenderIndex] = actorCooldown;
    if (state.attackerIndex < state.actorParticipationCounts.size()) ++state.actorParticipationCounts[state.attackerIndex];
    if (state.defenderIndex < state.actorParticipationCounts.size()) ++state.actorParticipationCounts[state.defenderIndex];
    const auto pair = std::minmax(state.attackerIndex, state.defenderIndex);
    state.recentPairs[state.recentPairCursor] = {pair.first, pair.second};
    state.recentPairCursor = (state.recentPairCursor + 1) % state.recentPairs.size();
    state.recentPairCount = std::min(state.recentPairCount + 1, state.recentPairs.size());
    ++state.completedInteractionCount;
    state.reservation = {};
    state.scenario = CombatScenarioId::None;
    state.globalCooldownRemaining = tuning.globalCooldownMinimumSeconds +
        NextUnit(state.randomState) *
            (tuning.globalCooldownMaximumSeconds - tuning.globalCooldownMinimumSeconds);
    if (state.actorCooldowns.size() <= 6) {
        state.globalCooldownRemaining += tuning.sparseActorCooldownBonusSeconds;
    }
    state.phase = CombatDirectorPhase::Cooldown;
}

bool AdvanceCombatDirectorResultHold(CombatDirectorState& state, double deltaSeconds)
{
    if (state.phase != CombatDirectorPhase::Active || !std::isfinite(deltaSeconds) || deltaSeconds < 0.0) {
        return false;
    }
    state.resultHoldRemaining = std::max(0.0, state.resultHoldRemaining - std::min(deltaSeconds, 1.0));
    return state.resultHoldRemaining <= 0.0;
}

bool CombatDirectorOwnsActor(const CombatDirectorState& state, std::size_t actorIndex)
{
    return state.phase == CombatDirectorPhase::Active &&
        (state.attackerIndex == actorIndex || state.defenderIndex == actorIndex);
}

bool IsInsideCombatReservation(const CombatReservation& reservation, double x, double y, double margin)
{
    return reservation.active &&
        std::hypot(x - reservation.centerX, y - reservation.centerY) < reservation.radius + std::max(0.0, margin);
}

CombatAvoidanceDecision ComputeCombatAvoidanceTarget(
    const CombatReservation& reservation,
    const CombatDirectorBounds& bounds,
    const CombatAvoidanceRequest& request)
{
    CombatAvoidanceDecision decision{false, request.targetX, request.targetY};
    if (!reservation.active || bounds.right <= bounds.left || bounds.bottom <= bounds.top) return decision;

    const CombatDirectorTuning& tuning = GetCombatDirectorTuning();
    const double margin = std::max(0.0, request.actorMargin);
    const double hardRadius = reservation.radius + margin;
    const double softRadius = hardRadius + reservation.radius * tuning.avoidanceHysteresisScale;
    const double positionDistance = std::hypot(request.x - reservation.centerX, request.y - reservation.centerY);
    const bool targetInside = IsInsideCombatReservation(
        reservation, request.targetX, request.targetY, margin);
    const bool pathCrosses = PointSegmentDistance(
        reservation.centerX, reservation.centerY,
        request.x, request.y, request.targetX, request.targetY) < hardRadius;
    const bool nearBoundary = positionDistance < softRadius;
    const double moveX = request.targetX - request.x;
    const double moveY = request.targetY - request.y;
    const double centerX = reservation.centerX - request.x;
    const double centerY = reservation.centerY - request.y;
    const bool movingTowardReservation = moveX * centerX + moveY * centerY > 0.0;
    if (!targetInside && !pathCrosses && !(nearBoundary && movingTowardReservation)) return decision;
    if (!targetInside && request.replanCooldownRemaining > 0.0) return decision;

    double radialX = request.x - reservation.centerX;
    double radialY = request.y - reservation.centerY;
    double radialLength = std::hypot(radialX, radialY);
    if (radialLength <= 1e-6) {
        radialX = (request.actorIndex & 1u) == 0u ? 1.0 : -1.0;
        radialY = 0.0;
        radialLength = 1.0;
    }
    radialX /= radialLength;
    radialY /= radialLength;
    const double side = ((request.actorIndex + static_cast<std::size_t>(reservation.centerX + reservation.centerY)) & 1u) == 0u ? 1.0 : -1.0;
    const double tangentX = -radialY * side;
    const double tangentY = radialX * side;
    const double targetRadius = softRadius + std::max(8.0, margin * 0.35);
    const double tangentTravel = reservation.radius * (nearBoundary ? 0.72 : 0.48);
    const double unclampedX = reservation.centerX + radialX * targetRadius + tangentX * tangentTravel;
    const double unclampedY = reservation.centerY + radialY * targetRadius + tangentY * tangentTravel;
    decision.targetX = std::clamp(unclampedX, bounds.left + margin, bounds.right - margin);
    decision.targetY = std::clamp(unclampedY, bounds.top + margin, bounds.bottom - margin);

    if (IsInsideCombatReservation(reservation, decision.targetX, decision.targetY, margin)) {
        decision.targetX = std::clamp(
            reservation.centerX + radialX * (hardRadius + 1.0),
            bounds.left + margin, bounds.right - margin);
        decision.targetY = std::clamp(
            reservation.centerY + radialY * (hardRadius + 1.0),
            bounds.top + margin, bounds.bottom - margin);
    }
    decision.reselectTarget = true;
    return decision;
}

} // namespace besktop
