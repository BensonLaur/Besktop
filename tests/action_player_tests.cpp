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

double Distance(const besktop::GaitVec3& first, const besktop::GaitVec3& second);

void TestParsing()
{
    using namespace besktop;
    Check(ParseActionId(L"lead_straight") == ActionId::LeadStraight, "parse lead_straight");
    Check(ParseActionId(L"layback") == ActionId::Layback, "parse layback");
    Check(ParseActionId(L"light_hit_react") == ActionId::LightHitReact, "parse light_hit_react");
    Check(ParseActionId(L"side_kick") == ActionId::SideKick, "parse side_kick");
    Check(ParseActionId(L"invalid") == ActionId::None, "invalid name falls back");
}

void TestPhaseBoundaries()
{
    using namespace besktop;
    const ActionId actions[] = {
        ActionId::LeadStraight,
        ActionId::Layback,
        ActionId::LightHitReact,
        ActionId::SideKick,
    };
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

besktop::GaitVec3 SideKickFootTarget(
    const besktop::ActionSample& sample,
    bool leadLeg,
    double direction,
    double planeSide)
{
    using namespace besktop;
    const double heading = direction < 0.0 ? -1.0 : 1.0;
    const double sideSign = leadLeg ? heading : -heading;
    const double depthSign = leadLeg ? 1.0 : -1.0;
    const GaitGeometry geometry = BuildGaitGeometry(
        planeSide, planeSide * 0.40, planeSide * 0.41);
    const double forwardOffset = leadLeg ?
        sample.leadFootForwardOffset : sample.rearFootForwardOffset;
    const double lift = leadLeg ? sample.leadFootLift : sample.rearFootLift;
    const double depthOffset = leadLeg ?
        sample.leadFootDepthOffset : sample.rearFootDepthOffset;
    return GaitVec3{
        sideSign * geometry.stride * 0.10 + heading * forwardOffset * planeSide,
        geometry.legDrop - lift * planeSide,
        depthSign * (geometry.footDepth + depthOffset * planeSide),
    };
}

besktop::TwoBoneIkSolution SolveSideKickLeg(
    const besktop::ActionSample& sample,
    bool leadLeg,
    double direction,
    double planeSide)
{
    using namespace besktop;
    const double heading = direction < 0.0 ? -1.0 : 1.0;
    const double depthSign = leadLeg ? 1.0 : -1.0;
    return SolveTwoBoneIk(
        GaitVec3{},
        SideKickFootTarget(sample, leadLeg, direction, planeSide),
        planeSide * 0.40,
        planeSide * 0.41,
        GaitVec3{heading, 0.0, depthSign * 0.18});
}

void TestSideKickGeometryAndEvents()
{
    using namespace besktop;
    constexpr double planeSide = 48.0;
    constexpr double thighLength = planeSide * 0.40;
    constexpr double shinLength = planeSide * 0.41;
    constexpr std::array<double, 6> sampleTimes{0.0, 0.18, 0.32, 0.37, 0.53, 0.72};

    const ActionClip& clip = GetActionClip(ActionId::SideKick);
    Check(std::abs(clip.prepareEnd - 0.20) < 1e-9, "side kick prepare timing");
    Check(std::abs(clip.activeEnd - 0.35) < 1e-9, "side kick active timing");
    Check(std::abs(clip.contactEnd - 0.41) < 1e-9, "side kick contact timing");
    Check(std::abs(clip.recoverEnd - 0.72) < 1e-9, "side kick recover timing");
    Check(std::abs(clip.duration - 0.86) < 1e-9, "side kick total timing");

    ActionPlayer normal;
    normal.Start(ActionId::SideKick);
    for (int frame = 0; frame < 60; ++frame) {
        normal.Update(1.0 / 60.0);
    }
    Check(normal.ConsumeEvents() != 0, "side kick normal playback emits contact");
    Check(normal.ConsumeEvents() == 0, "side kick normal contact is consumed once");

    ActionPlayer skipped;
    skipped.Start(ActionId::SideKick, 1.0, 8.0);
    skipped.Update(0.10);
    Check(skipped.ConsumeEvents() != 0, "side kick large step crosses contact");
    Check(skipped.ConsumeEvents() == 0, "side kick large-step contact is consumed once");
    skipped.Update(0.10);
    Check(skipped.ConsumeEvents() == 0, "side kick contact does not repeat later");

    const GaitVec3 plantedSupport = SolveSideKickLeg(
        SampleAction(clip, 0.0, 1.0), false, 1.0, planeSide).end;
    double maximumSupportDrift = 0.0;
    for (const double time : sampleTimes) {
        const ActionSample rightSample = SampleAction(clip, time, 1.0);
        const ActionSample leftSample = SampleAction(clip, time, -1.0);
        const TwoBoneIkSolution kickLeg = SolveSideKickLeg(rightSample, true, 1.0, planeSide);
        const TwoBoneIkSolution supportLeg = SolveSideKickLeg(rightSample, false, 1.0, planeSide);
        maximumSupportDrift = std::max(
            maximumSupportDrift,
            Distance(plantedSupport, supportLeg.end));

        Check(std::abs(Distance(kickLeg.root, kickLeg.joint) - thighLength) < 1e-6,
            "side kick thigh keeps fixed length");
        Check(std::abs(Distance(kickLeg.joint, kickLeg.end) - shinLength) < 1e-6,
            "side kick shin keeps fixed length");
        Check(std::abs(Distance(supportLeg.root, supportLeg.joint) - thighLength) < 1e-6,
            "side kick support thigh keeps fixed length");
        Check(std::abs(Distance(supportLeg.joint, supportLeg.end) - shinLength) < 1e-6,
            "side kick support shin keeps fixed length");

        const double kickKnee = JointInteriorAngleDegrees(kickLeg);
        const double supportKnee = JointInteriorAngleDegrees(supportLeg);
        Check(kickKnee > 35.0 && kickKnee < 178.0, "side kick knee does not reverse or lock");
        Check(supportKnee >= 165.0 && supportKnee <= 178.0,
            "side kick support knee stays naturally long");

        const GaitVec3 rightTarget = SideKickFootTarget(rightSample, true, 1.0, planeSide);
        const GaitVec3 leftTarget = SideKickFootTarget(leftSample, true, -1.0, planeSide);
        Check(std::abs(rightTarget.x + leftTarget.x) < 1e-9,
            "side kick left and right foot targets mirror horizontally");
        Check(std::abs(rightTarget.y - leftTarget.y) < 1e-9,
            "side kick mirror keeps foot height");
        Check(std::abs(JointInteriorAngleDegrees(kickLeg) -
            JointInteriorAngleDegrees(SolveSideKickLeg(leftSample, true, -1.0, planeSide))) < 1e-9,
            "side kick mirror keeps knee angle");
    }

    const ActionSample prepare = SampleAction(clip, 0.18, 1.0);
    const ActionSample contact = SampleAction(clip, 0.37, 1.0);
    const double prepareKnee = JointInteriorAngleDegrees(
        SolveSideKickLeg(prepare, true, 1.0, planeSide));
    const double contactKnee = JointInteriorAngleDegrees(
        SolveSideKickLeg(contact, true, 1.0, planeSide));
    Check(prepareKnee >= 80.0 && prepareKnee <= 140.0,
        "side kick prepare clearly chambers the knee");
    Check(contactKnee >= 160.0 && contactKnee <= 176.0,
        "side kick contact extends without locking the knee");
    Check(maximumSupportDrift <= planeSide * 0.005,
        "side kick support foot remains planted");

    const ActionSample complete = SampleAction(clip, clip.duration, 1.0);
    Check(std::abs(complete.leadFootForwardOffset) < 1e-9 &&
        std::abs(complete.leadFootLift) < 1e-9 &&
        std::abs(complete.leadFootDepthOffset) < 1e-9 &&
        std::abs(complete.bodyRotateY) < 1e-9 &&
        std::abs(complete.bodyRotateZ) < 1e-9 &&
        complete.handTargetWeight < 1e-9,
        "side kick completes at neutral pose");
    std::cout << "side kick metrics: prepare knee=" << prepareKnee
              << ", contact knee=" << contactKnee
              << ", support drift=" << maximumSupportDrift
              << ", thigh/shin=" << thighLength << "/" << shinLength << '\n';
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
    TestSideKickGeometryAndEvents();
    TestGaitGeometry();
    if (failures == 0) {
        std::cout << "besktop_action_tests: all checks passed\n";
    }
    return failures == 0 ? 0 : 1;
}
