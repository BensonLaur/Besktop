#include "besktop/animation/awakening_director.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>

namespace {

std::uint32_t MixBits(std::uint32_t value)
{
    value ^= value >> 16;
    value *= 0x7FEB352Du;
    value ^= value >> 15;
    value *= 0x846CA68Bu;
    value ^= value >> 16;
    return value == 0 ? 1u : value;
}

double UnitFromSeed(std::uint32_t seed)
{
    return static_cast<double>(MixBits(seed) & 0x00FFFFFFu) / 16777215.0;
}

double NormalizedDistanceSquared(
    const besktop::AwakeningActorInput& first,
    const besktop::AwakeningActorInput& second,
    const besktop::AwakeningBounds& bounds)
{
    const double width = std::max(1.0, bounds.right - bounds.left);
    const double height = std::max(1.0, bounds.bottom - bounds.top);
    const double dx = (first.x - second.x) / width;
    const double dy = (first.y - second.y) / height;
    return dx * dx + dy * dy;
}

std::size_t FirstWaveCount(std::size_t actorCount)
{
    if (actorCount == 0) return 0;
    if (actorCount == 1) return 1;
    const std::size_t proportional = static_cast<std::size_t>(
        std::llround(static_cast<double>(actorCount) * 0.20));
    return std::min(actorCount, std::max<std::size_t>(2, proportional));
}

std::size_t SecondWaveCount(std::size_t actorCount, std::size_t firstWaveCount)
{
    if (actorCount <= firstWaveCount) return 0;
    const std::size_t proportional = static_cast<std::size_t>(
        std::llround(static_cast<double>(actorCount) * 0.35));
    return std::min(actorCount - firstWaveCount, std::max<std::size_t>(1, proportional));
}

void AssignWaveTimes(
    std::vector<besktop::AwakeningPlanEntry>& entries,
    besktop::AwakeningWave wave,
    double minimumSeconds,
    double maximumSeconds,
    std::uint32_t salt)
{
    std::vector<std::size_t> indices;
    indices.reserve(entries.size());
    for (std::size_t index = 0; index < entries.size(); ++index) {
        if (entries[index].wave == wave) indices.push_back(index);
    }
    std::sort(indices.begin(), indices.end(), [&](std::size_t first, std::size_t second) {
        return MixBits(entries[first].stableSeed ^ salt) < MixBits(entries[second].stableSeed ^ salt);
    });
    const double range = std::max(0.0, maximumSeconds - minimumSeconds);
    for (std::size_t rank = 0; rank < indices.size(); ++rank) {
        besktop::AwakeningPlanEntry& entry = entries[indices[rank]];
        const double jitter = UnitFromSeed(entry.stableSeed ^ salt ^ 0xB5297A4Du);
        const double fraction = (static_cast<double>(rank) + 0.20 + jitter * 0.60) /
            static_cast<double>(indices.size());
        entry.originalStartSeconds = minimumSeconds + range * fraction;
        entry.startSeconds = entry.originalStartSeconds;
    }
}

void AccumulateRange(
    const std::vector<besktop::AwakeningPlanEntry>& entries,
    besktop::AwakeningWave wave,
    double& minimumSeconds,
    double& maximumSeconds)
{
    minimumSeconds = std::numeric_limits<double>::infinity();
    maximumSeconds = 0.0;
    for (const besktop::AwakeningPlanEntry& entry : entries) {
        if (entry.wave != wave) continue;
        minimumSeconds = std::min(minimumSeconds, entry.originalStartSeconds);
        maximumSeconds = std::max(maximumSeconds, entry.originalStartSeconds);
    }
    if (!std::isfinite(minimumSeconds)) minimumSeconds = 0.0;
}

} // namespace

namespace besktop {

const AwakeningDirectorTuning& GetAwakeningDirectorTuning()
{
    // Product pacing lives in the tuning struct so planning and tests share one source of truth.
    static const AwakeningDirectorTuning tuning{};
    return tuning;
}

void InitializeAwakeningDirector(
    AwakeningDirectorState& state,
    std::span<const AwakeningActorInput> actors,
    const AwakeningBounds& bounds,
    std::uint32_t seed)
{
    state = {};
    if (actors.empty()) return;

    state.entries.resize(actors.size());
    std::vector<std::size_t> inputByActorIndex(actors.size(), actors.size());
    for (std::size_t inputIndex = 0; inputIndex < actors.size(); ++inputIndex) {
        const AwakeningActorInput& actor = actors[inputIndex];
        if (actor.actorIndex < inputByActorIndex.size()) inputByActorIndex[actor.actorIndex] = inputIndex;
    }
    for (std::size_t actorIndex = 0; actorIndex < actors.size(); ++actorIndex) {
        const std::size_t inputIndex = inputByActorIndex[actorIndex] < actors.size() ?
            inputByActorIndex[actorIndex] : actorIndex;
        const AwakeningActorInput& actor = actors[inputIndex];
        AwakeningPlanEntry& entry = state.entries[actorIndex];
        entry.actorIndex = actorIndex;
        entry.stableSeed = MixBits(actor.seed ^ seed ^ static_cast<std::uint32_t>(actorIndex * 0x9E3779B9u));
        const AwakeningDirectorTuning& tuning = GetAwakeningDirectorTuning();
        entry.proximityDelaySeconds = tuning.proximityDelayMinimumSeconds +
            UnitFromSeed(entry.stableSeed ^ 0x68E31DA4u) *
                (tuning.proximityDelayMaximumSeconds - tuning.proximityDelayMinimumSeconds);
    }

    const std::size_t firstCount = FirstWaveCount(actors.size());
    const std::size_t secondCount = SecondWaveCount(actors.size(), firstCount);
    std::vector<bool> firstSelected(actors.size(), false);
    std::vector<std::size_t> firstIndices;
    firstIndices.reserve(firstCount);

    const double width = std::max(1.0, bounds.right - bounds.left);
    const double height = std::max(1.0, bounds.bottom - bounds.top);
    const double anchorX = bounds.left + width * (0.25 + UnitFromSeed(seed ^ 0x243F6A88u) * 0.50);
    const double anchorY = bounds.top + height * (0.25 + UnitFromSeed(seed ^ 0x85A308D3u) * 0.50);
    for (std::size_t selection = 0; selection < firstCount; ++selection) {
        std::size_t best = actors.size();
        double bestScore = selection == 0 ? std::numeric_limits<double>::infinity() : -1.0;
        for (std::size_t candidate = 0; candidate < actors.size(); ++candidate) {
            if (firstSelected[candidate]) continue;
            double score = 0.0;
            if (selection == 0) {
                const double dx = (actors[candidate].x - anchorX) / width;
                const double dy = (actors[candidate].y - anchorY) / height;
                score = dx * dx + dy * dy +
                    UnitFromSeed(state.entries[actors[candidate].actorIndex].stableSeed) * 0.002;
                if (score < bestScore) {
                    bestScore = score;
                    best = candidate;
                }
                continue;
            }
            double nearestSelected = std::numeric_limits<double>::infinity();
            for (std::size_t selected : firstIndices) {
                nearestSelected = std::min(
                    nearestSelected,
                    NormalizedDistanceSquared(actors[candidate], actors[selected], bounds));
            }
            score = nearestSelected +
                UnitFromSeed(state.entries[actors[candidate].actorIndex].stableSeed ^ 0x13198A2Eu) * 0.002;
            if (score > bestScore) {
                bestScore = score;
                best = candidate;
            }
        }
        if (best == actors.size()) break;
        firstSelected[best] = true;
        firstIndices.push_back(best);
        state.entries[actors[best].actorIndex].wave = AwakeningWave::First;
    }

    std::vector<std::size_t> remaining;
    remaining.reserve(actors.size() - firstIndices.size());
    for (std::size_t inputIndex = 0; inputIndex < actors.size(); ++inputIndex) {
        if (!firstSelected[inputIndex]) remaining.push_back(inputIndex);
    }
    std::sort(remaining.begin(), remaining.end(), [&](std::size_t first, std::size_t second) {
        const std::uint32_t firstKey = MixBits(state.entries[actors[first].actorIndex].stableSeed ^ 0xA4093822u);
        const std::uint32_t secondKey = MixBits(state.entries[actors[second].actorIndex].stableSeed ^ 0xA4093822u);
        return firstKey < secondKey;
    });
    for (std::size_t rank = 0; rank < remaining.size(); ++rank) {
        state.entries[actors[remaining[rank]].actorIndex].wave =
            rank < secondCount ? AwakeningWave::Second : AwakeningWave::Fallback;
    }

    const AwakeningDirectorTuning& tuning = GetAwakeningDirectorTuning();
    AssignWaveTimes(
        state.entries, AwakeningWave::First,
        tuning.firstWaveStartMinimumSeconds, tuning.firstWaveStartMaximumSeconds, 0x082EFA98u);
    AssignWaveTimes(
        state.entries, AwakeningWave::Second,
        tuning.secondWaveStartMinimumSeconds, tuning.secondWaveStartMaximumSeconds, 0xEC4E6C89u);
    AssignWaveTimes(
        state.entries, AwakeningWave::Fallback,
        tuning.fallbackWaveStartMinimumSeconds, tuning.fallbackWaveStartMaximumSeconds, 0x452821E6u);

    state.summary.totalCount = actors.size();
    state.summary.firstWaveCount = firstIndices.size();
    state.summary.secondWaveCount = secondCount;
    state.summary.fallbackWaveCount = actors.size() - firstIndices.size() - secondCount;
    AccumulateRange(
        state.entries, AwakeningWave::First,
        state.summary.firstStartMinimum, state.summary.firstStartMaximum);
    AccumulateRange(
        state.entries, AwakeningWave::Second,
        state.summary.secondStartMinimum, state.summary.secondStartMaximum);
    AccumulateRange(
        state.entries, AwakeningWave::Fallback,
        state.summary.fallbackStartMinimum, state.summary.fallbackStartMaximum);
    state.nextProximityCheckSeconds = tuning.desktopPauseSeconds;
}

const AwakeningPlanEntry* FindAwakeningPlanEntry(
    const AwakeningDirectorState& state,
    std::size_t actorIndex)
{
    if (actorIndex < state.entries.size() && state.entries[actorIndex].actorIndex == actorIndex) {
        return &state.entries[actorIndex];
    }
    const auto found = std::find_if(state.entries.begin(), state.entries.end(), [&](const AwakeningPlanEntry& entry) {
        return entry.actorIndex == actorIndex;
    });
    return found == state.entries.end() ? nullptr : &*found;
}

double GetActorAwakeningStartSeconds(
    const AwakeningDirectorState& state,
    std::size_t actorIndex)
{
    const AwakeningPlanEntry* entry = FindAwakeningPlanEntry(state, actorIndex);
    return entry == nullptr ? GetAwakeningDirectorTuning().desktopPauseSeconds : entry->startSeconds;
}

bool UpdateAwakeningProximity(
    AwakeningDirectorState& state,
    std::span<const AwakeningActorObservation> actors,
    double elapsedSeconds)
{
    if (!std::isfinite(elapsedSeconds) || elapsedSeconds < state.nextProximityCheckSeconds) return false;
    state.nextProximityCheckSeconds = elapsedSeconds + GetAwakeningDirectorTuning().proximityCheckIntervalSeconds;
    bool changed = false;
    for (AwakeningPlanEntry& sleeper : state.entries) {
        if (elapsedSeconds >= sleeper.startSeconds) continue;
        const auto sleeperObservation = std::find_if(actors.begin(), actors.end(), [&](const AwakeningActorObservation& actor) {
            return actor.actorIndex == sleeper.actorIndex;
        });
        if (sleeperObservation == actors.end()) continue;
        for (const AwakeningActorObservation& influencer : actors) {
            if (!influencer.wandering || influencer.actorIndex == sleeper.actorIndex) continue;
            const double influenceRadius = std::max(sleeperObservation->extent, influencer.extent) *
                GetAwakeningDirectorTuning().proximityRadiusScale;
            if (std::hypot(influencer.x - sleeperObservation->x, influencer.y - sleeperObservation->y) >
                influenceRadius) continue;
            const double acceleratedStart = std::max(
                GetAwakeningDirectorTuning().desktopPauseSeconds,
                elapsedSeconds + sleeper.proximityDelaySeconds);
            if (acceleratedStart + 1e-6 < sleeper.startSeconds) {
                sleeper.startSeconds = acceleratedStart;
                ++state.proximityAccelerationCount;
                changed = true;
            }
            break;
        }
    }
    return changed;
}

bool IsFirstWaveEcosystemReady(
    const AwakeningDirectorState& state,
    std::span<const AwakeningActorObservation> actors)
{
    if (state.summary.firstWaveCount == 0) return false;
    std::size_t wanderingCount = 0;
    for (const AwakeningActorObservation& actor : actors) {
        if (actor.wandering) ++wanderingCount;
    }
    if (wanderingCount < 2) return false;
    for (const AwakeningPlanEntry& entry : state.entries) {
        if (entry.wave != AwakeningWave::First) continue;
        const auto observation = std::find_if(actors.begin(), actors.end(), [&](const AwakeningActorObservation& actor) {
            return actor.actorIndex == entry.actorIndex;
        });
        if (observation == actors.end() || !observation->wandering) return false;
    }
    return true;
}

double LatestAwakeningStartSeconds(const AwakeningDirectorState& state)
{
    double latest = 0.0;
    for (const AwakeningPlanEntry& entry : state.entries) latest = std::max(latest, entry.startSeconds);
    return latest;
}

} // namespace besktop
