#include "besktop/animation/combat_episode.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace {

double NextUnit(std::uint32_t& state)
{
    state = state * 1664525u + 1013904223u;
    return static_cast<double>((state >> 8) & 0x00FFFFFFu) / 16777216.0;
}

std::uint32_t MixSeed(std::uint32_t seed)
{
    std::uint32_t value = seed == 0 ? 0xC0B47E11u : seed;
    value ^= value >> 16;
    value *= 0x7FEB352Du;
    value ^= value >> 15;
    value *= 0x846CA68Bu;
    value ^= value >> 16;
    return value == 0 ? 0xC0B47E11u : value;
}

std::size_t ChoosePlannedExchangeCount(std::uint32_t& state)
{
    const double roll = NextUnit(state);
    if (roll < 0.10) return 3;
    if (roll < 0.35) return 4;
    if (roll < 0.65) return 5;
    if (roll < 0.90) return 6;
    return 7;
}

double ContinueBias(besktop::ActorTendency tendency)
{
    switch (tendency) {
    case besktop::ActorTendency::Bold: return 0.16;
    case besktop::ActorTendency::Timid: return -0.18;
    case besktop::ActorTendency::Calm: return -0.08;
    case besktop::ActorTendency::Energetic: return 0.12;
    case besktop::ActorTendency::Curious: return -0.02;
    }
    return 0.0;
}

besktop::CombatScenarioId ChooseScenario(
    besktop::CombatEpisodeState& state,
    bool allowHeavy)
{
    constexpr std::array<besktop::CombatScenarioId, 3> common{
        besktop::CombatScenarioId::LeadParry,
        besktop::CombatScenarioId::LeadSlip,
        besktop::CombatScenarioId::UppercutLightHit,
    };
    for (int attempt = 0; attempt < 8; ++attempt) {
        const double roll = NextUnit(state.randomState);
        besktop::CombatScenarioId selected;
        if (allowHeavy && roll >= 0.88) {
            selected = besktop::CombatScenarioId::SideKickHeavyHit;
        } else {
            const std::size_t index = std::min(
                common.size() - 1,
                static_cast<std::size_t>(NextUnit(state.randomState) * common.size()));
            selected = common[index];
        }
        if (selected != state.previousScenario) return selected;
    }
    for (const besktop::CombatScenarioId candidate : common) {
        if (candidate != state.previousScenario) return candidate;
    }
    return besktop::CombatScenarioId::LeadParry;
}

std::size_t ChooseNextAttacker(besktop::CombatEpisodeState& state)
{
    const std::size_t previousAttacker = state.currentAttackerRole;
    const std::size_t previousDefender = 1 - previousAttacker;
    if (state.completedExchangeCount + 1 >= state.plannedExchangeCount) {
        if (state.attackCounts[0] == 0) return 0;
        if (state.attackCounts[1] == 0) return 1;
    }
    if (state.consecutiveAttacks >= 2) return previousDefender;
    if (state.lastResult == besktop::CombatResult::Blocked ||
        state.lastResult == besktop::CombatResult::Evaded ||
        state.lastResult == besktop::CombatResult::Whiffed) {
        return previousDefender;
    }

    double defenderChance = 0.52;
    if (state.lastResult == besktop::CombatResult::HitLight) defenderChance = 0.62;
    if (state.lastResult == besktop::CombatResult::HitHeavy) defenderChance = 0.76;
    defenderChance += state.initiative[previousDefender] * 0.12;
    defenderChance -= state.pressure[previousDefender] * 0.08;
    defenderChance += ContinueBias(state.tendencies[previousDefender]) * 0.30;
    defenderChance = std::clamp(defenderChance, 0.22, 0.90);
    return NextUnit(state.randomState) < defenderChance ? previousDefender : previousAttacker;
}

void ApplyExchangeResult(besktop::CombatEpisodeState& state, besktop::CombatResult result)
{
    const std::size_t attacker = state.currentAttackerRole;
    const std::size_t defender = state.currentDefenderRole;
    state.lastResult = result;
    if (result != besktop::CombatResult::None) state.finalResult = result;
    state.initiative[0] *= 0.62;
    state.initiative[1] *= 0.62;
    state.pressure[0] *= 0.82;
    state.pressure[1] *= 0.82;
    switch (result) {
    case besktop::CombatResult::Blocked:
        state.initiative[defender] += 0.48;
        state.pressure[attacker] += 0.16;
        break;
    case besktop::CombatResult::Evaded:
        state.initiative[defender] += 0.62;
        state.pressure[attacker] += 0.24;
        break;
    case besktop::CombatResult::Whiffed:
        state.initiative[defender] += 0.58;
        state.pressure[attacker] += 0.32;
        state.stamina[attacker] = std::max(0.0, state.stamina[attacker] - 0.07);
        break;
    case besktop::CombatResult::HitLight:
        state.stamina[defender] = std::max(0.0, state.stamina[defender] - 0.18);
        state.pressure[defender] += 0.30;
        state.initiative[attacker] += 0.18;
        break;
    case besktop::CombatResult::HitHeavy:
        state.stamina[defender] = std::max(0.0, state.stamina[defender] - 0.42);
        state.pressure[defender] += 0.58;
        state.initiative[attacker] += 0.26;
        break;
    default:
        break;
    }
    state.stamina[attacker] = std::max(0.0, state.stamina[attacker] - 0.05);
}

bool ShouldFinishNaturally(besktop::CombatEpisodeState& state)
{
    const auto& tuning = besktop::GetCombatEpisodeTuning();
    if (state.completedExchangeCount >= state.plannedExchangeCount) return true;
    if (state.completedExchangeCount < tuning.minimumExchangeCount) return false;

    double finishChance = 0.05;
    if (state.lastResult == besktop::CombatResult::HitHeavy) finishChance += 0.72;
    if (state.lastResult == besktop::CombatResult::HitLight) finishChance += 0.13;
    if (state.lastResult == besktop::CombatResult::Blocked) finishChance += 0.05;
    if (state.lastResult == besktop::CombatResult::Evaded ||
        state.lastResult == besktop::CombatResult::Whiffed) finishChance += 0.03;
    if (state.stamina[0] < 0.45 && state.stamina[1] < 0.45) finishChance += 0.22;
    const std::size_t pressured = state.currentDefenderRole;
    finishChance -= ContinueBias(state.tendencies[pressured]);
    finishChance += state.pressure[pressured] * 0.10;
    return NextUnit(state.randomState) < std::clamp(finishChance, 0.02, 0.92);
}

void PrepareNextExchange(besktop::CombatEpisodeState& state, bool first)
{
    if (!first) {
        const std::size_t next = ChooseNextAttacker(state);
        state.consecutiveAttacks = next == state.currentAttackerRole ?
            state.consecutiveAttacks + 1 : 1;
        state.currentAttackerRole = next;
        state.currentDefenderRole = 1 - next;
    } else {
        state.consecutiveAttacks = 1;
    }
    ++state.attackCounts[state.currentAttackerRole];
    state.previousScenario = state.currentScenario;
    if (first && state.currentScenario != besktop::CombatScenarioId::None) {
        // The pool's stable four-scene rotation remains the first exchange.
    } else {
        state.currentScenario = ChooseScenario(state, state.completedExchangeCount >= 2);
    }
    state.pair = {};
    state.pairCompletionConsumed = false;
}

} // namespace

namespace besktop {

const CombatEpisodeTuning& GetCombatEpisodeTuning()
{
    static const CombatEpisodeTuning tuning{};
    return tuning;
}

std::wstring_view CombatEpisodePhaseName(CombatEpisodePhase phase)
{
    switch (phase) {
    case CombatEpisodePhase::Ready: return L"Ready";
    case CombatEpisodePhase::ExchangeActive: return L"ExchangeActive";
    case CombatEpisodePhase::Regrouping: return L"Regrouping";
    case CombatEpisodePhase::Complete: return L"Complete";
    case CombatEpisodePhase::Cancelled: return L"Cancelled";
    default: return L"Inactive";
    }
}

std::wstring_view CombatEpisodeFinishReasonName(CombatEpisodeFinishReason reason)
{
    switch (reason) {
    case CombatEpisodeFinishReason::PlannedExchanges: return L"planned_exchanges";
    case CombatEpisodeFinishReason::DecisiveResult: return L"decisive_result";
    case CombatEpisodeFinishReason::StopRequested: return L"stop_requested";
    case CombatEpisodeFinishReason::Timeout: return L"timeout";
    case CombatEpisodeFinishReason::Cancelled: return L"cancelled";
    default: return L"none";
    }
}

void BeginCombatEpisode(
    CombatEpisodeState& state,
    std::uint32_t seed,
    ActorTendency firstTendency,
    ActorTendency secondTendency,
    bool firstActorAttacks,
    CombatScenarioId firstScenario)
{
    state = {};
    state.phase = CombatEpisodePhase::Ready;
    state.randomState = MixSeed(seed);
    state.plannedExchangeCount = ChoosePlannedExchangeCount(state.randomState);
    state.currentAttackerRole = firstActorAttacks ? 0 : 1;
    state.currentDefenderRole = 1 - state.currentAttackerRole;
    state.tendencies = {firstTendency, secondTendency};
    state.currentScenario = firstScenario == CombatScenarioId::SideKickHeavyHit ?
        CombatScenarioId::None : firstScenario;
    PrepareNextExchange(state, true);
}

CombatEpisodeStep UpdateCombatEpisode(
    CombatEpisodeState& state,
    const CombatEpisodeReadiness& readiness,
    double deltaSeconds)
{
    CombatEpisodeStep step;
    if (state.phase == CombatEpisodePhase::Inactive ||
        state.phase == CombatEpisodePhase::Complete ||
        state.phase == CombatEpisodePhase::Cancelled ||
        !std::isfinite(deltaSeconds) || deltaSeconds < 0.0) {
        return step;
    }
    if (!readiness.actorsValid) {
        CancelCombatEpisode(state);
        step.cancelled = true;
        step.finishReason = state.finishReason;
        return step;
    }

    const CombatEpisodeTuning& tuning = GetCombatEpisodeTuning();
    state.elapsedSeconds += std::min(deltaSeconds, 4.0);
    state.finishRequested = state.finishRequested || readiness.finishRequested;
    if (state.elapsedSeconds >= tuning.hardTimeoutSeconds) {
        state.timedOut = true;
        state.finishRequested = true;
    }
    if (state.phase == CombatEpisodePhase::Regrouping && state.finishRequested) {
        state.finishAfterRegroup = true;
        state.finishReason = state.timedOut ?
            CombatEpisodeFinishReason::Timeout : CombatEpisodeFinishReason::StopRequested;
    }

    if (state.phase == CombatEpisodePhase::ExchangeActive && readiness.pairRecovered &&
        readiness.pairResult != CombatResult::None && !state.pairCompletionConsumed) {
        state.pairCompletionConsumed = true;
        ++state.completedExchangeCount;
        ApplyExchangeResult(state, readiness.pairResult);
        step.exchangeCompleted = true;
        step.exchangeIndex = state.completedExchangeCount;
        step.attackerRole = state.currentAttackerRole;
        step.scenario = state.currentScenario;
        step.exchangeResult = readiness.pairResult;

        bool finish = state.finishRequested || state.timedOut;
        if (!finish) finish = ShouldFinishNaturally(state);
        if (finish) {
            state.finishAfterRegroup = true;
            if (state.timedOut) state.finishReason = CombatEpisodeFinishReason::Timeout;
            else if (state.finishRequested) state.finishReason = CombatEpisodeFinishReason::StopRequested;
            else if (state.completedExchangeCount >= state.plannedExchangeCount) {
                state.finishReason = CombatEpisodeFinishReason::PlannedExchanges;
            } else {
                state.finishReason = CombatEpisodeFinishReason::DecisiveResult;
            }
        } else {
            PrepareNextExchange(state, false);
        }
        state.regroupRemainingSeconds = tuning.regroupMinimumSeconds +
            NextUnit(state.randomState) *
                (tuning.regroupMaximumSeconds - tuning.regroupMinimumSeconds);
        state.phase = CombatEpisodePhase::Regrouping;
        step.finishReason = state.finishReason;
        return step;
    }

    if (state.phase == CombatEpisodePhase::Ready) {
        if (!readiness.readyForExchange) return step;
        state.phase = CombatEpisodePhase::ExchangeActive;
        step.startExchange = true;
    } else if (state.phase == CombatEpisodePhase::Regrouping) {
        state.regroupRemainingSeconds = std::max(
            0.0, state.regroupRemainingSeconds - std::min(deltaSeconds, 4.0));
        if (state.regroupRemainingSeconds > 1e-9 || !readiness.readyForExchange) return step;
        if (state.finishAfterRegroup) {
            state.phase = CombatEpisodePhase::Complete;
            state.completed = true;
            if (!state.completionEmitted) {
                state.completionEmitted = true;
                step.episodeCompleted = true;
            }
        } else {
            state.phase = CombatEpisodePhase::ExchangeActive;
            step.startExchange = true;
        }
    }

    step.exchangeIndex = state.completedExchangeCount + (state.completed ? 0 : 1);
    step.attackerRole = state.currentAttackerRole;
    step.scenario = state.currentScenario;
    step.finishReason = state.finishReason;
    return step;
}

void CancelCombatEpisode(CombatEpisodeState& state)
{
    if (state.phase == CombatEpisodePhase::Inactive || state.phase == CombatEpisodePhase::Complete) return;
    state.phase = CombatEpisodePhase::Cancelled;
    state.cancelled = true;
    state.completed = false;
    state.finishReason = CombatEpisodeFinishReason::Cancelled;
}

} // namespace besktop
