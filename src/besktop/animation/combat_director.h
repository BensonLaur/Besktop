#pragma once

#include "besktop/animation/combat_pair.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

namespace besktop {

enum class CombatDirectorPhase {
    Disabled,
    Idle,
    Active,
    Cooldown,
};

enum class CombatDirectorModeChange {
    NoChange,
    Enabled,
    Disabled,
    DisableDeferred,
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
    std::size_t eligibleActorCount = 0;
    std::size_t eligiblePairCount = 0;
    std::size_t spaceRejectedCount = 0;
};

struct CombatDirectorTuning {
    double openingWanderSeconds = 6.0;
    double retryMinimumSeconds = 0.85;
    double retryMaximumSeconds = 1.65;
    double resultHoldSeconds = 0.45;
    double actorCooldownSeconds = 14.0;
    double globalCooldownMinimumSeconds = 7.0;
    double globalCooldownMaximumSeconds = 11.0;
    double sparseActorCooldownBonusSeconds = 4.0;
    double avoidanceReplanIntervalSeconds = 0.85;
    double avoidanceHysteresisScale = 0.28;
    double resumeWanderSeconds = 4.0;
};

struct CombatAvoidanceRequest {
    std::size_t actorIndex = 0;
    double x = 0.0;
    double y = 0.0;
    double targetX = 0.0;
    double targetY = 0.0;
    double actorMargin = 0.0;
    double replanCooldownRemaining = 0.0;
};

struct CombatAvoidanceDecision {
    bool reselectTarget = false;
    double targetX = 0.0;
    double targetY = 0.0;
};

struct CombatDirectorState {
    CombatDirectorPhase phase = CombatDirectorPhase::Disabled;
    std::size_t attackerIndex = 0;
    std::size_t defenderIndex = 0;
    CombatScenarioId scenario = CombatScenarioId::None;
    CombatReservation reservation{};
    double retryRemaining = 0.0;
    double globalCooldownRemaining = 0.0;
    double openingWanderRemaining = 0.0;
    double resultHoldRemaining = 0.0;
    std::uint32_t randomState = 0xC001D00Du;
    std::size_t scenarioCursor = 0;
    std::vector<double> actorCooldowns;
    std::vector<unsigned int> actorParticipationCounts;
    std::array<std::pair<std::size_t, std::size_t>, 4> recentPairs{};
    std::size_t recentPairCount = 0;
    std::size_t recentPairCursor = 0;
    std::size_t completedInteractionCount = 0;
    std::size_t spaceRejectedTotal = 0;
    bool desiredEnabled = false;
    bool disableAfterActive = false;
};

const CombatDirectorTuning& GetCombatDirectorTuning();

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
CombatDirectorModeChange SetCombatDirectorEnabled(CombatDirectorState& state, bool enabled);
CombatDirectorModeChange ToggleCombatDirectorEnabled(CombatDirectorState& state);
bool AdvanceCombatDirectorResultHold(CombatDirectorState& state, double deltaSeconds);
bool CombatDirectorOwnsActor(const CombatDirectorState& state, std::size_t actorIndex);
bool IsInsideCombatReservation(const CombatReservation& reservation, double x, double y, double margin = 0.0);
CombatAvoidanceDecision ComputeCombatAvoidanceTarget(
    const CombatReservation& reservation,
    const CombatDirectorBounds& bounds,
    const CombatAvoidanceRequest& request);

} // namespace besktop
