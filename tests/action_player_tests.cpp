#include "besktop/animation/action_player.h"
#include "besktop/animation/gait_ik.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <string_view>

namespace {

int failures = 0;

void Check(bool condition, std::string_view message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

void TestParsing()
{
    using namespace besktop;
    Check(ParseActionId(L"lead_straight") == ActionId::LeadStraight, "parse lead_straight");
    Check(ParseActionId(L"layback") == ActionId::Layback, "parse layback");
    Check(ParseActionId(L"light_hit_react") == ActionId::LightHitReact, "parse light_hit_react");
    Check(ParseActionId(L"invalid") == ActionId::None, "invalid name falls back");
}

void TestPhaseBoundaries()
{
    using namespace besktop;
    const ActionId actions[] = {ActionId::LeadStraight, ActionId::Layback, ActionId::LightHitReact};
    for (const ActionId action : actions) {
        const ActionClip& clip = GetActionClip(action);
        Check(ActionPhaseAt(clip, 0.0) == ActionPhase::Prepare, "prepare phase");
        Check(ActionPhaseAt(clip, clip.prepareEnd) == ActionPhase::Active, "active boundary");
        Check(ActionPhaseAt(clip, clip.activeEnd) == ActionPhase::Contact, "contact boundary");
        Check(ActionPhaseAt(clip, clip.contactEnd) == ActionPhase::Recover, "recover boundary");
        Check(ActionPhaseAt(clip, clip.recoverEnd) == ActionPhase::Complete, "complete blend boundary");
        Check(ActionPhaseAt(clip, clip.duration) == ActionPhase::Complete, "duration complete");
    }
}

void TestSkippedContactIsConsumedOnce()
{
    using namespace besktop;
    ActionPlayer player;
    player.Start(ActionId::LeadStraight);
    player.Update(0.60);
    Check(player.ConsumeEvents() != 0, "large delta crosses contact");
    Check(player.ConsumeEvents() == 0, "contact consumed once");
    player.Update(0.20);
    Check(player.ConsumeEvents() == 0, "later update does not repeat contact");
    Check(player.IsComplete(), "action completes");

    player.Start(ActionId::LeadStraight, 1.0, 8.0);
    player.Update(0.10);
    Check(player.ConsumeEvents() != 0, "8x playback does not skip contact");
    Check(player.ConsumeEvents() == 0, "8x contact remains single-consume");
}

void TestLoopCanEmitAgain()
{
    using namespace besktop;
    ActionPlayer player;
    player.Start(ActionId::Layback);
    player.Update(1.0);
    Check(player.ConsumeEvents() != 0, "first loop emits contact marker");
    player.Start(ActionId::Layback);
    player.Update(1.0);
    Check(player.ConsumeEvents() != 0, "new loop emits contact marker again");
}

void TestMirroringKeepsTimeline()
{
    using namespace besktop;
    ActionPlayer right;
    ActionPlayer left;
    right.Start(ActionId::LightHitReact, 1.0);
    left.Start(ActionId::LightHitReact, -1.0);
    right.Update(0.16);
    left.Update(0.16);
    Check(right.State().phase == left.State().phase, "mirror phase matches");
    Check(std::abs(right.State().localTimeSeconds - left.State().localTimeSeconds) < 1e-9, "mirror time matches");
    Check(right.ConsumeEvents() == left.ConsumeEvents(), "mirror event time matches");
    const ActionSample rightSample = right.Sample();
    const ActionSample leftSample = left.Sample();
    Check(std::abs(rightSample.bodyRotateZ + leftSample.bodyRotateZ) < 1e-9, "mirror body rotation");
}

double Distance(const besktop::GaitVec3& first, const besktop::GaitVec3& second)
{
    const double dx = first.x - second.x;
    const double dy = first.y - second.y;
    const double dz = first.z - second.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

besktop::TwoBoneIkSolution SolveLeg(
    const besktop::GaitLegSample& sample,
    const besktop::GaitGeometry& geometry,
    double heading,
    double depthSign,
    double thighLength,
    double shinLength)
{
    using namespace besktop;
    const GaitVec3 root{0.0, 0.0, depthSign * 10.56};
    const GaitVec3 target{
        heading * sample.footForward,
        geometry.legDrop - sample.footLift,
        root.z + depthSign * geometry.footDepth,
    };
    return SolveTwoBoneIk(
        root,
        target,
        thighLength,
        shinLength,
        GaitVec3{heading, 0.0, depthSign * 0.18});
}

void TestGaitGeometry()
{
    using namespace besktop;
    constexpr double planeSide = 48.0;
    constexpr double thighLength = planeSide * 0.40;
    constexpr double shinLength = planeSide * 0.41;
    const GaitGeometry geometry = BuildGaitGeometry(planeSide, thighLength, shinLength);

    const GaitLegSample standing = SampleGaitLeg(0.25, 0.0, geometry, 0.0);
    Check(std::abs(standing.footForward) < 1e-9, "standing has neutral forward target");
    Check(std::abs(standing.footLift) < 1e-9, "standing has neutral foot height");
    const TwoBoneIkSolution standingLeg = SolveLeg(
        standing, geometry, 1.0, 1.0, thighLength, shinLength);
    const double standingKnee = JointInteriorAngleDegrees(standingLeg);
    Check(standingKnee >= 165.0 && standingKnee <= 178.0, "standing knee is long and slightly unlocked");

    constexpr std::array<double, 8> phases{0.0, 0.125, 0.25, 0.375, 0.50, 0.625, 0.75, 0.875};
    double minimumKnee = 180.0;
    double maximumKnee = 0.0;
    double minimumStanceKnee = 180.0;
    double maximumStanceKnee = 0.0;
    double maximumThighSeparation = 0.0;
    for (const double phase : phases) {
        const GaitLegSample right = SampleGaitLeg(phase, 0.0, geometry, 1.0);
        const GaitLegSample left = SampleGaitLeg(phase, 0.5, geometry, 1.0);
        const TwoBoneIkSolution rightLeg = SolveLeg(
            right, geometry, 1.0, 1.0, thighLength, shinLength);
        const TwoBoneIkSolution leftLeg = SolveLeg(
            left, geometry, 1.0, -1.0, thighLength, shinLength);
        const double rightKnee = JointInteriorAngleDegrees(rightLeg);
        const double leftKnee = JointInteriorAngleDegrees(leftLeg);
        minimumKnee = std::min(minimumKnee, std::min(rightKnee, leftKnee));
        maximumKnee = std::max(maximumKnee, std::max(rightKnee, leftKnee));
        if (right.stance) {
            minimumStanceKnee = std::min(minimumStanceKnee, rightKnee);
            maximumStanceKnee = std::max(maximumStanceKnee, rightKnee);
        }
        if (left.stance) {
            minimumStanceKnee = std::min(minimumStanceKnee, leftKnee);
            maximumStanceKnee = std::max(maximumStanceKnee, leftKnee);
        }
        Check(std::max(rightKnee, leftKnee) >= 165.0,
            "at least one leg remains a long support leg");

        const GaitVec3 rightThigh{
            rightLeg.joint.x - rightLeg.root.x,
            rightLeg.joint.y - rightLeg.root.y,
            rightLeg.joint.z - rightLeg.root.z,
        };
        const GaitVec3 leftThigh{
            leftLeg.joint.x - leftLeg.root.x,
            leftLeg.joint.y - leftLeg.root.y,
            leftLeg.joint.z - leftLeg.root.z,
        };
        maximumThighSeparation = std::max(
            maximumThighSeparation,
            VectorAngleDegrees(rightThigh, leftThigh));

        Check(std::abs(Distance(rightLeg.root, rightLeg.joint) - thighLength) < 1e-6,
            "right thigh keeps fixed length");
        Check(std::abs(Distance(rightLeg.joint, rightLeg.end) - shinLength) < 1e-6,
            "right shin keeps fixed length");
        Check(std::abs(Distance(leftLeg.root, leftLeg.joint) - thighLength) < 1e-6,
            "left thigh keeps fixed length");
        Check(std::abs(Distance(leftLeg.joint, leftLeg.end) - shinLength) < 1e-6,
            "left shin keeps fixed length");

        const GaitLegSample mirroredRight = SampleGaitLeg(phase, 0.0, geometry, 1.0);
        const TwoBoneIkSolution mirroredLeg = SolveLeg(
            mirroredRight, geometry, -1.0, 1.0, thighLength, shinLength);
        Check(std::abs(rightKnee - JointInteriorAngleDegrees(mirroredLeg)) < 1e-9,
            "left and right headings mirror knee angle");
        Check(std::abs(rightLeg.end.x + mirroredLeg.end.x) < 1e-9,
            "left and right headings mirror foot target");
    }

    const GaitLegSample earlySwing = SampleGaitLeg(0.715, 0.0, geometry, 1.0);
    const GaitLegSample middleSwing = SampleGaitLeg(0.81, 0.0, geometry, 1.0);
    const GaitLegSample lateSwing = SampleGaitLeg(0.975, 0.0, geometry, 1.0);
    const double earlySwingKnee = JointInteriorAngleDegrees(
        SolveLeg(earlySwing, geometry, 1.0, 1.0, thighLength, shinLength));
    const double middleSwingKnee = JointInteriorAngleDegrees(
        SolveLeg(middleSwing, geometry, 1.0, 1.0, thighLength, shinLength));
    const double lateSwingKnee = JointInteriorAngleDegrees(
        SolveLeg(lateSwing, geometry, 1.0, 1.0, thighLength, shinLength));
    minimumKnee = std::min(minimumKnee, std::min(earlySwingKnee, middleSwingKnee));
    maximumKnee = std::max(maximumKnee, lateSwingKnee);

    Check(earlySwingKnee >= 100.0 && earlySwingKnee <= 140.0,
        "early swing bends the knee clearly");
    Check(middleSwingKnee >= 100.0 && middleSwingKnee <= 140.0,
        "middle swing keeps useful foot clearance");
    Check(lateSwingKnee >= 165.0 && lateSwingKnee <= 178.0,
        "late swing extends the knee before landing");
    Check(minimumStanceKnee >= 165.0 && maximumStanceKnee <= 178.0,
        "stance knee remains long through sampled support phases");
    Check(maximumKnee <= 178.0, "walking knee remains slightly unlocked");
    Check(maximumThighSeparation >= 35.0 && maximumThighSeparation <= 50.0,
        "walking thighs have a readable front-back split");
    std::cout << "gait metrics: standing knee=" << standingKnee
              << ", walking knee=" << minimumKnee << ".." << maximumKnee
              << ", stance knee=" << minimumStanceKnee << ".." << maximumStanceKnee
              << ", swing knee=" << earlySwingKnee << "/" << middleSwingKnee
              << "/" << lateSwingKnee
              << ", max thigh separation=" << maximumThighSeparation << '\n';
}

} // namespace

int main()
{
    TestParsing();
    TestPhaseBoundaries();
    TestSkippedContactIsConsumedOnce();
    TestLoopCanEmitAgain();
    TestMirroringKeepsTimeline();
    TestGaitGeometry();
    if (failures == 0) {
        std::cout << "besktop_action_tests: all checks passed\n";
    }
    return failures == 0 ? 0 : 1;
}
