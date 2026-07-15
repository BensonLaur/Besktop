#pragma once

#include "besktop/animation/combat_pair.h"

#include <cstdint>
#include <optional>
#include <string_view>

namespace besktop {

enum class EncounterPhase {
    Inactive,
    Notice,
    Approaching,
    Facing,
    Assessing,
    Intent,
    Combat,
    Aftermath,
    Separating,
    Complete,
    Cancelled,
};

enum class EncounterIntent {
    Undecided,
    Combat,
    Yield,
    Bluff,
};

enum class EncounterActorRole {
    Attacker,
    Defender,
};

struct EncounterBounds {
    double left = 0.0;
    double top = 0.0;
    double right = 0.0;
    double bottom = 0.0;
};

struct EncounterReservation {
    double centerX = 0.0;
    double centerY = 0.0;
    double radius = 0.0;
};

struct EncounterPoint {
    double x = 0.0;
    double y = 0.0;
};

struct EncounterAftermathPlan {
    CombatResult result = CombatResult::None;
    double holdSeconds = 0.0;
    double attackerDepartureDelaySeconds = 0.0;
    double defenderDepartureDelaySeconds = 0.0;
    EncounterPoint attackerExit{};
    EncounterPoint defenderExit{};
    bool heavyRootMotionAlreadyApplied = false;
};

struct EncounterPoseSample {
    double bodyRotateX = 0.0;
    double bodyRotateY = 0.0;
    double bodyRotateZ = 0.0;
    double rootOffsetForward = 0.0;
    double rootOffsetY = 0.0;
};

struct EncounterReadiness {
    bool actorsValid = true;
    bool reservationSafe = true;
    bool atStations = false;
    bool aligned = false;
    bool combatComplete = false;
    bool separated = false;
    CombatResult combatResult = CombatResult::None;
};

struct EncounterStep {
    bool phaseChanged = false;
    bool requestCombatStart = false;
    bool completed = false;
    bool cancelled = false;
};

struct EncounterTuning {
    double noticeSeconds = 0.24;
    double assessMinimumSeconds = 0.60;
    double assessMaximumSeconds = 1.00;
    double yieldIntentSeconds = 0.72;
    double bluffIntentSeconds = 0.68;
    double combatWeight = 0.60;
    double yieldWeight = 0.25;
    double bluffWeight = 0.15;
};

struct EncounterState {
    EncounterPhase phase = EncounterPhase::Inactive;
    EncounterIntent intent = EncounterIntent::Undecided;
    double phaseTime = 0.0;
    double assessDuration = 0.0;
    double intentDuration = 0.0;
    std::uint32_t randomState = 0xE11C0A7u;
    bool attackerActsFirst = true;
    bool combatStartRequested = false;
    bool completionEmitted = false;
    bool cancellationEmitted = false;
    EncounterBounds bounds{};
    EncounterReservation reservation{};
    double actorMargin = 0.0;
    EncounterPoint attackerIntentTarget{};
    EncounterPoint defenderIntentTarget{};
    EncounterAftermathPlan aftermath{};
};

const EncounterTuning& GetEncounterTuning();
std::wstring_view EncounterPhaseName(EncounterPhase phase);
std::wstring_view EncounterIntentName(EncounterIntent intent);

void BeginEncounter(
    EncounterState& state,
    std::uint32_t seed,
    const EncounterReservation& reservation,
    const EncounterBounds& bounds,
    double actorMargin,
    EncounterIntent resolvedIntent = EncounterIntent::Undecided,
    std::optional<bool> attackerActsFirst = std::nullopt);

EncounterStep UpdateEncounter(
    EncounterState& state,
    const EncounterReadiness& readiness,
    double deltaSeconds);

void CancelEncounter(EncounterState& state);

EncounterAftermathPlan BuildEncounterAftermathPlan(
    EncounterIntent intent,
    CombatResult result,
    bool attackerActsFirst,
    const EncounterReservation& reservation,
    const EncounterBounds& bounds,
    double actorMargin);

EncounterPoseSample SampleEncounterPose(
    const EncounterState& state,
    EncounterActorRole role);

bool EncounterActorMayDepart(const EncounterState& state, EncounterActorRole role);
bool IsEncounterPointSafe(
    const EncounterPoint& point,
    const EncounterReservation& reservation,
    const EncounterBounds& bounds,
    double actorMargin);

} // namespace besktop
