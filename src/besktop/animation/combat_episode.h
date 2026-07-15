#pragma once

#include "besktop/animation/actor_ecosystem.h"
#include "besktop/animation/combat_pair.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace besktop {

enum class CombatEpisodePhase {
    Inactive,
    Ready,
    ExchangeActive,
    Regrouping,
    Complete,
    Cancelled,
};

enum class CombatEpisodeFinishReason {
    None,
    PlannedExchanges,
    DecisiveResult,
    StopRequested,
    Timeout,
    Cancelled,
};

struct CombatEpisodeTuning {
    std::size_t minimumExchangeCount = 3;
    std::size_t maximumExchangeCount = 7;
    double regroupMinimumSeconds = 0.30;
    double regroupMaximumSeconds = 0.80;
    double hardTimeoutSeconds = 20.0;
};

struct CombatEpisodeState {
    CombatEpisodePhase phase = CombatEpisodePhase::Inactive;
    CombatEpisodeFinishReason finishReason = CombatEpisodeFinishReason::None;
    std::uint32_t randomState = 0xC0B47E11u;
    std::size_t plannedExchangeCount = 0;
    std::size_t completedExchangeCount = 0;
    std::size_t currentAttackerRole = 0;
    std::size_t currentDefenderRole = 1;
    std::size_t consecutiveAttacks = 0;
    std::array<std::size_t, 2> attackCounts{};
    std::array<ActorTendency, 2> tendencies{ActorTendency::Calm, ActorTendency::Calm};
    CombatScenarioId currentScenario = CombatScenarioId::None;
    CombatScenarioId previousScenario = CombatScenarioId::None;
    CombatPairState pair{};
    double elapsedSeconds = 0.0;
    double regroupRemainingSeconds = 0.0;
    std::array<double, 2> stamina{1.0, 1.0};
    std::array<double, 2> pressure{};
    std::array<double, 2> initiative{};
    CombatResult lastResult = CombatResult::None;
    CombatResult finalResult = CombatResult::None;
    bool finishRequested = false;
    bool finishAfterRegroup = false;
    bool pairCompletionConsumed = false;
    bool completionEmitted = false;
    bool completed = false;
    bool timedOut = false;
    bool cancelled = false;
};

struct CombatEpisodeReadiness {
    bool actorsValid = true;
    bool readyForExchange = true;
    bool pairRecovered = false;
    CombatResult pairResult = CombatResult::None;
    bool finishRequested = false;
};

struct CombatEpisodeStep {
    bool startExchange = false;
    bool exchangeCompleted = false;
    bool episodeCompleted = false;
    bool cancelled = false;
    std::size_t exchangeIndex = 0;
    std::size_t attackerRole = 0;
    CombatScenarioId scenario = CombatScenarioId::None;
    CombatResult exchangeResult = CombatResult::None;
    CombatEpisodeFinishReason finishReason = CombatEpisodeFinishReason::None;
};

const CombatEpisodeTuning& GetCombatEpisodeTuning();
std::wstring_view CombatEpisodePhaseName(CombatEpisodePhase phase);
std::wstring_view CombatEpisodeFinishReasonName(CombatEpisodeFinishReason reason);

void BeginCombatEpisode(
    CombatEpisodeState& state,
    std::uint32_t seed,
    ActorTendency firstTendency,
    ActorTendency secondTendency,
    bool firstActorAttacks,
    CombatScenarioId firstScenario = CombatScenarioId::None);

CombatEpisodeStep UpdateCombatEpisode(
    CombatEpisodeState& state,
    const CombatEpisodeReadiness& readiness,
    double deltaSeconds);

void CancelCombatEpisode(CombatEpisodeState& state);

} // namespace besktop
