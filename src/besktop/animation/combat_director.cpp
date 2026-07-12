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

} // namespace

namespace besktop {

void InitializeCombatDirector(
    CombatDirectorState& state,
    bool enabled,
    std::size_t actorCount,
    std::uint32_t seed)
{
    state = {};
    state.phase = enabled ? CombatDirectorPhase::Idle : CombatDirectorPhase::Disabled;
    state.retryRemaining = enabled ? 1.5 : 0.0;
    state.randomState = seed == 0 ? 0xC001D00Du : seed;
    state.scenarioCursor = static_cast<std::size_t>(state.randomState % kScenarios.size());
    state.actorCooldowns.assign(actorCount, 0.0);
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
        state.retryRemaining = 0.35;
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
            if (!ReservationFits(bounds, centerX, centerY, radius)) continue;
            const double preferredDistance = extent * 4.0;
            const double score = std::abs(distance - preferredDistance) + NextUnit(state.randomState) * extent * 0.08;
            if (score < bestScore) {
                bestScore = score;
                bestA = &first;
                bestB = &second;
                bestReservation = {true, centerX, centerY, radius};
            }
        }
    }

    if (bestA == nullptr || bestB == nullptr) {
        state.retryRemaining = 0.75 + NextUnit(state.randomState) * 0.75;
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
    constexpr double actorCooldown = 10.0;
    if (state.attackerIndex < state.actorCooldowns.size()) state.actorCooldowns[state.attackerIndex] = actorCooldown;
    if (state.defenderIndex < state.actorCooldowns.size()) state.actorCooldowns[state.defenderIndex] = actorCooldown;
    state.reservation = {};
    state.scenario = CombatScenarioId::None;
    state.globalCooldownRemaining = 4.5 + NextUnit(state.randomState) * 2.5;
    state.phase = CombatDirectorPhase::Cooldown;
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

} // namespace besktop
