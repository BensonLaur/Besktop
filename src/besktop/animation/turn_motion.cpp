#include "besktop/animation/turn_motion.h"

#include <algorithm>
#include <cmath>

namespace {

constexpr double kPi = 3.14159265358979323846;

double Clamp01(double value)
{
    return std::clamp(value, 0.0, 1.0);
}

} // namespace

namespace besktop {

TurnFacing FacingFromDirection(double direction)
{
    return direction < 0.0 ? TurnFacing::Left : TurnFacing::Right;
}

TurnActorGeometry BuildTurnActorGeometry(double planeWidth, double planeHeight)
{
    TurnActorGeometry geometry;
    geometry.planeWidth = std::max(8.0, std::isfinite(planeWidth) ? planeWidth : 48.0);
    geometry.planeHeight = std::max(8.0, std::isfinite(planeHeight) ? planeHeight : 48.0);
    geometry.planeSide = std::max(24.0, std::max(geometry.planeWidth, geometry.planeHeight));
    geometry.limbWidth = std::clamp(geometry.planeSide * 0.072, 5.0, 8.5);
    geometry.visibleMargin = std::clamp(geometry.planeSide * 0.08, 4.0, 7.0);
    geometry.bodyAxisGap = (geometry.limbWidth * 0.5) + geometry.visibleMargin;
    geometry.bodyCenterOffset = (geometry.planeWidth * 0.5) + geometry.bodyAxisGap;
    geometry.maximumHorizontalExtent = geometry.bodyCenterOffset + (geometry.planeWidth * 0.5);
    return geometry;
}

TurnFacing ChooseTurnSafeInitialFacing(
    TurnFacing preferredFacing,
    double capturedCenterX,
    double localIconCenterOffset,
    double iconHalfWidth,
    double minimumX,
    double maximumX,
    double padding)
{
    const double safeHalfWidth = std::max(0.0, iconHalfWidth);
    const double safePadding = std::max(0.0, padding);
    const double rightTurnCenter = capturedCenterX - (2.0 * localIconCenterOffset);
    const double leftTurnCenter = capturedCenterX + (2.0 * localIconCenterOffset);
    const bool rightFacingTurnFits =
        rightTurnCenter - safeHalfWidth >= minimumX + safePadding &&
        rightTurnCenter + safeHalfWidth <= maximumX - safePadding;
    const bool leftFacingTurnFits =
        leftTurnCenter - safeHalfWidth >= minimumX + safePadding &&
        leftTurnCenter + safeHalfWidth <= maximumX - safePadding;

    if (preferredFacing == TurnFacing::Right && !rightFacingTurnFits && leftFacingTurnFits) {
        return TurnFacing::Left;
    }
    if (preferredFacing == TurnFacing::Left && !leftFacingTurnFits && rightFacingTurnFits) {
        return TurnFacing::Right;
    }
    return preferredFacing;
}

double FacingYaw(TurnFacing facing)
{
    return facing == TurnFacing::Left ? kPi : 0.0;
}

void InitializeTurnMotion(TurnMotionState& state, TurnFacing facing, double durationSeconds)
{
    state = {};
    state.currentFacing = facing;
    state.desiredFacing = facing;
    state.durationSeconds = std::isfinite(durationSeconds) ?
        std::clamp(durationSeconds, 0.10, 2.0) : 0.40;
    state.fromYaw = FacingYaw(facing);
    state.toYaw = state.fromYaw;
}

bool RequestTurn(TurnMotionState& state, TurnFacing desiredFacing)
{
    if (state.turning) {
        return false;
    }

    state.desiredFacing = desiredFacing;
    if (desiredFacing == state.currentFacing) {
        return false;
    }

    state.turning = true;
    state.elapsedSeconds = 0.0;
    state.progress = 0.0;
    state.fromYaw = FacingYaw(state.currentFacing);
    state.toYaw = FacingYaw(desiredFacing);
    return true;
}

void UpdateTurnMotion(TurnMotionState& state, double deltaSeconds)
{
    if (!state.turning || !std::isfinite(deltaSeconds) || deltaSeconds <= 0.0) {
        return;
    }

    state.elapsedSeconds = std::min(
        state.durationSeconds,
        state.elapsedSeconds + deltaSeconds);
    state.progress = Clamp01(state.elapsedSeconds / state.durationSeconds);
    if (state.progress >= 1.0) {
        state.currentFacing = state.desiredFacing;
        state.turning = false;
        state.fromYaw = state.toYaw;
    }
}

double SampleTurnYaw(const TurnMotionState& state)
{
    if (!state.turning) {
        return FacingYaw(state.currentFacing);
    }
    const double eased = SampleTurnEase(state.progress);
    return state.fromYaw + (state.toYaw - state.fromYaw) * eased;
}

double SampleTurnEase(double progress)
{
    const double t = Clamp01(progress);
    return t * t * (3.0 - 2.0 * t);
}

double BlendTurnLocomotion(double currentWeight, double targetWeight, double deltaSeconds)
{
    const double current = Clamp01(currentWeight);
    const double target = Clamp01(targetWeight);
    if (!std::isfinite(deltaSeconds) || deltaSeconds <= 0.0) {
        return current;
    }
    const double blend = 1.0 - std::exp(-deltaSeconds * 10.0);
    const double result = current + (target - current) * blend;
    if (target <= 0.0 && result < 0.001) {
        return 0.0;
    }
    if (target >= 1.0 && result > 0.999) {
        return 1.0;
    }
    return Clamp01(result);
}

double SampleObservationOrbitYaw(double elapsedSeconds, double durationSeconds)
{
    if (!std::isfinite(elapsedSeconds) || !std::isfinite(durationSeconds) ||
        elapsedSeconds <= 0.0 || durationSeconds <= 0.0) {
        return 0.0;
    }
    const double wrappedSeconds = std::fmod(elapsedSeconds, durationSeconds);
    return wrappedSeconds * (2.0 * kPi / durationSeconds);
}

GaitVec3 RotateAroundVerticalAxis(const GaitVec3& local, double yaw)
{
    const double cosine = std::cos(yaw);
    const double sine = std::sin(yaw);
    return GaitVec3{
        local.x * cosine + local.z * sine,
        local.y,
        -local.x * sine + local.z * cosine,
    };
}

TurnProjectedPoint ProjectTurnPoint(const GaitVec3& local, double yaw, double focalLength)
{
    return ProjectTurnPointWithRotation(local, 0.0, yaw, 0.0, focalLength);
}

TurnProjectedPoint ProjectTurnPointWithRotation(
    const GaitVec3& local,
    double rotateX,
    double rotateY,
    double rotateZ,
    double focalLength)
{
    const GaitVec3 afterY = RotateAroundVerticalAxis(local, rotateY);
    const double cosineX = std::cos(rotateX);
    const double sineX = std::sin(rotateX);
    const double yAfterX = local.y * cosineX - afterY.z * sineX;
    const double zAfterX = local.y * sineX + afterY.z * cosineX;
    const double cosineZ = std::cos(rotateZ);
    const double sineZ = std::sin(rotateZ);
    const GaitVec3 rotated{
        afterY.x * cosineZ - yAfterX * sineZ,
        afterY.x * sineZ + yAfterX * cosineZ,
        zAfterX,
    };
    const double safeFocalLength = std::max(80.0, focalLength);
    const double denominator = std::max(40.0, safeFocalLength - rotated.z);
    const double perspective = safeFocalLength / denominator;
    return TurnProjectedPoint{
        rotated.x * perspective,
        rotated.y * perspective,
        rotated.z,
    };
}

} // namespace besktop
