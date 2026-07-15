#include "besktop/animation/actor_ecosystem.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace {

constexpr double kIntentMinimumSeconds = 0.35;
constexpr double kIntentMaximumSeconds = 0.80;
constexpr double kRecentEncounterMemorySeconds = 28.0;

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

bool IsFiniteActor(const besktop::ActorPerceptionInput& actor)
{
    return std::isfinite(actor.x) && std::isfinite(actor.y) &&
        std::isfinite(actor.velocityX) && std::isfinite(actor.velocityY) &&
        std::isfinite(actor.extent) && actor.extent > 0.0;
}

bool IsAvailable(
    const besktop::ActorPerceptionInput& actor,
    const besktop::ActorRuntimeState& runtime)
{
    return IsFiniteActor(actor) && actor.awake && actor.wandering &&
        !actor.turning && !actor.actionActive && !actor.recovering && !actor.controlled &&
        runtime.encounterCooldownRemaining <= 0.0;
}

bool ReservationFits(
    const besktop::EncounterBounds& bounds,
    double centerX,
    double centerY,
    double radius)
{
    return bounds.right > bounds.left && bounds.bottom > bounds.top &&
        centerX - radius >= bounds.left && centerX + radius <= bounds.right &&
        centerY - radius >= bounds.top && centerY + radius <= bounds.bottom;
}

std::size_t IntentIndex(besktop::LocalIntent intent)
{
    return static_cast<std::size_t>(intent);
}

} // namespace

namespace besktop {

std::wstring_view ActorTendencyName(ActorTendency tendency)
{
    switch (tendency) {
    case ActorTendency::Bold: return L"Bold";
    case ActorTendency::Timid: return L"Timid";
    case ActorTendency::Curious: return L"Curious";
    case ActorTendency::Calm: return L"Calm";
    case ActorTendency::Energetic: return L"Energetic";
    }
    return L"Calm";
}

std::wstring_view LocalIntentName(LocalIntent intent)
{
    switch (intent) {
    case LocalIntent::Avoid: return L"Avoid";
    case LocalIntent::Observe: return L"Observe";
    case LocalIntent::Approach: return L"Approach";
    case LocalIntent::Challenge: return L"Challenge";
    case LocalIntent::Respond: return L"Respond";
    case LocalIntent::Yield: return L"Yield";
    default: return L"Ignore";
    }
}

ActorBehaviorProfile GenerateActorBehaviorProfile(std::uint32_t stableSeed)
{
    const std::uint32_t mixed = MixSeed(stableSeed ^ 0xB3A5C7D9u);
    return {static_cast<ActorTendency>(mixed % 5u), mixed};
}

double ActorAssessDurationAdjustment(ActorTendency tendency)
{
    switch (tendency) {
    case ActorTendency::Bold: return -0.04;
    case ActorTendency::Timid: return 0.03;
    case ActorTendency::Curious: return 0.06;
    case ActorTendency::Calm: return 0.08;
    case ActorTendency::Energetic: return -0.08;
    }
    return 0.0;
}

void InitializeActorRuntimeState(ActorRuntimeState& state, std::uint32_t stableSeed)
{
    state = {};
    state.stamina = 1.0;
    state.randomState = MixSeed(stableSeed ^ 0x51A7E11Du);
}

void UpdateActorRuntimeState(ActorRuntimeState& state, double deltaSeconds)
{
    if (!std::isfinite(deltaSeconds) || deltaSeconds <= 0.0) return;
    const double delta = std::min(deltaSeconds, 60.0);
    state.alertness = std::max(0.0, state.alertness - delta * 0.10);
    state.agitation = std::max(0.0, state.agitation - delta * 0.08);
    state.stamina = std::min(1.0, state.stamina + delta * 0.055);
    state.encounterCooldownRemaining = std::max(0.0, state.encounterCooldownRemaining - delta);
    state.recentEncounterMemoryRemaining = std::max(0.0, state.recentEncounterMemoryRemaining - delta);
}

LocalPerception FindLocalPerception(
    std::size_t selfIndex,
    std::span<const ActorPerceptionInput> actors,
    std::span<const ActorRuntimeState> runtimeStates,
    const EncounterBounds& bounds)
{
    LocalPerception best;
    best.score = std::numeric_limits<double>::infinity();
    if (selfIndex >= actors.size() || actors.size() != runtimeStates.size() ||
        !IsAvailable(actors[selfIndex], runtimeStates[selfIndex])) {
        return best;
    }

    const ActorPerceptionInput& self = actors[selfIndex];
    for (std::size_t otherIndex = 0; otherIndex < actors.size(); ++otherIndex) {
        if (otherIndex == selfIndex || !IsAvailable(actors[otherIndex], runtimeStates[otherIndex])) continue;
        const ActorPerceptionInput& other = actors[otherIndex];
        const double extent = std::max({36.0, self.extent, other.extent});
        const double dx = other.x - self.x;
        const double dy = other.y - self.y;
        const double distance = std::hypot(dx, dy);
        if (distance < extent * 1.80 || distance > extent * 6.25) continue;

        const double centerX = (self.x + other.x) * 0.5;
        const double centerY = (self.y + other.y) * 0.5;
        const double reservationRadius = extent * 2.35;
        if (!ReservationFits(bounds, centerX, centerY, reservationRadius)) continue;

        const double relativeVelocityX = other.velocityX - self.velocityX;
        const double relativeVelocityY = other.velocityY - self.velocityY;
        const bool approaching = dx * relativeVelocityX + dy * relativeVelocityY < -extent * 1.5;
        if (!approaching && distance > extent * 3.65) continue;

        double viewX = self.velocityX;
        double viewY = self.velocityY;
        double viewLength = std::hypot(viewX, viewY);
        if (viewLength <= 1e-6) {
            viewX = self.facingX;
            viewY = self.facingY;
            viewLength = std::hypot(viewX, viewY);
        }
        const double viewDot = viewLength > 1e-6 && distance > 1e-6 ?
            (viewX * dx + viewY * dy) / (viewLength * distance) : 0.0;
        const bool inView = viewDot >= -0.18;

        const ActorRuntimeState& runtime = runtimeStates[selfIndex];
        const bool recentlySameActor = runtime.lastEncounterActor == other.actorIndex &&
            runtime.recentEncounterMemoryRemaining > 0.0;
        const double recentPenalty = recentlySameActor ? extent * 3.2 : 0.0;
        const double viewPenalty = inView ? 0.0 : extent * 0.85;
        const double approachBonus = approaching ? extent * 0.55 : 0.0;
        const double score = distance + recentPenalty + viewPenalty - approachBonus;
        if (score < best.score - 1e-9 ||
            (std::abs(score - best.score) <= 1e-9 && other.actorIndex < best.actorIndex)) {
            best = {true, other.actorIndex, distance, score, approaching, inView};
        }
    }
    return best;
}

bool UpdateActorLocalIntent(
    ActorRuntimeState& state,
    const ActorBehaviorProfile& profile,
    const LocalPerception& perception,
    LocalIntent observedOpponentIntent,
    double deltaSeconds)
{
    const double delta = std::isfinite(deltaSeconds) && deltaSeconds > 0.0 ?
        std::min(deltaSeconds, 8.0) : 0.0;
    state.intentDecisionRemaining = std::max(0.0, state.intentDecisionRemaining - delta);
    if (state.intentDecisionRemaining > 0.0) return false;

    const LocalIntent previousIntent = state.heldIntent;
    const std::size_t previousTarget = state.intentTargetActor;
    state.intentDecisionRemaining = kIntentMinimumSeconds +
        NextUnit(state.randomState) * (kIntentMaximumSeconds - kIntentMinimumSeconds);
    if (!perception.perceived) {
        state.heldIntent = LocalIntent::Ignore;
        state.intentTargetActor = kNoEncounterActor;
        state.intentCandidateScore = 0.0;
        return previousIntent != state.heldIntent || previousTarget != state.intentTargetActor;
    }

    std::array<double, 7> weights{0.90, 0.24, 0.72, 0.62, 0.28, 0.10, 0.12};
    switch (profile.tendency) {
    case ActorTendency::Bold:
        weights[IntentIndex(LocalIntent::Challenge)] += 1.35;
        weights[IntentIndex(LocalIntent::Respond)] += 0.90;
        weights[IntentIndex(LocalIntent::Yield)] *= 0.45;
        weights[IntentIndex(LocalIntent::Avoid)] *= 0.55;
        break;
    case ActorTendency::Timid:
        weights[IntentIndex(LocalIntent::Avoid)] += 1.15;
        weights[IntentIndex(LocalIntent::Yield)] += 0.95;
        weights[IntentIndex(LocalIntent::Challenge)] *= 0.24;
        weights[IntentIndex(LocalIntent::Approach)] *= 0.58;
        break;
    case ActorTendency::Curious:
        weights[IntentIndex(LocalIntent::Observe)] += 1.25;
        weights[IntentIndex(LocalIntent::Approach)] += 1.00;
        break;
    case ActorTendency::Calm:
        weights[IntentIndex(LocalIntent::Ignore)] += 1.00;
        weights[IntentIndex(LocalIntent::Observe)] += 0.62;
        weights[IntentIndex(LocalIntent::Challenge)] *= 0.32;
        break;
    case ActorTendency::Energetic:
        weights[IntentIndex(LocalIntent::Approach)] += 1.25;
        weights[IntentIndex(LocalIntent::Observe)] += 0.35;
        weights[IntentIndex(LocalIntent::Challenge)] += 0.22;
        weights[IntentIndex(LocalIntent::Respond)] += 0.30;
        break;
    }

    if (observedOpponentIntent == LocalIntent::Challenge) {
        weights[IntentIndex(LocalIntent::Respond)] += 1.55;
        weights[IntentIndex(LocalIntent::Yield)] += 0.90;
        weights[IntentIndex(LocalIntent::Avoid)] += 0.70;
        weights[IntentIndex(LocalIntent::Ignore)] *= 0.25;
    } else if (observedOpponentIntent == LocalIntent::Approach) {
        weights[IntentIndex(LocalIntent::Observe)] += 0.85;
        weights[IntentIndex(LocalIntent::Approach)] += 0.75;
    } else if (observedOpponentIntent == LocalIntent::Avoid) {
        weights[IntentIndex(LocalIntent::Ignore)] += 1.10;
        weights[IntentIndex(LocalIntent::Challenge)] *= 0.15;
    }

    if (perception.naturallyApproaching) {
        weights[IntentIndex(LocalIntent::Observe)] += 0.35;
        weights[IntentIndex(LocalIntent::Respond)] += 0.28;
    }
    if (!perception.inView) {
        weights[IntentIndex(LocalIntent::Ignore)] += 0.65;
        weights[IntentIndex(LocalIntent::Challenge)] *= 0.55;
    }
    weights[IntentIndex(LocalIntent::Challenge)] += state.agitation * 0.75;
    weights[IntentIndex(LocalIntent::Respond)] += state.alertness * 0.55;
    weights[IntentIndex(LocalIntent::Avoid)] += (1.0 - state.stamina) * 0.80;
    weights[IntentIndex(LocalIntent::Yield)] += (1.0 - state.stamina) * 0.65;

    double total = 0.0;
    for (const double weight : weights) total += std::max(0.0, weight);
    double sample = NextUnit(state.randomState) * total;
    LocalIntent chosen = LocalIntent::Ignore;
    for (std::size_t index = 0; index < weights.size(); ++index) {
        sample -= std::max(0.0, weights[index]);
        if (sample <= 0.0) {
            chosen = static_cast<LocalIntent>(index);
            break;
        }
    }
    state.heldIntent = chosen;
    state.intentTargetActor = perception.actorIndex;
    state.intentCandidateScore = 1.0 / (1.0 + std::max(0.0, perception.score));
    return previousIntent != chosen || previousTarget != perception.actorIndex;
}

EncounterIntent ResolveLocalIntentHandshake(LocalIntent first, LocalIntent second)
{
    if (first == LocalIntent::Avoid || second == LocalIntent::Avoid ||
        first == LocalIntent::Ignore || second == LocalIntent::Ignore) {
        return EncounterIntent::Undecided;
    }
    const bool firstChallenges = first == LocalIntent::Challenge;
    const bool secondChallenges = second == LocalIntent::Challenge;
    if ((firstChallenges && second == LocalIntent::Respond) ||
        (secondChallenges && first == LocalIntent::Respond) ||
        (firstChallenges && secondChallenges)) {
        return EncounterIntent::Combat;
    }
    if ((firstChallenges && second == LocalIntent::Yield) ||
        (secondChallenges && first == LocalIntent::Yield)) {
        return EncounterIntent::Yield;
    }
    const bool firstApproaches = first == LocalIntent::Approach;
    const bool secondApproaches = second == LocalIntent::Approach;
    const bool approachObserved =
        (firstApproaches && (second == LocalIntent::Observe || secondApproaches)) ||
        (secondApproaches && first == LocalIntent::Observe);
    return approachObserved ? EncounterIntent::Bluff : EncounterIntent::Undecided;
}

std::vector<LocalEncounterRequest> BuildLocalEncounterRequests(
    std::span<const ActorPerceptionInput> actors,
    std::span<const ActorRuntimeState> runtimeStates,
    bool interactionsEnabled)
{
    std::vector<LocalEncounterRequest> requests;
    if (!interactionsEnabled || actors.size() != runtimeStates.size()) return requests;
    requests.reserve(actors.size() / 2);
    for (std::size_t firstIndex = 0; firstIndex < actors.size(); ++firstIndex) {
        const ActorPerceptionInput& first = actors[firstIndex];
        const ActorRuntimeState& firstState = runtimeStates[firstIndex];
        if (!IsAvailable(first, firstState)) continue;
        for (std::size_t secondIndex = firstIndex + 1; secondIndex < actors.size(); ++secondIndex) {
            const ActorPerceptionInput& second = actors[secondIndex];
            const ActorRuntimeState& secondState = runtimeStates[secondIndex];
            if (!IsAvailable(second, secondState) || first.actorIndex == second.actorIndex ||
                firstState.intentTargetActor != second.actorIndex ||
                secondState.intentTargetActor != first.actorIndex) {
                continue;
            }
            const EncounterIntent encounterIntent = ResolveLocalIntentHandshake(
                firstState.heldIntent, secondState.heldIntent);
            if (encounterIntent == EncounterIntent::Undecided) continue;

            const bool firstInitiates = firstState.heldIntent == LocalIntent::Challenge ||
                firstState.heldIntent == LocalIntent::Approach;
            const bool secondInitiates = secondState.heldIntent == LocalIntent::Challenge ||
                secondState.heldIntent == LocalIntent::Approach;
            const bool useFirst = firstInitiates != secondInitiates ? firstInitiates :
                first.actorIndex < second.actorIndex;
            const double extent = std::max({36.0, first.extent, second.extent});
            requests.push_back({
                useFirst ? first.actorIndex : second.actorIndex,
                useFirst ? second.actorIndex : first.actorIndex,
                useFirst ? firstState.heldIntent : secondState.heldIntent,
                useFirst ? secondState.heldIntent : firstState.heldIntent,
                encounterIntent,
                {(first.x + second.x) * 0.5, (first.y + second.y) * 0.5, extent * 2.35},
                firstState.intentCandidateScore + secondState.intentCandidateScore,
            });
        }
    }
    return requests;
}

void RecordActorEncounter(
    ActorRuntimeState& state,
    std::size_t counterpartIndex,
    ActorEncounterOutcome outcome,
    double cooldownSeconds)
{
    state.lastEncounterActor = counterpartIndex;
    state.lastEncounterOutcome = outcome;
    state.encounterCooldownRemaining = std::max(state.encounterCooldownRemaining, std::max(0.0, cooldownSeconds));
    state.recentEncounterMemoryRemaining = kRecentEncounterMemorySeconds;
    state.alertness = std::clamp(state.alertness + 0.35, 0.0, 1.0);
    state.agitation = std::clamp(state.agitation +
        (outcome == ActorEncounterOutcome::Combat ? 0.38 : 0.16), 0.0, 1.0);
    state.stamina = std::clamp(state.stamina -
        (outcome == ActorEncounterOutcome::Combat ? 0.16 : 0.05), 0.0, 1.0);
    state.heldIntent = LocalIntent::Ignore;
    state.intentTargetActor = kNoEncounterActor;
    state.intentDecisionRemaining = kIntentMinimumSeconds;
    state.intentCandidateScore = 0.0;
}

} // namespace besktop
