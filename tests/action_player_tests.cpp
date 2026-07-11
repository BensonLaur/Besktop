#include "besktop/animation/action_player.h"
#include "besktop/animation/gait_ik.h"
#include "besktop/animation/turn_motion.h"

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

void TestTurnMotionStateAndGeometry()
{
    using namespace besktop;
    constexpr double pi = 3.14159265358979323846;
    constexpr double planeSide = 48.0;
    constexpr double focalLength = 576.0;
    constexpr double bodyCenterOffset = 27.0;

    Check(ChooseTurnSafeInitialFacing(
        TurnFacing::Right, 30.0, bodyCenterOffset, planeSide * 0.5, 0.0, 320.0) ==
        TurnFacing::Left,
        "left-edge actor starts toward the safe turn arc");
    Check(ChooseTurnSafeInitialFacing(
        TurnFacing::Left, 290.0, bodyCenterOffset, planeSide * 0.5, 0.0, 320.0) ==
        TurnFacing::Right,
        "right-edge actor starts toward the safe turn arc");
    Check(ChooseTurnSafeInitialFacing(
        TurnFacing::Right, 160.0, bodyCenterOffset, planeSide * 0.5, 0.0, 320.0) ==
        TurnFacing::Right,
        "actor with room on both sides keeps its preferred facing");

    TurnMotionState sameDirection;
    InitializeTurnMotion(sameDirection, TurnFacing::Right);
    Check(!RequestTurn(sameDirection, TurnFacing::Right),
        "same facing does not start a turn");
    Check(!sameDirection.turning, "same facing remains stable");

    TurnMotionState rightToLeft;
    InitializeTurnMotion(rightToLeft, TurnFacing::Right);
    Check(RequestTurn(rightToLeft, TurnFacing::Left),
        "opposite facing starts a turn");
    Check(rightToLeft.currentFacing == TurnFacing::Right,
        "turn does not commit facing at start");
    Check(std::abs(SampleTurnYaw(rightToLeft)) < 1e-9,
        "right-facing turn starts at zero yaw");

    double previousProgress = 0.0;
    for (int step = 0; step < 8; ++step) {
        UpdateTurnMotion(rightToLeft, 0.04);
        Check(rightToLeft.progress >= previousProgress &&
            rightToLeft.progress >= 0.0 && rightToLeft.progress <= 1.0,
            "turn progress is monotonic and clamped");
        Check(rightToLeft.currentFacing == TurnFacing::Right,
            "turn keeps current facing until completion");
        previousProgress = rightToLeft.progress;
    }
    Check(std::abs(rightToLeft.progress - 0.8) < 1e-9,
        "turn uses configured duration");
    UpdateTurnMotion(rightToLeft, 1.0);
    Check(!rightToLeft.turning, "large delta completes turn");
    Check(rightToLeft.currentFacing == TurnFacing::Left,
        "turn commits desired facing at completion");
    Check(std::abs(rightToLeft.progress - 1.0) < 1e-9 &&
        std::abs(SampleTurnYaw(rightToLeft) - pi) < 1e-9,
        "completed turn lands exactly on target yaw");

    TurnMotionState mirroredRight;
    TurnMotionState mirroredLeft;
    InitializeTurnMotion(mirroredRight, TurnFacing::Right);
    InitializeTurnMotion(mirroredLeft, TurnFacing::Left);
    RequestTurn(mirroredRight, TurnFacing::Left);
    RequestTurn(mirroredLeft, TurnFacing::Right);
    for (int step = 0; step <= 10; ++step) {
        if (step > 0) {
            UpdateTurnMotion(mirroredRight, 0.04);
            UpdateTurnMotion(mirroredLeft, 0.04);
        }
        Check(std::abs(mirroredRight.progress - mirroredLeft.progress) < 1e-9,
            "left and right turn progress matches");
        Check(std::abs(SampleTurnYaw(mirroredRight) +
            SampleTurnYaw(mirroredLeft) - pi) < 1e-9,
            "left and right yaw curves mirror");
    }

    const auto projectedWidth = [=](double yaw) {
        const TurnProjectedPoint left = ProjectTurnPoint(
            GaitVec3{bodyCenterOffset - planeSide * 0.5, 0.0, 0.0}, yaw, focalLength);
        const TurnProjectedPoint right = ProjectTurnPoint(
            GaitVec3{bodyCenterOffset + planeSide * 0.5, 0.0, 0.0}, yaw, focalLength);
        return std::abs(right.x - left.x);
    };
    const double startWidth = projectedWidth(0.0);
    const double middleWidth = projectedWidth(pi * 0.5);
    const double endWidth = projectedWidth(pi);
    Check(middleWidth < startWidth * 0.05,
        "turn midpoint becomes a narrow plane edge");
    Check(std::abs(endWidth - startWidth) < 1e-9,
        "turn endpoint restores icon width");
    const GaitVec3 localIconCenter{bodyCenterOffset, 0.0, 0.0};
    TurnProjectedPoint previousIconCenter = ProjectTurnPoint(
        localIconCenter, 0.0, focalLength);
    double maximumIconCenterStep = 0.0;
    double middleIconCenterX = 0.0;
    double endIconCenterX = 0.0;
    for (int frame = 0; frame <= 60; ++frame) {
        const double yaw = pi * SampleTurnEase(static_cast<double>(frame) / 60.0);
        const GaitVec3 rotatedCenter = RotateAroundVerticalAxis(localIconCenter, yaw);
        Check(std::abs(std::hypot(rotatedCenter.x, rotatedCenter.z) - bodyCenterOffset) < 1e-9,
            "icon center keeps constant radius around body axis");
        const TurnProjectedPoint projectedCenter = ProjectTurnPoint(
            localIconCenter, yaw, focalLength);
        if (frame > 0) {
            maximumIconCenterStep = std::max(
                maximumIconCenterStep,
                std::hypot(
                    projectedCenter.x - previousIconCenter.x,
                    projectedCenter.y - previousIconCenter.y));
        }
        previousIconCenter = projectedCenter;
        if (frame == 30) {
            middleIconCenterX = projectedCenter.x;
        }
        if (frame == 60) {
            endIconCenterX = projectedCenter.x;
        }
    }
    Check(ProjectTurnPoint(localIconCenter, 0.0, focalLength).x > 0.0 && endIconCenterX < 0.0,
        "icon center finishes on the opposite side of body axis");
    Check(std::abs(middleIconCenterX) < 1e-9,
        "icon center passes body axis near the edge-on midpoint");
    Check(maximumIconCenterStep < planeSide * 0.05,
        "icon center follows a continuous projected arc");

    const GaitVec3 shoulderCenter{0.0, -planeSide * 0.20, 0.0};
    const GaitVec3 hipCenter{0.0, planeSide * 0.68, 0.0};
    const GaitVec3 leftShoulder{0.0, shoulderCenter.y, -planeSide * 0.24};
    const GaitVec3 rightShoulder{0.0, shoulderCenter.y, planeSide * 0.24};
    double maximumAnchorStep = 0.0;
    TurnProjectedPoint previousLeft = ProjectTurnPoint(leftShoulder, 0.0, focalLength);
    TurnProjectedPoint previousRight = ProjectTurnPoint(rightShoulder, 0.0, focalLength);
    double depthExchangeProgress = -1.0;
    double previousDepthDifference =
        ProjectTurnPoint(rightShoulder, 0.0, focalLength).depth -
        ProjectTurnPoint(leftShoulder, 0.0, focalLength).depth;
    for (int frame = 0; frame <= 60; ++frame) {
        const double progress = static_cast<double>(frame) / 60.0;
        const double yaw = pi * SampleTurnEase(progress);
        const TurnProjectedPoint shoulderAxis = ProjectTurnPoint(shoulderCenter, yaw, focalLength);
        const TurnProjectedPoint hipAxis = ProjectTurnPoint(hipCenter, yaw, focalLength);
        const TurnProjectedPoint rootAxis = ProjectTurnPoint(GaitVec3{}, yaw, focalLength);
        Check(std::abs(rootAxis.x) < 1e-9 &&
            std::abs(shoulderAxis.x) < 1e-9 && std::abs(hipAxis.x) < 1e-9,
            "shoulder and hip centers keep a stable vertical axis");

        const TurnProjectedPoint left = ProjectTurnPoint(leftShoulder, yaw, focalLength);
        const TurnProjectedPoint right = ProjectTurnPoint(rightShoulder, yaw, focalLength);
        if (frame > 0) {
            maximumAnchorStep = std::max(maximumAnchorStep,
                std::max(
                    std::hypot(left.x - previousLeft.x, left.y - previousLeft.y),
                    std::hypot(right.x - previousRight.x, right.y - previousRight.y)));
        }
        previousLeft = left;
        previousRight = right;
        const double depthDifference = right.depth - left.depth;
        if (depthExchangeProgress < 0.0 && previousDepthDifference > 0.0 && depthDifference <= 0.0) {
            depthExchangeProgress = progress;
        }
        previousDepthDifference = depthDifference;
    }
    Check(maximumAnchorStep < planeSide * 0.04,
        "turn anchors move continuously without a one-frame side swap");
    Check(depthExchangeProgress >= 0.45 && depthExchangeProgress <= 0.55,
        "front and back limb depth exchanges near turn midpoint");

    const double upperArmLength = planeSide * 0.30;
    const double forearmLength = planeSide * 0.32;
    const TwoBoneIkSolution arm = SolveTwoBoneIk(
        rightShoulder,
        GaitVec3{rightShoulder.x + 4.0, rightShoulder.y + 22.0, rightShoulder.z + 3.0},
        upperArmLength,
        forearmLength,
        GaitVec3{0.0, 1.0, 0.25});
    for (const double yaw : {0.0, pi * 0.25, pi * 0.5, pi * 0.75, pi}) {
        const GaitVec3 root = RotateAroundVerticalAxis(arm.root, yaw);
        const GaitVec3 joint = RotateAroundVerticalAxis(arm.joint, yaw);
        const GaitVec3 end = RotateAroundVerticalAxis(arm.end, yaw);
        Check(std::abs(Distance(root, joint) - upperArmLength) < 1e-6,
            "turn keeps upper arm length");
        Check(std::abs(Distance(joint, end) - forearmLength) < 1e-6,
            "turn keeps forearm length");
    }

    const double thighLength = planeSide * 0.40;
    const double shinLength = planeSide * 0.41;
    const GaitVec3 hip{0.0, planeSide * 0.68, planeSide * 0.22};
    const TwoBoneIkSolution leg = SolveTwoBoneIk(
        hip,
        GaitVec3{hip.x + planeSide * 0.07, hip.y + planeSide * 0.80, hip.z + planeSide * 0.05},
        thighLength,
        shinLength,
        GaitVec3{1.0, 0.0, 0.18});
    for (const double yaw : {0.0, pi * 0.5, pi}) {
        const GaitVec3 root = RotateAroundVerticalAxis(leg.root, yaw);
        const GaitVec3 joint = RotateAroundVerticalAxis(leg.joint, yaw);
        const GaitVec3 end = RotateAroundVerticalAxis(leg.end, yaw);
        Check(std::abs(Distance(root, joint) - thighLength) < 1e-6,
            "turn keeps thigh length");
        Check(std::abs(Distance(joint, end) - shinLength) < 1e-6,
            "turn keeps shin length");
    }

    double locomotionWeight = 1.0;
    for (int step = 0; step < 40; ++step) {
        locomotionWeight = BlendTurnLocomotion(locomotionWeight, 0.0, 1.0 / 60.0);
    }
    Check(locomotionWeight < 0.01, "turn can blend gait out to a planted pose");
    for (int step = 0; step < 50; ++step) {
        locomotionWeight = BlendTurnLocomotion(locomotionWeight, 1.0, 1.0 / 60.0);
    }
    Check(locomotionWeight > 0.99, "gait can resume after turn completion");

    std::cout << "turn metrics: duration=0.4, widths="
              << startWidth << "/" << middleWidth << "/" << endWidth
              << ", icon radius=" << bodyCenterOffset
              << ", icon max step=" << maximumIconCenterStep
              << ", max anchor step=" << maximumAnchorStep
              << ", depth exchange=" << depthExchangeProgress << '\n';
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
    TestTurnMotionStateAndGeometry();
    if (failures == 0) {
        std::cout << "besktop_action_tests: all checks passed\n";
    }
    return failures == 0 ? 0 : 1;
}
