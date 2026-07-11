#include "besktop/animation/gait_ik.h"

#include <algorithm>
#include <cmath>

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kStanceRatio = 0.62;

double Clamp01(double value)
{
    return std::clamp(value, 0.0, 1.0);
}

double SmoothStep(double value)
{
    const double t = Clamp01(value);
    return t * t * (3.0 - 2.0 * t);
}

double Dot(const besktop::GaitVec3& first, const besktop::GaitVec3& second)
{
    return first.x * second.x + first.y * second.y + first.z * second.z;
}

double Length(const besktop::GaitVec3& value)
{
    return std::sqrt(Dot(value, value));
}

besktop::GaitVec3 Scale(const besktop::GaitVec3& value, double scale)
{
    return {value.x * scale, value.y * scale, value.z * scale};
}

besktop::GaitVec3 Add(const besktop::GaitVec3& first, const besktop::GaitVec3& second)
{
    return {first.x + second.x, first.y + second.y, first.z + second.z};
}

besktop::GaitVec3 Subtract(const besktop::GaitVec3& first, const besktop::GaitVec3& second)
{
    return {first.x - second.x, first.y - second.y, first.z - second.z};
}

besktop::GaitVec3 Normalize(const besktop::GaitVec3& value)
{
    const double length = Length(value);
    return length > 1e-9 ? Scale(value, 1.0 / length) : besktop::GaitVec3{};
}

} // namespace

namespace besktop {

GaitGeometry BuildGaitGeometry(double planeSide, double thighLength, double shinLength)
{
    const double safePlaneSide = std::max(8.0, planeSide);
    const double totalLegLength = std::max(2.0, thighLength + shinLength);

    GaitGeometry geometry;
    geometry.stride = std::clamp(safePlaneSide * 0.68, 18.0, 64.0);
    geometry.stepHeight = std::clamp(safePlaneSide * 0.14, 3.0, 10.0);
    geometry.legDrop = totalLegLength * 0.994;
    geometry.footDepth = safePlaneSide * 0.050;
    geometry.cycleTravel = std::max(1.0, geometry.stride * 0.90 / kStanceRatio);
    return geometry;
}

GaitLegSample SampleGaitLeg(
    double walkPhase,
    double phaseOffset,
    const GaitGeometry& geometry,
    double locomotionWeight)
{
    GaitLegSample sample;
    sample.phase = WrapGaitPhase(walkPhase + phaseOffset);
    sample.stance = sample.phase < kStanceRatio;

    double animatedForward = 0.0;
    double animatedLift = 0.0;
    if (sample.stance) {
        const double t = sample.phase / kStanceRatio;
        animatedForward = geometry.stride * (0.45 - 0.90 * t);
        sample.plantWeight = 1.0;
    } else {
        const double swingT = (sample.phase - kStanceRatio) / (1.0 - kStanceRatio);
        const double smoothSwing = SmoothStep(swingT);
        animatedForward = geometry.stride * (-0.45 + 0.90 * smoothSwing);
        animatedLift = std::sin(swingT * kPi) * geometry.stepHeight;
        sample.plantWeight = 0.0;
    }

    const double weight = Clamp01(locomotionWeight);
    sample.footForward = animatedForward * weight;
    sample.footLift = animatedLift * weight;
    sample.plantWeight = (sample.plantWeight * weight) + (1.0 - weight);
    sample.stance = weight < 0.01 || sample.stance;
    return sample;
}

TwoBoneIkSolution SolveTwoBoneIk(
    const GaitVec3& root,
    const GaitVec3& target,
    double firstLength,
    double secondLength,
    const GaitVec3& bendHint)
{
    const double safeFirstLength = std::max(0.5, firstLength);
    const double safeSecondLength = std::max(0.5, secondLength);
    GaitVec3 delta = Subtract(target, root);
    double distance = Length(delta);
    GaitVec3 axis = distance > 1e-9 ? Scale(delta, 1.0 / distance) : GaitVec3{0.0, 1.0, 0.0};

    const double reachSlack = std::max(0.05, (safeFirstLength + safeSecondLength) * 0.003);
    const double minReach = std::max(0.05, std::abs(safeFirstLength - safeSecondLength) + reachSlack);
    const double maxReach = std::max(minReach + 0.05, safeFirstLength + safeSecondLength - reachSlack);
    const double clampedDistance = std::clamp(distance, minReach, maxReach);
    const GaitVec3 end = Add(root, Scale(axis, clampedDistance));

    const double base = std::clamp(
        ((safeFirstLength * safeFirstLength) - (safeSecondLength * safeSecondLength) +
            (clampedDistance * clampedDistance)) /
            (2.0 * clampedDistance),
        0.0,
        safeFirstLength);
    const double height = std::sqrt(std::max(0.0, safeFirstLength * safeFirstLength - base * base));

    GaitVec3 perpendicular = Subtract(bendHint, Scale(axis, Dot(bendHint, axis)));
    if (Length(perpendicular) < 1e-6) {
        const GaitVec3 fallback = std::abs(axis.z) < 0.9 ? GaitVec3{0.0, 0.0, 1.0} : GaitVec3{1.0, 0.0, 0.0};
        perpendicular = Subtract(fallback, Scale(axis, Dot(fallback, axis)));
    }
    perpendicular = Normalize(perpendicular);

    TwoBoneIkSolution solution;
    solution.root = root;
    solution.end = end;
    solution.joint = Add(Add(root, Scale(axis, base)), Scale(perpendicular, height));
    return solution;
}

double JointInteriorAngleDegrees(const TwoBoneIkSolution& solution)
{
    const GaitVec3 toRoot = Subtract(solution.root, solution.joint);
    const GaitVec3 toEnd = Subtract(solution.end, solution.joint);
    return VectorAngleDegrees(toRoot, toEnd);
}

double VectorAngleDegrees(const GaitVec3& first, const GaitVec3& second)
{
    const double denominator = Length(first) * Length(second);
    if (denominator <= 1e-9) {
        return 0.0;
    }
    const double cosine = std::clamp(Dot(first, second) / denominator, -1.0, 1.0);
    return std::acos(cosine) * 180.0 / kPi;
}

double WrapGaitPhase(double value)
{
    double wrapped = std::fmod(value, 1.0);
    if (wrapped < 0.0) {
        wrapped += 1.0;
    }
    return wrapped;
}

} // namespace besktop
