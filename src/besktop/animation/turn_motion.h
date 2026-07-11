#pragma once

#include "besktop/animation/gait_ik.h"

namespace besktop {

enum class TurnFacing {
    Left = -1,
    Right = 1,
};

struct TurnMotionState {
    TurnFacing currentFacing = TurnFacing::Right;
    TurnFacing desiredFacing = TurnFacing::Right;
    bool turning = false;
    double elapsedSeconds = 0.0;
    double durationSeconds = 0.40;
    double fromYaw = 0.0;
    double toYaw = 0.0;
    double progress = 0.0;
};

struct TurnProjectedPoint {
    double x = 0.0;
    double y = 0.0;
    double depth = 0.0;
};

TurnFacing FacingFromDirection(double direction);
TurnFacing ChooseTurnSafeInitialFacing(
    TurnFacing preferredFacing,
    double capturedCenterX,
    double bodyCenterOffset,
    double iconHalfWidth,
    double minimumX,
    double maximumX,
    double padding = 2.0);
double FacingYaw(TurnFacing facing);
void InitializeTurnMotion(
    TurnMotionState& state,
    TurnFacing facing,
    double durationSeconds = 0.40);
bool RequestTurn(TurnMotionState& state, TurnFacing desiredFacing);
void UpdateTurnMotion(TurnMotionState& state, double deltaSeconds);
double SampleTurnYaw(const TurnMotionState& state);
double SampleTurnEase(double progress);
double BlendTurnLocomotion(double currentWeight, double targetWeight, double deltaSeconds);
GaitVec3 RotateAroundVerticalAxis(const GaitVec3& local, double yaw);
TurnProjectedPoint ProjectTurnPoint(
    const GaitVec3& local,
    double yaw,
    double focalLength);

} // namespace besktop
