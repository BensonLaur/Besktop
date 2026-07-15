#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace besktop {

enum class AwakeningWave {
    First,
    Second,
    Fallback,
};

struct AwakeningBounds {
    double left = 0.0;
    double top = 0.0;
    double right = 0.0;
    double bottom = 0.0;
};

struct AwakeningActorInput {
    std::size_t actorIndex = 0;
    double x = 0.0;
    double y = 0.0;
    double extent = 48.0;
    std::uint32_t seed = 1u;
};

struct AwakeningActorObservation {
    std::size_t actorIndex = 0;
    double x = 0.0;
    double y = 0.0;
    double extent = 48.0;
    bool wandering = false;
};

struct AwakeningPlanEntry {
    std::size_t actorIndex = 0;
    AwakeningWave wave = AwakeningWave::Fallback;
    double originalStartSeconds = 0.0;
    double startSeconds = 0.0;
    double proximityDelaySeconds = 0.0;
    std::uint32_t stableSeed = 1u;
};

struct AwakeningPlanSummary {
    std::size_t totalCount = 0;
    std::size_t firstWaveCount = 0;
    std::size_t secondWaveCount = 0;
    std::size_t fallbackWaveCount = 0;
    double firstStartMinimum = 0.0;
    double firstStartMaximum = 0.0;
    double secondStartMinimum = 0.0;
    double secondStartMaximum = 0.0;
    double fallbackStartMinimum = 0.0;
    double fallbackStartMaximum = 0.0;
};

struct AwakeningDirectorTuning {
    double desktopPauseSeconds = 1.00;
    double firstWaveStartMinimumSeconds = 2.00;
    double firstWaveStartMaximumSeconds = 9.00;
    double secondWaveStartMinimumSeconds = 12.00;
    double secondWaveStartMaximumSeconds = 26.00;
    double fallbackWaveStartMinimumSeconds = 30.00;
    double fallbackWaveStartMaximumSeconds = 52.00;
    double proximityCheckIntervalSeconds = 0.15;
    double proximityRadiusScale = 2.70;
    double proximityDelayMinimumSeconds = 4.00;
    double proximityDelayMaximumSeconds = 7.00;
};

struct AwakeningDirectorState {
    std::vector<AwakeningPlanEntry> entries;
    AwakeningPlanSummary summary{};
    double nextProximityCheckSeconds = 0.0;
    std::size_t proximityAccelerationCount = 0;
};

const AwakeningDirectorTuning& GetAwakeningDirectorTuning();

void InitializeAwakeningDirector(
    AwakeningDirectorState& state,
    std::span<const AwakeningActorInput> actors,
    const AwakeningBounds& bounds,
    std::uint32_t seed = 0xA7413E5Du);

const AwakeningPlanEntry* FindAwakeningPlanEntry(
    const AwakeningDirectorState& state,
    std::size_t actorIndex);

double GetActorAwakeningStartSeconds(
    const AwakeningDirectorState& state,
    std::size_t actorIndex);

bool UpdateAwakeningProximity(
    AwakeningDirectorState& state,
    std::span<const AwakeningActorObservation> actors,
    double elapsedSeconds);

bool IsFirstWaveEcosystemReady(
    const AwakeningDirectorState& state,
    std::span<const AwakeningActorObservation> actors);

double LatestAwakeningStartSeconds(const AwakeningDirectorState& state);

} // namespace besktop
