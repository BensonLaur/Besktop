#pragma once

#include "besktop/animation/combat_pair.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace besktop {

enum class CombatDirectorPhase {
    Disabled,
    Idle,
    Active,
    Cooldown,
};

struct CombatDirectorBounds {
    double left = 0.0;
    double top = 0.0;
    double right = 0.0;
    double bottom = 0.0;
};

struct CombatDirectorCandidate {
    std::size_t actorIndex = 0;
    double x = 0.0;
    double y = 0.0;
    double extent = 48.0;
    bool awake = false;
    bool wandering = false;
    bool turning = false;
    bool actionActive = false;
};

struct CombatReservation {
    bool active = false;
    double centerX = 0.0;
    double centerY = 0.0;
    double radius = 0.0;
};

struct CombatDirectorSelection {
    bool started = false;
    std::size_t attackerIndex = 0;
    std::size_t defenderIndex = 0;
    CombatScenarioId scenario = CombatScenarioId::None;
    CombatReservation reservation{};
};

struct CombatDirectorState {
    CombatDirectorPhase phase = CombatDirectorPhase::Disabled;
    std::size_t attackerIndex = 0;
    std::size_t defenderIndex = 0;
    CombatScenarioId scenario = CombatScenarioId::None;
    CombatReservation reservation{};
    double retryRemaining = 0.0;
    double globalCooldownRemaining = 0.0;
    std::uint32_t randomState = 0xC001D00Du;
    std::size_t scenarioCursor = 0;
    std::vector<double> actorCooldowns;
};

void InitializeCombatDirector(
    CombatDirectorState& state,
    bool enabled,
    std::size_t actorCount,
    std::uint32_t seed = 0xC001D00Du);

CombatDirectorSelection UpdateCombatDirector(
    CombatDirectorState& state,
    std::span<const CombatDirectorCandidate> candidates,
    const CombatDirectorBounds& bounds,
    double deltaSeconds);

void CompleteCombatDirectorInteraction(CombatDirectorState& state);
bool CombatDirectorOwnsActor(const CombatDirectorState& state, std::size_t actorIndex);
bool IsInsideCombatReservation(const CombatReservation& reservation, double x, double y, double margin = 0.0);

} // namespace besktop
