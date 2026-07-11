#pragma once

namespace besktop {

struct GaitVec3 {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

struct TwoBoneIkSolution {
    GaitVec3 root{};
    GaitVec3 joint{};
    GaitVec3 end{};
};

struct GaitGeometry {
    double stride = 0.0;
    double stepHeight = 0.0;
    double legDrop = 0.0;
    double footDepth = 0.0;
    double cycleTravel = 1.0;
};

struct GaitLegSample {
    double phase = 0.0;
    double footForward = 0.0;
    double footLift = 0.0;
    double plantWeight = 1.0;
    bool stance = true;
};

GaitGeometry BuildGaitGeometry(
    double planeSide,
    double thighLength,
    double shinLength);
GaitLegSample SampleGaitLeg(
    double walkPhase,
    double phaseOffset,
    const GaitGeometry& geometry,
    double locomotionWeight);
TwoBoneIkSolution SolveTwoBoneIk(
    const GaitVec3& root,
    const GaitVec3& target,
    double firstLength,
    double secondLength,
    const GaitVec3& bendHint);
double JointInteriorAngleDegrees(const TwoBoneIkSolution& solution);
double VectorAngleDegrees(const GaitVec3& first, const GaitVec3& second);
double WrapGaitPhase(double value);

} // namespace besktop
