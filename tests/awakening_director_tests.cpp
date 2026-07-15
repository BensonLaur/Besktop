#include "besktop/animation/awakening_director.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <set>
#include <vector>

namespace {

int failures = 0;

void Check(bool condition, const char* name)
{
    if (!condition) {
        ++failures;
        std::cerr << "FAILED: " << name << '\n';
    }
}

std::vector<besktop::AwakeningActorInput> GridActors(std::size_t count)
{
    std::vector<besktop::AwakeningActorInput> actors;
    actors.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        actors.push_back({
            index,
            100.0 + static_cast<double>(index % 8) * 105.0,
            100.0 + static_cast<double>(index / 8) * 125.0,
            48.0,
            0x12340000u ^ static_cast<std::uint32_t>((index + 1) * 0x9E3779B9u),
        });
    }
    return actors;
}

void TestEmptyAndSmallPlans()
{
    using namespace besktop;
    const AwakeningBounds bounds{0.0, 0.0, 1000.0, 800.0};
    AwakeningDirectorState state;
    InitializeAwakeningDirector(state, {}, bounds);
    Check(state.entries.empty() && state.summary.totalCount == 0, "zero actors produce empty plan");

    for (std::size_t count = 1; count <= 4; ++count) {
        const std::vector actors = GridActors(count);
        InitializeAwakeningDirector(state, actors, bounds, 42u);
        Check(state.entries.size() == count, "small plan retains every actor");
        std::set<int> startMilliseconds;
        for (const AwakeningPlanEntry& entry : state.entries) {
            Check(entry.startSeconds >= GetAwakeningDirectorTuning().desktopPauseSeconds,
                "small actor never starts before desktop pause");
            Check(entry.startSeconds <= GetAwakeningDirectorTuning().fallbackWaveStartMaximumSeconds,
                "small actor remains inside fallback ceiling");
            startMilliseconds.insert(static_cast<int>(std::lround(entry.startSeconds * 1000.0)));
        }
        Check(startMilliseconds.size() == count, "small plans keep visible deterministic staggering");
    }
}

void TestWaveRatiosAndFallback()
{
    using namespace besktop;
    AwakeningDirectorState state;
    const std::vector actors = GridActors(100);
    InitializeAwakeningDirector(state, actors, {0.0, 0.0, 1200.0, 1000.0}, 77u);
    const double firstRatio = static_cast<double>(state.summary.firstWaveCount) / actors.size();
    const double secondRatio = static_cast<double>(state.summary.secondWaveCount) / actors.size();
    Check(firstRatio >= 0.15 && firstRatio <= 0.25, "first wave ratio is 15-25 percent");
    Check(secondRatio >= 0.30 && secondRatio <= 0.40, "second wave ratio is 30-40 percent");
    Check(state.summary.firstWaveCount + state.summary.secondWaveCount +
        state.summary.fallbackWaveCount == actors.size(), "fallback owns every remaining actor");
    Check(state.summary.fallbackWaveCount > 0, "large plan retains fallback wave");
    Check(state.summary.firstStartMaximum < state.summary.secondStartMinimum,
        "first and second waves retain a visible pause");
    Check(state.summary.secondStartMaximum < state.summary.fallbackStartMinimum,
        "second and fallback waves retain a visible pause");
}

void TestSpatialCoverageAndDeterminism()
{
    using namespace besktop;
    std::vector<AwakeningActorInput> actors;
    for (std::size_t quadrant = 0; quadrant < 4; ++quadrant) {
        for (std::size_t local = 0; local < 5; ++local) {
            actors.push_back({
                actors.size(),
                (quadrant % 2 == 0 ? 140.0 : 860.0) + static_cast<double>(local) * 5.0,
                (quadrant < 2 ? 130.0 : 670.0) + static_cast<double>(local) * 4.0,
                48.0,
                static_cast<std::uint32_t>(0x2000u + actors.size() * 17u),
            });
        }
    }
    AwakeningDirectorState first;
    AwakeningDirectorState second;
    const AwakeningBounds bounds{0.0, 0.0, 1000.0, 800.0};
    InitializeAwakeningDirector(first, actors, bounds, 91u);
    InitializeAwakeningDirector(second, actors, bounds, 91u);
    std::set<int> quadrants;
    for (const AwakeningPlanEntry& entry : first.entries) {
        const AwakeningPlanEntry& other = second.entries[entry.actorIndex];
        Check(entry.wave == other.wave && entry.startSeconds == other.startSeconds &&
            entry.proximityDelaySeconds == other.proximityDelaySeconds,
            "same positions and seed reproduce identical plan");
        if (entry.wave != AwakeningWave::First) continue;
        const AwakeningActorInput& actor = actors[entry.actorIndex];
        quadrants.insert((actor.x >= 500.0 ? 1 : 0) + (actor.y >= 400.0 ? 2 : 0));
    }
    Check(quadrants.size() >= 3, "first wave covers multiple desktop regions");

    const std::vector originalActors = actors;
    AwakeningDirectorState ignored;
    InitializeAwakeningDirector(ignored, actors, bounds, 91u);
    Check(std::equal(actors.begin(), actors.end(), originalActors.begin(), [](const auto& a, const auto& b) {
        return a.actorIndex == b.actorIndex && a.seed == b.seed && a.x == b.x && a.y == b.y;
    }), "planning does not consume or change actor roaming seeds");
}

void TestProximityAccelerationAndFallback()
{
    using namespace besktop;
    const std::vector inputs = GridActors(12);
    AwakeningDirectorState state;
    InitializeAwakeningDirector(state, inputs, {0.0, 0.0, 1200.0, 900.0}, 123u);
    const auto sleeper = std::find_if(state.entries.begin(), state.entries.end(), [](const AwakeningPlanEntry& entry) {
        return entry.wave == AwakeningWave::Fallback;
    });
    const auto influencer = std::find_if(state.entries.begin(), state.entries.end(), [](const AwakeningPlanEntry& entry) {
        return entry.wave == AwakeningWave::First;
    });
    Check(sleeper != state.entries.end() && influencer != state.entries.end(), "test plan has first and fallback actors");
    if (sleeper == state.entries.end() || influencer == state.entries.end()) return;

    std::vector<AwakeningActorObservation> observations(inputs.size());
    for (std::size_t index = 0; index < observations.size(); ++index) {
        observations[index] = {index, inputs[index].x, inputs[index].y, inputs[index].extent, false};
    }
    observations[influencer->actorIndex].wandering = true;
    observations[sleeper->actorIndex].x = observations[influencer->actorIndex].x + 40.0;
    observations[sleeper->actorIndex].y = observations[influencer->actorIndex].y;
    const double originalStart = sleeper->startSeconds;
    Check(UpdateAwakeningProximity(state, observations, 2.0), "nearby wanderer accelerates sleeper");
    const double acceleratedStart = GetActorAwakeningStartSeconds(state, sleeper->actorIndex);
    Check(acceleratedStart < originalStart, "proximity only moves start earlier");
    Check(acceleratedStart >= 2.0 + GetAwakeningDirectorTuning().proximityDelayMinimumSeconds &&
        acceleratedStart <= 2.0 + GetAwakeningDirectorTuning().proximityDelayMaximumSeconds,
        "proximity keeps a readable causal delay before awakening");
    Check(acceleratedStart >= GetAwakeningDirectorTuning().desktopPauseSeconds,
        "proximity cannot break desktop pause floor");
    const std::size_t accelerationCount = state.proximityAccelerationCount;
    Check(!UpdateAwakeningProximity(state, observations, 2.2),
        "repeated proximity check does not reroll or accumulate event");
    Check(state.proximityAccelerationCount == accelerationCount,
        "repeated proximity check keeps stable acceleration count");

    AwakeningDirectorState inactiveState;
    InitializeAwakeningDirector(inactiveState, inputs, {0.0, 0.0, 1200.0, 900.0}, 123u);
    observations[influencer->actorIndex].wandering = false;
    Check(!UpdateAwakeningProximity(inactiveState, observations, 2.0),
        "actor without completed limbs cannot accelerate sleepers");
    Check(LatestAwakeningStartSeconds(inactiveState) <=
        GetAwakeningDirectorTuning().fallbackWaveStartMaximumSeconds,
        "fallback wakes every actor without proximity");
    Check(!UpdateAwakeningProximity(inactiveState, observations, 100.0),
        "large delta does not restart already scheduled actors");
}

void TestEcosystemReadiness()
{
    using namespace besktop;
    const std::vector inputs = GridActors(10);
    AwakeningDirectorState state;
    InitializeAwakeningDirector(state, inputs, {0.0, 0.0, 1200.0, 900.0}, 54u);
    std::vector<AwakeningActorObservation> observations(inputs.size());
    for (std::size_t index = 0; index < observations.size(); ++index) {
        observations[index] = {index, inputs[index].x, inputs[index].y, inputs[index].extent, false};
    }
    Check(!IsFirstWaveEcosystemReady(state, observations), "first wave not ready blocks ecosystem");
    for (const AwakeningPlanEntry& entry : state.entries) {
        if (entry.wave == AwakeningWave::First) observations[entry.actorIndex].wandering = true;
    }
    Check(IsFirstWaveEcosystemReady(state, observations),
        "ready first wave and two wanderers allow ecosystem without all actors");
    const auto first = std::find_if(state.entries.begin(), state.entries.end(), [](const AwakeningPlanEntry& entry) {
        return entry.wave == AwakeningWave::First;
    });
    observations[first->actorIndex].wandering = false;
    Check(!IsFirstWaveEcosystemReady(state, observations), "every first-wave actor must finish limb growth");
}

} // namespace

int main()
{
    TestEmptyAndSmallPlans();
    TestWaveRatiosAndFallback();
    TestSpatialCoverageAndDeterminism();
    TestProximityAccelerationAndFallback();
    TestEcosystemReadiness();
    if (failures != 0) return 1;
    std::cout << "besktop_awakening_tests: all checks passed\n";
    return 0;
}
