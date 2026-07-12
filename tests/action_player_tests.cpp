#include "besktop/animation/action_player.h"
#include "besktop/animation/gait_ik.h"
#include "besktop/animation/turn_motion.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <string_view>
#include <utility>

namespace {

int failures = 0;
constexpr double kPi = 3.14159265358979323846;

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
    constexpr std::pair<std::wstring_view, ActionId> actions[] = {
        {L"lead_straight", ActionId::LeadStraight},
        {L"rear_straight", ActionId::RearStraight},
        {L"uppercut", ActionId::Uppercut},
        {L"hook", ActionId::Hook},
        {L"swing_punch", ActionId::SwingPunch},
        {L"layback", ActionId::Layback},
        {L"slip_left", ActionId::SlipLeft},
        {L"slip_right", ActionId::SlipRight},
        {L"parry", ActionId::Parry},
        {L"front_kick", ActionId::FrontKick},
        {L"side_kick", ActionId::SideKick},
        {L"roundhouse_kick", ActionId::RoundhouseKick},
        {L"spinning_back_kick", ActionId::SpinningBackKick},
        {L"light_hit_react", ActionId::LightHitReact},
        {L"heavy_stagger", ActionId::HeavyStagger},
        {L"whiff_recovery", ActionId::WhiffRecovery},
    };
    for (const auto& [name, id] : actions) {
        Check(ParseActionId(name) == id, "public action name parses");
        Check(ActionIdName(id) == name, "action name round trips");
    }
    Check(ParseActionId(L"invalid") == ActionId::None, "invalid name falls back");
    Check(ActionIdName(ActionId::None) == L"none", "none has a stable name");
}

void TestPhaseBoundaries()
{
    using namespace besktop;
    const ActionId actions[] = {
        ActionId::SwingPunch,
        ActionId::LeadStraight,
        ActionId::RearStraight,
        ActionId::Hook,
        ActionId::Uppercut,
        ActionId::FrontKick,
        ActionId::RoundhouseKick,
        ActionId::SpinningBackKick,
        ActionId::Layback,
        ActionId::SlipLeft,
        ActionId::SlipRight,
        ActionId::Parry,
        ActionId::LightHitReact,
        ActionId::SideKick,
        ActionId::HeavyStagger,
        ActionId::WhiffRecovery,
    };
    for (const ActionId action : actions) {
        const ActionClip& clip = GetActionClip(action);
        Check(ActionPhaseAt(clip, 0.0) == ActionPhase::Prepare, "prepare phase");
        Check(ActionPhaseAt(clip, clip.prepareEnd) == ActionPhase::Active, "active boundary");
        Check(ActionPhaseAt(clip, clip.activeEnd) == ActionPhase::Contact, "contact boundary");
        Check(ActionPhaseAt(clip, clip.contactEnd) == ActionPhase::Recover, "recover boundary");
        Check(ActionPhaseAt(clip, clip.recoverEnd) == ActionPhase::Complete, "complete blend boundary");
        Check(ActionPhaseAt(clip, clip.duration) == ActionPhase::Complete, "duration complete");
        Check(clip.prepareEnd > 0.0 &&
            clip.prepareEnd < clip.activeEnd &&
            clip.activeEnd < clip.contactEnd &&
            clip.contactEnd < clip.recoverEnd &&
            clip.recoverEnd < clip.duration,
            "phase boundaries are strictly monotonic");
        const ActionSample complete = SampleAction(clip, clip.duration, 1.0);
        Check(std::abs(complete.bodyRotateX) < 1e-9 &&
            std::abs(complete.bodyRotateY) < 1e-9 &&
            std::abs(complete.bodyRotateZ) < 1e-9 &&
            std::abs(complete.rootOffsetForward) < 1e-9 &&
            std::abs(complete.rootOffsetLateral) < 1e-9 &&
            std::abs(complete.rootOffsetY) < 1e-9 &&
            complete.handTargetWeight < 1e-9 &&
            complete.footTargetWeight < 1e-9,
            "completed action returns channels to neutral");
    }
}

void TestSkippedContactIsConsumedOnce()
{
    using namespace besktop;
    ActionPlayer player;
    const ActionId attacks[] = {
        ActionId::LeadStraight,
        ActionId::RearStraight,
        ActionId::Uppercut,
        ActionId::Hook,
        ActionId::SwingPunch,
        ActionId::FrontKick,
        ActionId::SideKick,
        ActionId::RoundhouseKick,
        ActionId::SpinningBackKick,
    };
    for (const ActionId attack : attacks) {
        const ActionClip& clip = GetActionClip(attack);
        Check(clip.eventCount == 1, "attack owns one contact event");
        Check(clip.events[0].timeSeconds >= clip.activeEnd &&
            clip.events[0].timeSeconds < clip.contactEnd,
            "contact event lies inside contact phase");
        player.Start(attack);
        while (!player.IsComplete()) {
            player.Update(1.0 / 60.0);
        }
        Check(player.ConsumeEvents() != 0, "normal playback emits attack contact");
        Check(player.ConsumeEvents() == 0, "normal attack contact is consumed once");
        player.Start(attack);
        player.Update(clip.duration);
        Check(player.ConsumeEvents() != 0, "large delta crosses attack contact");
        Check(player.ConsumeEvents() == 0, "attack contact consumed once");
        player.Update(0.20);
        Check(player.ConsumeEvents() == 0, "later update does not repeat contact");
        Check(player.IsComplete(), "attack completes");

        player.Start(attack, 1.0, 8.0);
        player.Update(clip.duration);
        Check(player.ConsumeEvents() != 0, "8x playback does not skip attack contact");
        Check(player.ConsumeEvents() == 0, "8x attack contact remains single-consume");
    }

    const ActionId nonAttacks[] = {
        ActionId::Layback,
        ActionId::SlipLeft,
        ActionId::SlipRight,
        ActionId::Parry,
        ActionId::LightHitReact,
        ActionId::HeavyStagger,
        ActionId::WhiffRecovery,
    };
    for (const ActionId action : nonAttacks) {
        const ActionClip& clip = GetActionClip(action);
        Check(clip.eventCount == 0, "defense and feedback actions do not emit contact");
        player.Start(action, 1.0, 8.0);
        player.Update(clip.duration);
        Check(player.ConsumeEvents() == 0, "non-attack remains contact-free at large delta");
    }
}

void TestLoopCanEmitAgain()
{
    using namespace besktop;
    ActionPlayer player;
    player.Start(ActionId::LeadStraight);
    player.Update(1.0);
    Check(player.ConsumeEvents() != 0, "first loop emits contact marker");
    player.Start(ActionId::LeadStraight);
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
    GaitVec3 target = SideKickFootTarget(sample, leadLeg, direction, planeSide);
    GaitVec3 bendHint{heading, 0.0, depthSign * 0.18};
    const double rootCompensation = std::clamp(
        sample.footTargetRootCompensationWeight, 0.0, 1.0);
    target.x -= sample.rootOffsetForward * planeSide * rootCompensation;
    target.y -= sample.rootOffsetY * planeSide * rootCompensation;
    const double lowerBodyYaw =
        (sample.bodyRotateY * sample.lowerBodyActionRotationWeight) +
        sample.lowerBodyRotateY;
    const double compensation = std::clamp(
        sample.footTargetYawCompensationWeight, 0.0, 1.0);
    if (compensation > 0.0) {
        const GaitVec3 compensatedTarget = RotateAroundVerticalAxis(target, -lowerBodyYaw);
        const GaitVec3 compensatedHint = RotateAroundVerticalAxis(bendHint, -lowerBodyYaw);
        target = GaitVec3{
            target.x + (compensatedTarget.x - target.x) * compensation,
            target.y + (compensatedTarget.y - target.y) * compensation,
            target.z + (compensatedTarget.z - target.z) * compensation,
        };
        bendHint = GaitVec3{
            bendHint.x + (compensatedHint.x - bendHint.x) * compensation,
            bendHint.y + (compensatedHint.y - bendHint.y) * compensation,
            bendHint.z + (compensatedHint.z - bendHint.z) * compensation,
        };
    }
    return SolveTwoBoneIk(
        GaitVec3{},
        target,
        planeSide * 0.40,
        planeSide * 0.41,
        bendHint);
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
        const GaitVec3 supportFootInActionSpace = RotateAroundVerticalAxis(
            supportLeg.end, rightSample.lowerBodyRotateY);
        maximumSupportDrift = std::max(
            maximumSupportDrift,
            Distance(plantedSupport, supportFootInActionSpace));

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
    const ActionSample rechamber = SampleAction(clip, 0.52, 1.0);
    const double prepareKnee = JointInteriorAngleDegrees(
        SolveSideKickLeg(prepare, true, 1.0, planeSide));
    const double contactKnee = JointInteriorAngleDegrees(
        SolveSideKickLeg(contact, true, 1.0, planeSide));
    const double rechamberKnee = JointInteriorAngleDegrees(
        SolveSideKickLeg(rechamber, true, 1.0, planeSide));
    Check(prepareKnee >= 80.0 && prepareKnee <= 140.0,
        "side kick prepare clearly chambers the knee");
    Check(contactKnee >= 160.0 && contactKnee <= 176.0,
        "side kick contact extends without locking the knee");
    Check(rechamberKnee >= 80.0 && rechamberKnee <= 140.0,
        "side kick folds back into its chamber before landing");
    Check(std::abs(contact.bodyRotateY) * 180.0 / kPi >= 25.0 &&
        std::abs(contact.bodyRotateZ) * 180.0 / kPi >= 18.0 &&
        contact.leadFootLift >= 0.49,
        "side kick presents a higher side-on silhouette than front kick");
    Check(std::abs(prepare.bodyRotateY) * 180.0 / kPi >= 70.0 &&
        std::abs(contact.bodyRotateY - contact.lowerBodyRotateY) < 1e-9 &&
        contact.footTargetYawCompensationWeight > 0.99,
        "side kick turns shoulders and pelvis together before extension");
    Check(contact.upperBodyOffsetForward <= -0.095 &&
        contact.upperBodyOffsetY >= 0.17,
        "side kick shifts only the upper body backward and down for balance");
    Check(contact.leadHandForward >= 0.17 && contact.rearHandForward >= 0.15 &&
        contact.leadArmBendForward >= 0.59 && contact.rearArmBendForward >= 0.61,
        "side kick keeps both hands in a readable balance guard");
    Check(contact.leadShoulderYOffset <= -0.045 &&
        contact.rearShoulderYOffset >= 0.03 &&
        std::abs(contact.bodyRotateX) < 1e-9,
        "side kick raises the kicking-side shoulder without pitching the chest skyward");
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
              << ", contact/rechamber knee=" << contactKnee << "/" << rechamberKnee
              << ", support drift=" << maximumSupportDrift
              << ", thigh/shin=" << thighLength << "/" << shinLength << '\n';
}

besktop::TwoBoneIkSolution SolveActionArm(
    const besktop::ActionSample& sample,
    bool leadArm,
    double direction,
    double planeSide)
{
    using namespace besktop;
    const double heading = direction < 0.0 ? -1.0 : 1.0;
    const double depthSign = leadArm ? heading : -heading;
    const double forward = leadArm ? sample.leadHandForward : sample.rearHandForward;
    const double targetY = leadArm ? sample.leadHandY : sample.rearHandY;
    const double targetDepth = leadArm ? sample.leadHandDepth : sample.rearHandDepth;
    const double bendForward = leadArm ?
        sample.leadArmBendForward : sample.rearArmBendForward;
    const double shoulderForward = leadArm ?
        sample.leadShoulderForwardOffset : sample.rearShoulderForwardOffset;
    const double shoulderY = leadArm ?
        sample.leadShoulderYOffset : sample.rearShoulderYOffset;
    const double shoulderDepth = leadArm ?
        sample.leadShoulderDepthOffset : sample.rearShoulderDepthOffset;
    const GaitVec3 shoulder{
        heading * shoulderForward * planeSide,
        (-0.10 + shoulderY) * planeSide,
        depthSign * (0.24 + shoulderDepth) * planeSide,
    };
    const GaitVec3 target{
        shoulder.x + heading * forward * planeSide,
        targetY * planeSide,
        depthSign * targetDepth * planeSide,
    };
    return SolveTwoBoneIk(
        shoulder,
        target,
        planeSide * 0.30,
        planeSide * 0.32,
        GaitVec3{heading * bendForward, 1.0, depthSign * 0.25});
}

besktop::TurnProjectedPoint ProjectActionPoint(
    const besktop::ActionSample& sample,
    const besktop::GaitVec3& local,
    double direction,
    double planeSide)
{
    using namespace besktop;
    const double heading = direction < 0.0 ? -1.0 : 1.0;
    const double facingYaw = direction < 0.0 ? kPi : 0.0;
    TurnProjectedPoint projected = ProjectTurnPointWithRotation(
        local,
        sample.bodyRotateX,
        facingYaw + sample.bodyRotateY,
        sample.bodyRotateZ,
        std::max(520.0, planeSide * 12.0));
    projected.x += heading * sample.rootOffsetForward * planeSide +
        sample.rootOffsetLateral * planeSide;
    projected.y += sample.rootOffsetY * planeSide;
    return projected;
}

void TestActionMetadataAndDefenseWindows()
{
    using namespace besktop;
    const ActionId punches[] = {
        ActionId::LeadStraight,
        ActionId::RearStraight,
        ActionId::Uppercut,
        ActionId::Hook,
        ActionId::SwingPunch,
    };
    const ActionId kicks[] = {
        ActionId::FrontKick,
        ActionId::SideKick,
        ActionId::RoundhouseKick,
        ActionId::SpinningBackKick,
    };
    for (const ActionId id : punches) {
        const ActionClip& clip = GetActionClip(id);
        Check(clip.attackType == ActionAttackType::Punch,
            "punch metadata identifies attack type");
        Check(clip.hitStrength != ActionHitStrength::None,
            "punch metadata identifies hit strength");
        const ActionSample prepare = SampleAction(clip, clip.prepareEnd * 0.8, 1.0);
        const ActionSample contact = SampleAction(clip, (clip.activeEnd + clip.contactEnd) * 0.5, 1.0);
        const double poseDifference =
            std::abs(contact.bodyRotateY - prepare.bodyRotateY) +
            std::abs(contact.rootOffsetForward - prepare.rootOffsetForward) +
            std::abs(contact.leadHandForward - prepare.leadHandForward) +
            std::abs(contact.rearHandForward - prepare.rearHandForward) +
            std::abs(contact.punchStrength - prepare.punchStrength);
        Check(poseDifference > 0.20, "punch contact differs measurably from prepare");
    }
    for (const ActionId id : kicks) {
        const ActionClip& clip = GetActionClip(id);
        Check(clip.attackType == ActionAttackType::Kick,
            "kick metadata identifies attack type");
        Check(clip.hitStrength != ActionHitStrength::None,
            "kick metadata identifies hit strength");
        const ActionSample prepare = SampleAction(clip, clip.prepareEnd * 0.8, 1.0);
        const ActionSample contact = SampleAction(clip, (clip.activeEnd + clip.contactEnd) * 0.5, 1.0);
        const double poseDifference =
            std::abs(contact.bodyRotateY - prepare.bodyRotateY) +
            std::abs(contact.leadFootForwardOffset - prepare.leadFootForwardOffset) +
            std::abs(contact.leadFootLift - prepare.leadFootLift) +
            std::abs(contact.leadFootDepthOffset - prepare.leadFootDepthOffset) +
            std::abs(contact.kickStrength - prepare.kickStrength);
        Check(poseDifference > 0.20, "kick contact differs measurably from prepare");
    }

    const ActionClip& slipLeft = GetActionClip(ActionId::SlipLeft);
    const ActionClip& slipRight = GetActionClip(ActionId::SlipRight);
    Check(slipLeft.defenseWindow.type == ActionDefenseWindowType::Evade &&
        slipRight.defenseWindow.type == ActionDefenseWindowType::Evade,
        "slips expose evade windows");
    Check(std::abs(slipLeft.defenseWindow.startSeconds - slipRight.defenseWindow.startSeconds) < 1e-9 &&
        std::abs(slipLeft.defenseWindow.endSeconds - slipRight.defenseWindow.endSeconds) < 1e-9,
        "slip evade windows share a timeline");
    Check(ActionDefenseWindowAt(slipLeft, slipLeft.defenseWindow.startSeconds) ==
        ActionDefenseWindowType::Evade,
        "evade window includes its start");
    Check(ActionDefenseWindowAt(slipLeft, slipLeft.defenseWindow.endSeconds) ==
        ActionDefenseWindowType::None,
        "evade window excludes its end");

    const ActionClip& parry = GetActionClip(ActionId::Parry);
    Check(ActionDefenseWindowAt(parry, 0.20) == ActionDefenseWindowType::Parry,
        "parry exposes a queryable parry window");
    Check(ActionDefenseWindowAt(parry, 0.05) == ActionDefenseWindowType::None,
        "parry window is inactive during early prepare");
    Check(GetActionClip(ActionId::WhiffRecovery).whiffRecovery,
        "whiff recovery metadata is explicit");
}

void TestPunchPoseMathematics()
{
    using namespace besktop;
    constexpr double planeSide = 48.0;
    constexpr double upperArmLength = planeSide * 0.30;
    constexpr double forearmLength = planeSide * 0.32;
    const ActionId punches[] = {
        ActionId::LeadStraight,
        ActionId::RearStraight,
        ActionId::Uppercut,
        ActionId::Hook,
        ActionId::SwingPunch,
    };
    for (const ActionId id : punches) {
        const ActionClip& clip = GetActionClip(id);
        const ActionSample contact = SampleAction(clip, (clip.activeEnd + clip.contactEnd) * 0.5, 1.0);
        for (const bool leadArm : {false, true}) {
            const TwoBoneIkSolution arm = SolveActionArm(contact, leadArm, 1.0, planeSide);
            Check(std::abs(Distance(arm.root, arm.joint) - upperArmLength) < 1e-6,
                "punch upper arm keeps fixed length");
            Check(std::abs(Distance(arm.joint, arm.end) - forearmLength) < 1e-6,
                "punch forearm keeps fixed length");
        }
    }

    const ActionClip& leadClip = GetActionClip(ActionId::LeadStraight);
    const ActionSample leadPrepare = SampleAction(leadClip, leadClip.prepareEnd * 0.8, 1.0);
    const ActionSample leadContact = SampleAction(leadClip, 0.32, 1.0);
    const ActionSample leadRecover = SampleAction(leadClip, 0.50, 1.0);
    const double leadPrepareReach = Distance(
        SolveActionArm(leadPrepare, true, 1.0, planeSide).root,
        SolveActionArm(leadPrepare, true, 1.0, planeSide).end);
    const double leadContactReach = Distance(
        SolveActionArm(leadContact, true, 1.0, planeSide).root,
        SolveActionArm(leadContact, true, 1.0, planeSide).end);
    Check(leadContactReach > leadPrepareReach + planeSide * 0.25,
        "lead straight extends clearly from prepare to contact");
    const double leadPrepareElbow = JointInteriorAngleDegrees(
        SolveActionArm(leadPrepare, true, 1.0, planeSide));
    const double leadContactElbow = JointInteriorAngleDegrees(
        SolveActionArm(leadContact, true, 1.0, planeSide));
    const double leadRecoverElbow = JointInteriorAngleDegrees(
        SolveActionArm(leadRecover, true, 1.0, planeSide));
    Check(leadPrepareElbow >= 65.0,
        "lead straight prepare avoids an acute folded elbow");
    Check(leadRecoverElbow >= 65.0,
        "lead straight recover avoids an acute folded elbow");
    Check(leadContactElbow > leadPrepareElbow + 35.0 &&
        leadContactElbow > leadRecoverElbow + 25.0,
        "lead straight contains one clear extension between guard poses");

    const ActionSample leadContactLeft = SampleAction(leadClip, 0.32, -1.0);
    const GaitVec3 neutralLeadRight{0.0, -planeSide * 0.10, planeSide * 0.24};
    const GaitVec3 neutralRearRight{0.0, -planeSide * 0.10, -planeSide * 0.24};
    const GaitVec3 drivenLeadRight{
        leadContact.leadShoulderForwardOffset * planeSide,
        (-0.10 + leadContact.leadShoulderYOffset) * planeSide,
        (0.24 + leadContact.leadShoulderDepthOffset) * planeSide,
    };
    const GaitVec3 drivenRearRight{
        leadContact.rearShoulderForwardOffset * planeSide,
        (-0.10 + leadContact.rearShoulderYOffset) * planeSide,
        -(0.24 + leadContact.rearShoulderDepthOffset) * planeSide,
    };
    const GaitVec3 neutralLeadLeft{0.0, -planeSide * 0.10, -planeSide * 0.24};
    const GaitVec3 drivenLeadLeft{
        leadContactLeft.leadShoulderForwardOffset * planeSide,
        (-0.10 + leadContactLeft.leadShoulderYOffset) * planeSide,
        -(0.24 + leadContactLeft.leadShoulderDepthOffset) * planeSide,
    };
    const TurnProjectedPoint neutralLeadRightProjected = ProjectActionPoint(
        ActionSample{}, neutralLeadRight, 1.0, planeSide);
    const TurnProjectedPoint neutralRearRightProjected = ProjectActionPoint(
        ActionSample{}, neutralRearRight, 1.0, planeSide);
    const TurnProjectedPoint drivenLeadRightProjected = ProjectActionPoint(
        leadContact, drivenLeadRight, 1.0, planeSide);
    const TurnProjectedPoint drivenRearRightProjected = ProjectActionPoint(
        leadContact, drivenRearRight, 1.0, planeSide);
    const TurnProjectedPoint neutralLeadLeftProjected = ProjectActionPoint(
        ActionSample{}, neutralLeadLeft, -1.0, planeSide);
    const TurnProjectedPoint drivenLeadLeftProjected = ProjectActionPoint(
        leadContactLeft, drivenLeadLeft, -1.0, planeSide);
    Check(drivenLeadRightProjected.x > neutralLeadRightProjected.x + planeSide * 0.08,
        "lead straight advances the striking shoulder in screen space");
    Check(drivenRearRightProjected.x < neutralRearRightProjected.x - planeSide * 0.01,
        "lead straight retracts the guarding shoulder in screen space");
    Check(drivenLeadRightProjected.y < drivenRearRightProjected.y - planeSide * 0.015,
        "lead straight raises the striking shoulder and lowers the guarding shoulder");
    Check(std::abs((drivenLeadRightProjected.x - neutralLeadRightProjected.x) +
        (drivenLeadLeftProjected.x - neutralLeadLeftProjected.x)) < planeSide * 0.01,
        "lead straight shoulder drive mirrors horizontally");
    Check(std::abs(drivenLeadRightProjected.y - drivenLeadLeftProjected.y) < planeSide * 0.01,
        "lead straight shoulder lift is symmetric across facing directions");

    const TurnActorGeometry actorGeometry = BuildTurnActorGeometry(planeSide, planeSide);
    const GaitVec3 iconCenter{-actorGeometry.bodyCenterOffset, 0.0, 0.0};
    const TurnProjectedPoint neutralIconRight = ProjectActionPoint(
        ActionSample{}, iconCenter, 1.0, planeSide);
    const TurnProjectedPoint drivenIconRight = ProjectActionPoint(
        leadContact, iconCenter, 1.0, planeSide);
    const TurnProjectedPoint neutralIconLeft = ProjectActionPoint(
        ActionSample{}, iconCenter, -1.0, planeSide);
    const TurnProjectedPoint drivenIconLeft = ProjectActionPoint(
        leadContactLeft, iconCenter, -1.0, planeSide);
    Check(drivenIconRight.x > neutralIconRight.x + planeSide * 0.025,
        "lead straight carries the icon torso forward with the shoulder drive");
    Check(drivenIconRight.y > neutralIconRight.y + planeSide * 0.025,
        "lead straight presses the icon torso slightly downward");
    Check(std::abs((drivenIconRight.x - neutralIconRight.x) +
        (drivenIconLeft.x - neutralIconLeft.x)) < planeSide * 0.01,
        "lead straight torso drive mirrors horizontally");
    Check(std::abs((drivenIconRight.y - neutralIconRight.y) -
        (drivenIconLeft.y - neutralIconLeft.y)) < planeSide * 0.01,
        "lead straight torso press matches across facing directions");

    const TwoBoneIkSolution leadGuard = SolveActionArm(
        leadContact, false, 1.0, planeSide);
    Check(leadGuard.end.x > leadGuard.root.x + planeSide * 0.10,
        "lead straight guard hand stays in front of the torso");
    Check(leadGuard.end.y > leadGuard.root.y,
        "lead straight guard hand stays below shoulder height");
    Check(leadGuard.joint.y > leadGuard.root.y,
        "lead straight guard elbow bends downward below the shoulder");
    Check(leadGuard.joint.y < leadGuard.end.y + planeSide * 0.35,
        "lead straight guard elbow remains compact around the hand");
    const double guardUpperArmForwardDegrees = std::atan2(
        leadGuard.joint.x - leadGuard.root.x,
        leadGuard.joint.y - leadGuard.root.y) * 180.0 / kPi;
    Check(guardUpperArmForwardDegrees >= 15.0 && guardUpperArmForwardDegrees <= 20.0,
        "lead straight guard upper arm leans forward by 15 to 20 degrees");

    const ActionClip& rearClip = GetActionClip(ActionId::RearStraight);
    const ActionSample rearPrepare = SampleAction(rearClip, rearClip.prepareEnd * 0.8, 1.0);
    const ActionSample rearContact = SampleAction(rearClip, 0.35, 1.0);
    const ActionSample rearRecover = SampleAction(rearClip, 0.55, 1.0);
    Check(rearContact.rearHandForward > rearContact.leadHandForward + 0.35,
        "rear straight drives the rear hand target");
    const TwoBoneIkSolution rearPrepareArm = SolveActionArm(
        rearPrepare, false, 1.0, planeSide);
    const TwoBoneIkSolution rearContactArm = SolveActionArm(
        rearContact, false, 1.0, planeSide);
    const TwoBoneIkSolution rearRecoverArm = SolveActionArm(
        rearRecover, false, 1.0, planeSide);
    const double rearPrepareReach = Distance(rearPrepareArm.root, rearPrepareArm.end);
    const double rearContactReach = Distance(rearContactArm.root, rearContactArm.end);
    const double rearPrepareElbow = JointInteriorAngleDegrees(rearPrepareArm);
    const double rearContactElbow = JointInteriorAngleDegrees(rearContactArm);
    const double rearRecoverElbow = JointInteriorAngleDegrees(rearRecoverArm);
    Check(rearContactReach > rearPrepareReach + planeSide * 0.25,
        "rear straight extends clearly from guard to contact");
    Check(rearPrepareElbow >= 65.0 && rearRecoverElbow >= 65.0,
        "rear straight guard and recovery avoid an acute elbow fold");
    Check(rearContactElbow > rearPrepareElbow + 35.0 &&
        rearContactElbow > rearRecoverElbow + 25.0,
        "rear straight contains one clear extension between guard poses");

    const TwoBoneIkSolution rearLeadGuard = SolveActionArm(
        rearContact, true, 1.0, planeSide);
    const double rearLeadGuardAngle = std::atan2(
        rearLeadGuard.joint.x - rearLeadGuard.root.x,
        rearLeadGuard.joint.y - rearLeadGuard.root.y) * 180.0 / kPi;
    Check(rearLeadGuard.end.x > rearLeadGuard.root.x + planeSide * 0.10 &&
        rearLeadGuard.end.y > rearLeadGuard.root.y &&
        rearLeadGuard.joint.y > rearLeadGuard.root.y,
        "rear straight keeps the lead hand in a compact forward guard");
    Check(rearLeadGuardAngle >= 15.0 && rearLeadGuardAngle <= 20.0,
        "rear straight lead guard upper arm leans forward by 15 to 20 degrees");

    const ActionSample rearContactLeft = SampleAction(rearClip, 0.35, -1.0);
    const GaitVec3 drivenRearShoulderRight{
        rearContact.rearShoulderForwardOffset * planeSide,
        (-0.10 + rearContact.rearShoulderYOffset) * planeSide,
        -(0.24 + rearContact.rearShoulderDepthOffset) * planeSide,
    };
    const GaitVec3 drivenLeadShoulderRight{
        rearContact.leadShoulderForwardOffset * planeSide,
        (-0.10 + rearContact.leadShoulderYOffset) * planeSide,
        (0.24 + rearContact.leadShoulderDepthOffset) * planeSide,
    };
    const GaitVec3 neutralRearLeft{0.0, -planeSide * 0.10, planeSide * 0.24};
    const GaitVec3 drivenRearShoulderLeft{
        rearContactLeft.rearShoulderForwardOffset * planeSide,
        (-0.10 + rearContactLeft.rearShoulderYOffset) * planeSide,
        (0.24 + rearContactLeft.rearShoulderDepthOffset) * planeSide,
    };
    const TurnProjectedPoint drivenRearShoulderRightProjected = ProjectActionPoint(
        rearContact, drivenRearShoulderRight, 1.0, planeSide);
    const TurnProjectedPoint drivenLeadShoulderRightProjected = ProjectActionPoint(
        rearContact, drivenLeadShoulderRight, 1.0, planeSide);
    const TurnProjectedPoint drivenRearShoulderLeftProjected = ProjectActionPoint(
        rearContactLeft, drivenRearShoulderLeft, -1.0, planeSide);
    const TurnProjectedPoint neutralRearLeftProjected = ProjectActionPoint(
        ActionSample{}, neutralRearLeft, -1.0, planeSide);
    Check(drivenRearShoulderRightProjected.x > neutralRearRightProjected.x + planeSide * 0.08,
        "rear straight advances the striking rear shoulder in screen space");
    Check(drivenLeadShoulderRightProjected.x < neutralLeadRightProjected.x - planeSide * 0.01,
        "rear straight retracts the guarding lead shoulder in screen space");
    Check(drivenRearShoulderRightProjected.y < drivenLeadShoulderRightProjected.y - planeSide * 0.015,
        "rear straight raises the rear shoulder and lowers the lead shoulder");
    Check(std::abs((drivenRearShoulderRightProjected.x - neutralRearRightProjected.x) +
        (drivenRearShoulderLeftProjected.x - neutralRearLeftProjected.x)) < planeSide * 0.01,
        "rear straight shoulder drive mirrors horizontally");

    const TurnProjectedPoint rearIconRight = ProjectActionPoint(
        rearContact, iconCenter, 1.0, planeSide);
    const TurnProjectedPoint rearIconLeft = ProjectActionPoint(
        rearContactLeft, iconCenter, -1.0, planeSide);
    Check(rearIconRight.x > neutralIconRight.x + planeSide * 0.03,
        "rear straight carries the icon torso forward");
    Check(rearIconRight.y > neutralIconRight.y + planeSide * 0.025,
        "rear straight presses the icon torso downward");
    Check(std::abs((rearIconRight.x - neutralIconRight.x) +
        (rearIconLeft.x - neutralIconLeft.x)) < planeSide * 0.01,
        "rear straight torso drive mirrors horizontally");
    Check(std::abs(rearContact.bodyRotateY) > std::abs(leadContact.bodyRotateY) + 0.05,
        "rear straight uses more torso rotation than lead straight");

    const ActionClip& uppercutClip = GetActionClip(ActionId::Uppercut);
    const ActionSample uppercutPrepare = SampleAction(uppercutClip, 0.20, 1.0);
    const ActionSample uppercutContact = SampleAction(uppercutClip, 0.41, 1.0);
    const ActionSample uppercutRecover = SampleAction(uppercutClip, 0.60, 1.0);
    Check(uppercutContact.rearHandY < uppercutPrepare.rearHandY - 0.35,
        "uppercut hand target rises from low to high");
    const TwoBoneIkSolution uppercutPrepareArm = SolveActionArm(
        uppercutPrepare, false, 1.0, planeSide);
    const TwoBoneIkSolution uppercutContactArm = SolveActionArm(
        uppercutContact, false, 1.0, planeSide);
    const TwoBoneIkSolution uppercutRecoverArm = SolveActionArm(
        uppercutRecover, false, 1.0, planeSide);
    const double uppercutPrepareElbow = JointInteriorAngleDegrees(uppercutPrepareArm);
    const double uppercutContactElbow = JointInteriorAngleDegrees(uppercutContactArm);
    const double uppercutRecoverElbow = JointInteriorAngleDegrees(uppercutRecoverArm);
    Check(uppercutPrepareElbow >= 65.0 && uppercutRecoverElbow >= 65.0,
        "uppercut chamber and recovery avoid an acute elbow fold");
    Check(uppercutContactElbow >= 70.0 && uppercutContactElbow <= 125.0,
        "uppercut stays visibly bent while driving upward");
    Check(uppercutContactArm.end.y < uppercutPrepareArm.end.y - planeSide * 0.30,
        "uppercut fist travels clearly upward from the chamber");

    const TwoBoneIkSolution uppercutLeadGuard = SolveActionArm(
        uppercutContact, true, 1.0, planeSide);
    const double uppercutGuardAngle = std::atan2(
        uppercutLeadGuard.joint.x - uppercutLeadGuard.root.x,
        uppercutLeadGuard.joint.y - uppercutLeadGuard.root.y) * 180.0 / kPi;
    Check(uppercutLeadGuard.end.x > uppercutLeadGuard.root.x + planeSide * 0.10 &&
        uppercutLeadGuard.end.y > uppercutLeadGuard.root.y &&
        uppercutLeadGuard.joint.y > uppercutLeadGuard.root.y,
        "uppercut keeps the lead hand in a compact forward guard");
    Check(uppercutGuardAngle >= 15.0 && uppercutGuardAngle <= 20.0,
        "uppercut lead guard preserves the reviewed upper-arm angle");

    const ActionSample uppercutContactLeft = SampleAction(uppercutClip, 0.41, -1.0);
    const GaitVec3 uppercutRearShoulderRight{
        uppercutContact.rearShoulderForwardOffset * planeSide,
        (-0.10 + uppercutContact.rearShoulderYOffset) * planeSide,
        -(0.24 + uppercutContact.rearShoulderDepthOffset) * planeSide,
    };
    const GaitVec3 uppercutLeadShoulderRight{
        uppercutContact.leadShoulderForwardOffset * planeSide,
        (-0.10 + uppercutContact.leadShoulderYOffset) * planeSide,
        (0.24 + uppercutContact.leadShoulderDepthOffset) * planeSide,
    };
    const GaitVec3 uppercutRearShoulderLeft{
        uppercutContactLeft.rearShoulderForwardOffset * planeSide,
        (-0.10 + uppercutContactLeft.rearShoulderYOffset) * planeSide,
        (0.24 + uppercutContactLeft.rearShoulderDepthOffset) * planeSide,
    };
    const TurnProjectedPoint uppercutRearShoulderRightProjected = ProjectActionPoint(
        uppercutContact, uppercutRearShoulderRight, 1.0, planeSide);
    const TurnProjectedPoint uppercutLeadShoulderRightProjected = ProjectActionPoint(
        uppercutContact, uppercutLeadShoulderRight, 1.0, planeSide);
    const TurnProjectedPoint uppercutRearShoulderLeftProjected = ProjectActionPoint(
        uppercutContactLeft, uppercutRearShoulderLeft, -1.0, planeSide);
    Check(uppercutRearShoulderRightProjected.x > neutralRearRightProjected.x + planeSide * 0.045,
        "uppercut advances the striking rear shoulder");
    Check(uppercutRearShoulderRightProjected.y < uppercutLeadShoulderRightProjected.y - planeSide * 0.02,
        "uppercut lifts the striking rear shoulder above the guard shoulder");
    Check(std::abs((uppercutRearShoulderRightProjected.x - neutralRearRightProjected.x) +
        (uppercutRearShoulderLeftProjected.x - neutralRearLeftProjected.x)) < planeSide * 0.01,
        "uppercut shoulder drive mirrors horizontally");

    const TurnProjectedPoint uppercutIconRight = ProjectActionPoint(
        uppercutContact, iconCenter, 1.0, planeSide);
    const TurnProjectedPoint uppercutIconLeft = ProjectActionPoint(
        uppercutContactLeft, iconCenter, -1.0, planeSide);
    Check(uppercutIconRight.y < neutralIconRight.y - planeSide * 0.005,
        "uppercut releases the icon torso upward from the loaded stance");
    Check(std::abs((uppercutIconRight.x - neutralIconRight.x) +
        (uppercutIconLeft.x - neutralIconLeft.x)) < planeSide * 0.01,
        "uppercut torso motion mirrors horizontally");

    const ActionClip& hookClip = GetActionClip(ActionId::Hook);
    const ActionSample hookPrepare = SampleAction(hookClip, 0.16, 1.0);
    const ActionSample hookContact = SampleAction(hookClip, 0.37, 1.0);
    const ActionSample hookRecover = SampleAction(hookClip, 0.55, 1.0);
    const ActionClip& swingClip = GetActionClip(ActionId::SwingPunch);
    const ActionSample swingPrepare = SampleAction(swingClip, 0.23, 1.0);
    const ActionSample swingContact = SampleAction(swingClip, 0.51, 1.0);
    const ActionSample swingRecover = SampleAction(swingClip, 0.74, 1.0);
    const double hookPrepareElbow = JointInteriorAngleDegrees(
        SolveActionArm(hookPrepare, true, 1.0, planeSide));
    const double hookElbow = JointInteriorAngleDegrees(
        SolveActionArm(hookContact, true, 1.0, planeSide));
    const double hookRecoverElbow = JointInteriorAngleDegrees(
        SolveActionArm(hookRecover, true, 1.0, planeSide));
    const double swingElbow = JointInteriorAngleDegrees(
        SolveActionArm(swingContact, true, 1.0, planeSide));
    const double swingPrepareElbow = JointInteriorAngleDegrees(
        SolveActionArm(swingPrepare, true, 1.0, planeSide));
    const double swingRecoverElbow = JointInteriorAngleDegrees(
        SolveActionArm(swingRecover, true, 1.0, planeSide));
    Check(hookPrepareElbow >= 65.0 && hookRecoverElbow >= 65.0,
        "hook guard and recovery avoid an acute elbow fold");
    Check(hookElbow >= 78.0 && hookElbow <= 110.0,
        "hook keeps a near-right-angle elbow at contact");
    Check(hookContact.leadHandDepth > hookPrepare.leadHandDepth + 0.20,
        "hook fist travels through a clear horizontal depth arc");

    const TwoBoneIkSolution hookRearGuard = SolveActionArm(
        hookContact, false, 1.0, planeSide);
    const double hookGuardAngle = std::atan2(
        hookRearGuard.joint.x - hookRearGuard.root.x,
        hookRearGuard.joint.y - hookRearGuard.root.y) * 180.0 / kPi;
    Check(hookRearGuard.end.x > hookRearGuard.root.x + planeSide * 0.10 &&
        hookRearGuard.end.y > hookRearGuard.root.y &&
        hookRearGuard.joint.y > hookRearGuard.root.y,
        "hook keeps the rear hand in a compact forward guard");
    Check(hookGuardAngle >= 15.0 && hookGuardAngle <= 20.0,
        "hook rear guard preserves the reviewed upper-arm angle");

    const ActionSample hookContactLeft = SampleAction(hookClip, 0.37, -1.0);
    const GaitVec3 hookLeadShoulderRight{
        hookContact.leadShoulderForwardOffset * planeSide,
        (-0.10 + hookContact.leadShoulderYOffset) * planeSide,
        (0.24 + hookContact.leadShoulderDepthOffset) * planeSide,
    };
    const GaitVec3 hookRearShoulderRight{
        hookContact.rearShoulderForwardOffset * planeSide,
        (-0.10 + hookContact.rearShoulderYOffset) * planeSide,
        -(0.24 + hookContact.rearShoulderDepthOffset) * planeSide,
    };
    const GaitVec3 hookLeadShoulderLeft{
        hookContactLeft.leadShoulderForwardOffset * planeSide,
        (-0.10 + hookContactLeft.leadShoulderYOffset) * planeSide,
        -(0.24 + hookContactLeft.leadShoulderDepthOffset) * planeSide,
    };
    const TurnProjectedPoint hookLeadShoulderRightProjected = ProjectActionPoint(
        hookContact, hookLeadShoulderRight, 1.0, planeSide);
    const TurnProjectedPoint hookRearShoulderRightProjected = ProjectActionPoint(
        hookContact, hookRearShoulderRight, 1.0, planeSide);
    const TurnProjectedPoint hookLeadShoulderLeftProjected = ProjectActionPoint(
        hookContactLeft, hookLeadShoulderLeft, -1.0, planeSide);
    Check(hookLeadShoulderRightProjected.x > neutralLeadRightProjected.x + planeSide * 0.07,
        "hook advances the striking lead shoulder");
    Check(hookRearShoulderRightProjected.x < neutralRearRightProjected.x - planeSide * 0.01,
        "hook retracts the guarding rear shoulder");
    Check(std::abs((hookLeadShoulderRightProjected.x - neutralLeadRightProjected.x) +
        (hookLeadShoulderLeftProjected.x - neutralLeadLeftProjected.x)) < planeSide * 0.01,
        "hook shoulder drive mirrors horizontally");

    const TurnProjectedPoint hookIconRight = ProjectActionPoint(
        hookContact, iconCenter, 1.0, planeSide);
    const TurnProjectedPoint hookIconLeft = ProjectActionPoint(
        hookContactLeft, iconCenter, -1.0, planeSide);
    Check(hookIconRight.x > neutralIconRight.x + planeSide * 0.02,
        "hook carries the icon torso into the short arc");
    Check(std::abs((hookIconRight.x - neutralIconRight.x) +
        (hookIconLeft.x - neutralIconLeft.x)) < planeSide * 0.01,
        "hook torso drive mirrors horizontally");

    Check(swingPrepareElbow >= 65.0 && swingRecoverElbow >= 65.0,
        "swing punch load and recovery avoid an acute elbow fold");
    Check(swingElbow >= 85.0 && swingElbow <= 125.0,
        "swing punch keeps a bent elbow through the wide contact arc");
    Check(swingContact.leadHandDepth > swingPrepare.leadHandDepth + 0.45,
        "swing punch sweeps through a distinctly wide depth arc");

    const TwoBoneIkSolution swingRearGuard = SolveActionArm(
        swingContact, false, 1.0, planeSide);
    const double swingGuardAngle = std::atan2(
        swingRearGuard.joint.x - swingRearGuard.root.x,
        swingRearGuard.joint.y - swingRearGuard.root.y) * 180.0 / kPi;
    Check(swingRearGuard.end.x > swingRearGuard.root.x + planeSide * 0.10 &&
        swingRearGuard.end.y > swingRearGuard.root.y &&
        swingRearGuard.joint.y > swingRearGuard.root.y,
        "swing punch keeps the rear hand in a compact forward guard");
    Check(swingGuardAngle >= 15.0 && swingGuardAngle <= 20.0,
        "swing punch rear guard preserves the reviewed upper-arm angle");

    const ActionSample swingContactLeft = SampleAction(swingClip, 0.51, -1.0);
    const GaitVec3 swingLeadShoulderRight{
        swingContact.leadShoulderForwardOffset * planeSide,
        (-0.10 + swingContact.leadShoulderYOffset) * planeSide,
        (0.24 + swingContact.leadShoulderDepthOffset) * planeSide,
    };
    const GaitVec3 swingRearShoulderRight{
        swingContact.rearShoulderForwardOffset * planeSide,
        (-0.10 + swingContact.rearShoulderYOffset) * planeSide,
        -(0.24 + swingContact.rearShoulderDepthOffset) * planeSide,
    };
    const GaitVec3 swingLeadShoulderLeft{
        swingContactLeft.leadShoulderForwardOffset * planeSide,
        (-0.10 + swingContactLeft.leadShoulderYOffset) * planeSide,
        -(0.24 + swingContactLeft.leadShoulderDepthOffset) * planeSide,
    };
    const TurnProjectedPoint swingLeadShoulderRightProjected = ProjectActionPoint(
        swingContact, swingLeadShoulderRight, 1.0, planeSide);
    const TurnProjectedPoint swingRearShoulderRightProjected = ProjectActionPoint(
        swingContact, swingRearShoulderRight, 1.0, planeSide);
    const TurnProjectedPoint swingLeadShoulderLeftProjected = ProjectActionPoint(
        swingContactLeft, swingLeadShoulderLeft, -1.0, planeSide);
    Check(swingLeadShoulderRightProjected.x >
        hookLeadShoulderRightProjected.x + planeSide * 0.035,
        "swing punch drives the lead shoulder farther than compact hook");
    Check(swingRearShoulderRightProjected.x < neutralRearRightProjected.x - planeSide * 0.015,
        "swing punch retracts the guarding rear shoulder");
    Check(std::abs((swingLeadShoulderRightProjected.x - neutralLeadRightProjected.x) +
        (swingLeadShoulderLeftProjected.x - neutralLeadLeftProjected.x)) < planeSide * 0.01,
        "swing punch shoulder drive mirrors horizontally");

    const TurnProjectedPoint swingIconRight = ProjectActionPoint(
        swingContact, iconCenter, 1.0, planeSide);
    const TurnProjectedPoint swingIconLeft = ProjectActionPoint(
        swingContactLeft, iconCenter, -1.0, planeSide);
    Check(swingIconRight.x > hookIconRight.x + planeSide * 0.025,
        "swing punch carries the icon torso farther than compact hook");
    Check(std::abs((swingIconRight.x - neutralIconRight.x) +
        (swingIconLeft.x - neutralIconLeft.x)) < planeSide * 0.01,
        "swing punch torso drive mirrors horizontally");

    Check(swingContact.leadHandDepth - 0.20 > hookContact.leadHandDepth - 0.20 + 0.10,
        "swing punch uses a wider depth arc than hook");
    Check(std::abs(swingContact.bodyRotateY) > std::abs(hookContact.bodyRotateY) + 0.10,
        "swing punch rotates the body more than hook");

    std::cout << "punch metrics: lead reach=" << leadPrepareReach << "->" << leadContactReach
              << ", lead elbow prepare/contact/recover=" << leadPrepareElbow << "/"
              << leadContactElbow << "/" << leadRecoverElbow
              << ", guard shoulder/elbow/hand y=" << leadGuard.root.y << "/"
              << leadGuard.joint.y << "/" << leadGuard.end.y
              << ", guard upper-arm forward=" << guardUpperArmForwardDegrees
              << ", shoulder drive lead/rear="
              << drivenLeadRightProjected.x - neutralLeadRightProjected.x << "/"
              << drivenRearRightProjected.x - neutralRearRightProjected.x
              << ", icon drive x/y=" << drivenIconRight.x - neutralIconRight.x << "/"
              << drivenIconRight.y - neutralIconRight.y
              << ", rear elbow prepare/contact/recover=" << rearPrepareElbow << "/"
              << rearContactElbow << "/" << rearRecoverElbow
              << ", rear guard upper-arm forward=" << rearLeadGuardAngle
              << ", rear shoulder drive="
              << drivenRearShoulderRightProjected.x - neutralRearRightProjected.x
              << ", rear icon drive x/y=" << rearIconRight.x - neutralIconRight.x << "/"
              << rearIconRight.y - neutralIconRight.y
              << ", uppercut elbow prepare/contact/recover=" << uppercutPrepareElbow << "/"
              << uppercutContactElbow << "/" << uppercutRecoverElbow
              << ", uppercut guard=" << uppercutGuardAngle
              << ", uppercut shoulder x/y="
              << uppercutRearShoulderRightProjected.x - neutralRearRightProjected.x << "/"
              << uppercutRearShoulderRightProjected.y - neutralRearRightProjected.y
              << ", uppercut icon x/y=" << uppercutIconRight.x - neutralIconRight.x << "/"
              << uppercutIconRight.y - neutralIconRight.y
              << ", hook elbow prepare/contact/recover=" << hookPrepareElbow << "/"
              << hookElbow << "/" << hookRecoverElbow
              << ", hook guard=" << hookGuardAngle
              << ", hook shoulder/icon x="
              << hookLeadShoulderRightProjected.x - neutralLeadRightProjected.x << "/"
              << hookIconRight.x - neutralIconRight.x
              << ", swing elbow prepare/contact/recover=" << swingPrepareElbow << "/"
              << swingElbow << "/" << swingRecoverElbow
              << ", swing guard=" << swingGuardAngle
              << ", swing shoulder/icon x="
              << swingLeadShoulderRightProjected.x - neutralLeadRightProjected.x << "/"
              << swingIconRight.x - neutralIconRight.x
              << ", hook/swing depth=" << hookContact.leadHandDepth << "/"
              << swingContact.leadHandDepth << '\n';
}

void TestKickPoseMathematics()
{
    using namespace besktop;
    constexpr double planeSide = 48.0;
    constexpr double thighLength = planeSide * 0.40;
    constexpr double shinLength = planeSide * 0.41;
    const ActionId kicks[] = {
        ActionId::FrontKick,
        ActionId::RoundhouseKick,
        ActionId::SpinningBackKick,
    };
    double maximumSupportDrift = 0.0;
    double maximumProjectedSupportDrift = 0.0;
    for (const ActionId id : kicks) {
        const ActionClip& clip = GetActionClip(id);
        const double supportReferenceTime =
            id == ActionId::SpinningBackKick ? clip.prepareEnd * 0.9 : 0.0;
        const ActionSample supportReference = SampleAction(
            clip, supportReferenceTime, 1.0);
        const GaitVec3 plantedSupport = SolveSideKickLeg(
            supportReference,
            id == ActionId::SpinningBackKick,
            1.0,
            planeSide).end;
        const GaitVec3 plantedSupportInActionSpace = RotateAroundVerticalAxis(
            plantedSupport,
            (supportReference.bodyRotateY * supportReference.lowerBodyActionRotationWeight) +
                supportReference.lowerBodyRotateY);
        const GaitVec3 plantedSupportWithRoot{
            plantedSupportInActionSpace.x + supportReference.rootOffsetForward * planeSide,
            plantedSupportInActionSpace.y + supportReference.rootOffsetY * planeSide,
            plantedSupportInActionSpace.z,
        };
        for (const double time : {clip.prepareEnd * 0.9,
                (clip.activeEnd + clip.contactEnd) * 0.5,
                (clip.contactEnd + clip.recoverEnd) * 0.5}) {
            const ActionSample sample = SampleAction(clip, time, 1.0);
            const TwoBoneIkSolution kickLeg = SolveSideKickLeg(sample, true, 1.0, planeSide);
            const bool spinningBackKick = id == ActionId::SpinningBackKick;
            const TwoBoneIkSolution measuredKickLeg = SolveSideKickLeg(
                sample, !spinningBackKick, 1.0, planeSide);
            const TwoBoneIkSolution supportLeg = SolveSideKickLeg(
                sample, spinningBackKick, 1.0, planeSide);
            const double lowerBodyYaw =
                (sample.bodyRotateY * sample.lowerBodyActionRotationWeight) +
                sample.lowerBodyRotateY;
            const GaitVec3 supportInActionSpace = RotateAroundVerticalAxis(
                supportLeg.end, lowerBodyYaw);
            const GaitVec3 supportWithRoot{
                supportInActionSpace.x + sample.rootOffsetForward * planeSide,
                supportInActionSpace.y + sample.rootOffsetY * planeSide,
                supportInActionSpace.z,
            };
            maximumSupportDrift = std::max(
                maximumSupportDrift,
                Distance(plantedSupportWithRoot, supportWithRoot));
            const GaitVec3 projectedSupport = RotateAroundVerticalAxis(
                supportLeg.end, sample.lowerBodyRotateY);
            const GaitVec3 projectedSupportWithRoot{
                projectedSupport.x + sample.rootOffsetForward * planeSide,
                projectedSupport.y + sample.rootOffsetY * planeSide,
                projectedSupport.z,
            };
            maximumProjectedSupportDrift = std::max(
                maximumProjectedSupportDrift,
                Distance(plantedSupportWithRoot, projectedSupportWithRoot));
            Check(std::abs(Distance(measuredKickLeg.root, measuredKickLeg.joint) - thighLength) < 1e-6,
                "new kick thigh keeps fixed length");
            Check(std::abs(Distance(measuredKickLeg.joint, measuredKickLeg.end) - shinLength) < 1e-6,
                "new kick shin keeps fixed length");
            const double knee = JointInteriorAngleDegrees(measuredKickLeg);
            Check(knee > 35.0 && knee < 178.0,
                "new kick knee does not reverse or lock");
        }
    }
    Check(maximumSupportDrift <= planeSide * 0.005,
        "new kicks keep the support foot planted in action space");
    Check(maximumProjectedSupportDrift <= planeSide * 0.12,
        "new kick support-foot rotation stays within a bounded visual tolerance");

    const ActionSample frontPrepare = SampleAction(GetActionClip(ActionId::FrontKick), 0.18, 1.0);
    const ActionSample frontContact = SampleAction(GetActionClip(ActionId::FrontKick), 0.38, 1.0);
    const ActionSample frontRechamber = SampleAction(GetActionClip(ActionId::FrontKick), 0.50, 1.0);
    const double frontPrepareKnee = JointInteriorAngleDegrees(
        SolveSideKickLeg(frontPrepare, true, 1.0, planeSide));
    const double frontContactKnee = JointInteriorAngleDegrees(
        SolveSideKickLeg(frontContact, true, 1.0, planeSide));
    const double frontRechamberKnee = JointInteriorAngleDegrees(
        SolveSideKickLeg(frontRechamber, true, 1.0, planeSide));
    Check(frontPrepareKnee >= 80.0 && frontPrepareKnee <= 145.0,
        "front kick chambers before extension");
    Check(frontContactKnee >= 155.0 && frontContactKnee <= 176.0,
        "front kick extends without locking");
    Check(frontRechamberKnee >= 80.0 && frontRechamberKnee <= 145.0,
        "front kick folds back into a chamber before landing");
    Check(std::abs(frontContact.bodyRotateZ) * 180.0 / kPi >= 10.5 &&
        frontContact.leadHandForward >= 0.19 &&
        frontContact.rearHandForward >= 0.17 &&
        frontContact.leadArmBendForward > 0.60 &&
        frontContact.rearArmBendForward > 0.60,
        "front kick combines a readable counter-lean with two guarded hands");
    Check(std::abs(frontContact.rootOffsetForward) < 1e-9 &&
        std::abs(frontContact.rootOffsetLateral) < 1e-9 &&
        std::abs(frontContact.rootOffsetY) < 1e-9,
        "front kick does not slide the planted actor root");

    const ActionSample sideContact = SampleAction(GetActionClip(ActionId::SideKick), 0.38, 1.0);
    const ActionSample roundPrepare = SampleAction(GetActionClip(ActionId::RoundhouseKick), 0.22, 1.0);
    const ActionSample roundContact = SampleAction(GetActionClip(ActionId::RoundhouseKick), 0.46, 1.0);
    const ActionSample roundRechamber = SampleAction(GetActionClip(ActionId::RoundhouseKick), 0.61, 1.0);
    Check(std::abs(frontContact.leadFootDepthOffset - sideContact.leadFootDepthOffset) >= 0.015,
        "front and side kick foot directions are distinct");
    Check(std::abs(roundContact.leadFootDepthOffset) >
        std::abs(frontContact.leadFootDepthOffset) + 0.25,
        "roundhouse has a larger depth sweep than front kick");
    const double roundPrepareKnee = JointInteriorAngleDegrees(
        SolveSideKickLeg(roundPrepare, true, 1.0, planeSide));
    const double roundContactKnee = JointInteriorAngleDegrees(
        SolveSideKickLeg(roundContact, true, 1.0, planeSide));
    const double roundRechamberKnee = JointInteriorAngleDegrees(
        SolveSideKickLeg(roundRechamber, true, 1.0, planeSide));
    Check(roundPrepareKnee >= 70.0 && roundPrepareKnee <= 140.0 &&
        roundRechamberKnee >= 70.0 && roundRechamberKnee <= 140.0,
        "roundhouse chambers before and after the horizontal sweep");
    Check(roundContactKnee >= 145.0 && roundContactKnee <= 176.0,
        "roundhouse snaps outward without locking or staying folded");
    Check(std::abs(roundPrepare.lowerBodyRotateY) * 180.0 / kPi >= 45.0 &&
        std::abs(roundContact.bodyRotateY) * 180.0 / kPi >= 58.0 &&
        std::abs(roundContact.lowerBodyRotateY) * 180.0 / kPi >= 50.0 &&
        roundContact.footTargetYawCompensationWeight > 0.99,
        "roundhouse turns the pelvis before the leg sweep and anchors its foot targets");
    Check(roundContact.leadFootLift >= 0.43 &&
        roundContact.leadFootDepthOffset >= 0.47 &&
        roundContact.leadHandForward >= 0.15 &&
        roundContact.rearHandForward >= 0.18,
        "roundhouse combines a high depth arc with a guarded upper body");

    const ActionClip& spinClip = GetActionClip(ActionId::SpinningBackKick);
    const ActionSample spinPrepare = SampleAction(spinClip, 0.30, 1.0);
    const ActionSample spinContact = SampleAction(spinClip, 0.65, 1.0);
    const ActionSample spinRecover = SampleAction(spinClip, 0.95, 1.0);
    const ActionSample spinStep = SampleAction(spinClip, 0.18, 1.0);
    Check(spinPrepare.bodyRotateY > spinPrepare.lowerBodyRotateY + 20.0 * kPi / 180.0,
        "spinning back kick shoulders lead the pelvis during prepare");
    Check(spinContact.bodyRotateY >= 195.0 * kPi / 180.0 &&
        spinContact.bodyRotateY <= 225.0 * kPi / 180.0 &&
        spinContact.lowerBodyRotateY >= 180.0 * kPi / 180.0 &&
        spinContact.lowerBodyRotateY <= 210.0 * kPi / 180.0,
        "spinning back kick contacts while the actor is back-facing");
    Check(spinRecover.bodyRotateY > spinContact.bodyRotateY &&
        spinRecover.bodyRotateY < 350.0 * kPi / 180.0,
        "spinning back kick continues turning during recover");
    Check(spinContact.footTargetYawCompensationWeight > 0.99 &&
        spinContact.footTargetRootCompensationWeight > 0.99 &&
        spinStep.leadFootForwardOffset >= 0.15 &&
        spinContact.rearFootForwardOffset >= 1.04 &&
        spinContact.rearFootLift >= 0.59,
        "spinning back kick anchors foot targets and keeps a balanced guard");
    Check(spinContact.rootOffsetForward > 0.16 &&
        spinClip.finalRootDisplacementForward >= 0.15,
        "spinning back kick moves the pelvis around the planted lead foot and commits forward travel");
    const double spinPrepareKnee = JointInteriorAngleDegrees(
        SolveSideKickLeg(spinPrepare, false, 1.0, planeSide));
    const double spinContactKnee = JointInteriorAngleDegrees(
        SolveSideKickLeg(spinContact, false, 1.0, planeSide));
    std::cout << "spinning back kick metrics: prepare/contact knee="
              << spinPrepareKnee << "/" << spinContactKnee << '\n';
    Check(spinPrepareKnee >= 55.0 && spinPrepareKnee <= 135.0 &&
        spinContactKnee >= 150.0 && spinContactKnee <= 176.0,
        "spinning back kick chambers and extends the left/rear kicking leg");
    Check(std::abs(spinContact.bodyRotateZ) * 180.0 / kPi >= 70.0 &&
        spinContact.upperBodyOffsetY >= 0.17,
        "spinning back kick lowers the upper body dramatically at peak extension");
    Check(spinStep.leadHandY <= -0.28 &&
        spinContact.rearHandDepth < 0.0 &&
        spinContact.leadHandDepth < 0.0,
        "spinning back kick raises the right hand during the step and swaps arm depth during the turn");
    double maximumSpinYaw = 0.0;
    bool crossedSideOn = false;
    for (int step = 0; step <= 60; ++step) {
        const double time = spinClip.duration * static_cast<double>(step) / 60.0;
        const double yaw = SampleAction(spinClip, time, 1.0).bodyRotateY;
        maximumSpinYaw = std::max(maximumSpinYaw, yaw);
        if (yaw >= (kPi * 0.45) && yaw <= (kPi * 0.55)) {
            crossedSideOn = true;
        }
    }
    Check(crossedSideOn, "spinning back kick yaw crosses near 90 degrees continuously");
    Check(maximumSpinYaw > kPi * 1.9, "spinning back kick completes a full internal turn");
    Check(std::abs(SampleAction(spinClip, spinClip.recoverEnd, 1.0).bodyRotateY - 2.0 * kPi) < 1e-9,
        "spinning back kick completes 360 degrees at the end of recover");
    Check(std::abs(SampleAction(spinClip, spinClip.duration, 1.0).bodyRotateY) < 1e-9,
        "spinning back kick returns action yaw to neutral");
    const ActionSample spinComplete = SampleAction(spinClip, spinClip.duration, 1.0);
    Check(std::abs(spinComplete.leadFootForwardOffset) < 1e-9 &&
        std::abs(spinComplete.rearFootForwardOffset) < 1e-9 &&
        std::abs(spinComplete.rearFootLift) < 1e-9,
        "spinning back kick restores the original lead/rear stance");
    const ActionSample spinBeforeComplete = SampleAction(
        spinClip, spinClip.duration - 1e-6, 1.0);
    const double committedRoot = spinClip.finalRootDisplacementForward * planeSide;
    const auto worldFootForwardBeforeCommit = [planeSide](
            const ActionSample& sample, bool leadFoot) {
        const double footOffset = leadFoot ?
            sample.leadFootForwardOffset : sample.rearFootForwardOffset;
        return sample.rootOffsetForward * planeSide +
            footOffset * planeSide -
            sample.rootOffsetForward * planeSide *
                std::clamp(sample.footTargetRootCompensationWeight, 0.0, 1.0);
    };
    Check(std::abs(worldFootForwardBeforeCommit(spinBeforeComplete, true) - committedRoot) < 1e-3 &&
        std::abs(worldFootForwardBeforeCommit(spinBeforeComplete, false) - committedRoot) < 1e-3,
        "spinning back kick lands both feet at the committed root before returning to neutral");

    std::cout << "kick metrics: front knee=" << frontPrepareKnee << "->" << frontContactKnee
              << "->" << frontRechamberKnee
              << ", round knee=" << roundPrepareKnee << "->" << roundContactKnee
              << "->" << roundRechamberKnee
              << ", depth front/side/round=" << frontContact.leadFootDepthOffset << "/"
              << sideContact.leadFootDepthOffset << "/" << roundContact.leadFootDepthOffset
              << ", support drift=" << maximumSupportDrift
              << ", projected support drift=" << maximumProjectedSupportDrift
              << ", max spin yaw=" << maximumSpinYaw * 180.0 / kPi << '\n';
}

void TestDefenseAndFeedbackMathematics()
{
    using namespace besktop;
    constexpr double planeSide = 48.0;
    const ActionClip& laybackClip = GetActionClip(ActionId::Layback);
    const ActionSample layback = SampleAction(laybackClip, 0.30, 1.0);
    const ActionSample laybackLeft = SampleAction(laybackClip, 0.30, -1.0);
    Check(std::abs(layback.bodyRotateZ) >= 24.0 * kPi / 180.0,
        "layback creates a clearly readable upper-body lean");
    Check(std::abs(layback.rootOffsetForward) <= 0.02 &&
        layback.rootOffsetY >= 0.03 && layback.rootOffsetY <= 0.04,
        "layback limits backward travel while lowering into a shallow crouch");
    Check(layback.lowerBodyActionRotationWeight <= 0.05,
        "layback isolates most upper-body rotation from the planted legs");

    const TwoBoneIkSolution laybackLeadGuard = SolveActionArm(
        layback, true, 1.0, planeSide);
    const TwoBoneIkSolution laybackRearGuard = SolveActionArm(
        layback, false, 1.0, planeSide);
    const double laybackLeadGuardAngle = std::atan2(
        laybackLeadGuard.joint.x - laybackLeadGuard.root.x,
        laybackLeadGuard.joint.y - laybackLeadGuard.root.y) * 180.0 / kPi;
    const double laybackRearGuardAngle = std::atan2(
        laybackRearGuard.joint.x - laybackRearGuard.root.x,
        laybackRearGuard.joint.y - laybackRearGuard.root.y) * 180.0 / kPi;
    Check(laybackLeadGuard.end.y > laybackLeadGuard.root.y &&
        laybackRearGuard.end.y > laybackRearGuard.root.y &&
        laybackLeadGuard.joint.y > laybackLeadGuard.root.y &&
        laybackRearGuard.joint.y > laybackRearGuard.root.y,
        "layback keeps both guard elbows below the shoulder line");
    Check(laybackLeadGuardAngle >= 15.0 && laybackLeadGuardAngle <= 20.5 &&
        laybackRearGuardAngle >= 15.0 && laybackRearGuardAngle <= 20.5,
        "layback keeps both upper arms in the reviewed guard range");

    const GaitVec3 neutralShoulderCenter{0.0, -planeSide * 0.10, 0.0};
    const GaitVec3 withdrawnShoulderCenter{
        layback.leadShoulderForwardOffset * planeSide,
        (-0.10 + layback.leadShoulderYOffset) * planeSide,
        0.0,
    };
    const GaitVec3 withdrawnShoulderCenterLeft{
        laybackLeft.leadShoulderForwardOffset * planeSide,
        (-0.10 + laybackLeft.leadShoulderYOffset) * planeSide,
        0.0,
    };
    const TurnProjectedPoint neutralShoulderRight = ProjectActionPoint(
        ActionSample{}, neutralShoulderCenter, 1.0, planeSide);
    const TurnProjectedPoint neutralShoulderLeft = ProjectActionPoint(
        ActionSample{}, neutralShoulderCenter, -1.0, planeSide);
    const TurnProjectedPoint withdrawnShoulderRight = ProjectActionPoint(
        layback, withdrawnShoulderCenter, 1.0, planeSide);
    const TurnProjectedPoint withdrawnShoulderLeft = ProjectActionPoint(
        laybackLeft, withdrawnShoulderCenterLeft, -1.0, planeSide);
    Check(withdrawnShoulderRight.x < neutralShoulderRight.x - planeSide * 0.11,
        "layback withdraws the shoulder line behind the attack line");
    Check(std::abs((withdrawnShoulderRight.x - neutralShoulderRight.x) +
        (withdrawnShoulderLeft.x - neutralShoulderLeft.x)) < planeSide * 0.01,
        "layback shoulder withdrawal mirrors horizontally");

    const GaitGeometry laybackGeometry = BuildGaitGeometry(
        planeSide, planeSide * 0.40, planeSide * 0.41);
    const double hipY = planeSide * 0.5 + std::clamp(planeSide * 0.18, 12.0, 24.0);
    double minimumLaybackKnee = 180.0;
    double maximumLaybackKnee = 0.0;
    for (const double side : {-1.0, 1.0}) {
        const double depthSign = side;
        const GaitVec3 hip{0.0, hipY, depthSign * planeSide * 0.22};
        const GaitVec3 foot{
            side * laybackGeometry.stride * 0.10,
            hipY + laybackGeometry.legDrop - layback.leadFootLift * planeSide,
            hip.z + depthSign * laybackGeometry.footDepth,
        };
        const TwoBoneIkSolution crouchedLeg = SolveTwoBoneIk(
            hip,
            foot,
            planeSide * 0.40,
            planeSide * 0.41,
            GaitVec3{1.0, 0.0, depthSign * 0.18});
        const double knee = JointInteriorAngleDegrees(crouchedLeg);
        minimumLaybackKnee = std::min(minimumLaybackKnee, knee);
        maximumLaybackKnee = std::max(maximumLaybackKnee, knee);
        Check(crouchedLeg.joint.x > std::max(crouchedLeg.root.x, crouchedLeg.end.x),
            "layback knee bends forward beyond the hip-foot line");

        const GaitVec3 mirroredHip{0.0, hipY, -depthSign * planeSide * 0.22};
        const GaitVec3 mirroredFoot{
            -side * laybackGeometry.stride * 0.10,
            foot.y,
            mirroredHip.z - depthSign * laybackGeometry.footDepth,
        };
        const TwoBoneIkSolution mirroredLeg = SolveTwoBoneIk(
            mirroredHip,
            mirroredFoot,
            planeSide * 0.40,
            planeSide * 0.41,
            GaitVec3{-1.0, 0.0, -depthSign * 0.18});
        Check(mirroredLeg.joint.x < std::min(mirroredLeg.root.x, mirroredLeg.end.x),
            "layback mirrored knee bends toward the mirrored facing");
        Check(std::abs(JointInteriorAngleDegrees(mirroredLeg) - knee) < 1e-9,
            "layback crouch keeps mirrored knee angles equal");
    }
    Check(minimumLaybackKnee >= 140.0 && maximumLaybackKnee <= 160.0,
        "layback uses a shallow controlled crouch rather than a deep squat");

    ActionSample lowerBodyLayback = layback;
    lowerBodyLayback.bodyRotateX *= layback.lowerBodyActionRotationWeight;
    lowerBodyLayback.bodyRotateY *= layback.lowerBodyActionRotationWeight;
    lowerBodyLayback.bodyRotateZ *= layback.lowerBodyActionRotationWeight;
    double maximumLaybackFootDrift = 0.0;
    for (const double side : {-1.0, 1.0}) {
        const GaitVec3 foot{
            side * laybackGeometry.stride * 0.10,
            hipY + laybackGeometry.legDrop - layback.leadFootLift * planeSide,
            side * laybackGeometry.footDepth,
        };
        GaitVec3 neutralFootLocal = foot;
        neutralFootLocal.y += layback.leadFootLift * planeSide;
        const TurnProjectedPoint neutralFoot = ProjectActionPoint(
            ActionSample{}, neutralFootLocal, 1.0, planeSide);
        const TurnProjectedPoint movedFoot = ProjectActionPoint(
            lowerBodyLayback, foot, 1.0, planeSide);
        maximumLaybackFootDrift = std::max(maximumLaybackFootDrift, std::hypot(
            movedFoot.x - neutralFoot.x,
            movedFoot.y - neutralFoot.y));
    }
    Check(maximumLaybackFootDrift <= planeSide * 0.07,
        "layback keeps projected foot drift within a small visual tolerance");

    const ActionSample laybackComplete = SampleAction(
        laybackClip, laybackClip.duration, 1.0);
    Check(std::abs(laybackComplete.bodyRotateZ) < 1e-9 &&
        std::abs(laybackComplete.rootOffsetForward) < 1e-9 &&
        std::abs(laybackComplete.leadShoulderForwardOffset) < 1e-9,
        "layback completes at the neutral pose");

    const ActionSample slipLeft = SampleAction(GetActionClip(ActionId::SlipLeft), 0.29, 1.0);
    const ActionSample slipRight = SampleAction(GetActionClip(ActionId::SlipRight), 0.29, 1.0);
    Check(std::abs(slipLeft.rootOffsetLateral + slipRight.rootOffsetLateral) < 1e-9 &&
        std::abs(slipLeft.bodyRotateZ + slipRight.bodyRotateZ) < 1e-9,
        "left and right slips mirror spatial channels");
    Check(std::abs(slipLeft.rootOffsetForward) < 1e-9 &&
        std::abs(slipRight.rootOffsetForward) < 1e-9,
        "slips avoid forward root motion");
    Check(slipLeft.leadFootTargetEnabled && slipLeft.rearFootTargetEnabled &&
        slipLeft.rootOffsetY >= 0.049 &&
        std::abs(slipLeft.leadFootLift - slipLeft.rootOffsetY) < 1e-9 &&
        slipLeft.rearFootLift > slipLeft.leadFootLift + 0.01,
        "left slip reuses the planted crouch path and bends the left/rear leg farther");
    Check(slipRight.leadFootLift > slipRight.rearFootLift + 0.01 &&
        std::abs(slipRight.rearFootLift - slipRight.rootOffsetY) < 1e-9,
        "right slip mirrors the deeper leg target");
    Check(std::abs(std::abs(slipLeft.bodyRotateX) * 180.0 / kPi - 45.0) < 0.1 &&
        std::abs(slipLeft.bodyRotateZ) < 1e-9,
        "slips use a forty-five-degree side bend distinct from layback");
    Check(slipLeft.upperBodyOffsetDepth < -0.29 &&
        slipRight.upperBodyOffsetDepth > 0.29 &&
        slipLeft.upperBodyOffsetY > 0.17 &&
        std::abs(slipLeft.upperBodyOffsetY - slipRight.upperBodyOffsetY) < 1e-9,
        "slips mirror an isolated upper-body translation toward the dodge side and down");
    const auto slipKneeAngle = [&](const ActionSample& sample, bool leadLeg) {
        const double depthSign = leadLeg ? 1.0 : -1.0;
        const double legSide = leadLeg ? 1.0 : -1.0;
        const GaitVec3 hip{0.0, hipY, depthSign * planeSide * 0.22};
        const double lift = leadLeg ? sample.leadFootLift : sample.rearFootLift;
        const GaitVec3 foot{
            legSide * laybackGeometry.stride * 0.10,
            hipY + laybackGeometry.legDrop - lift * planeSide,
            hip.z + depthSign * laybackGeometry.footDepth,
        };
        return JointInteriorAngleDegrees(SolveTwoBoneIk(
            hip,
            foot,
            planeSide * 0.40,
            planeSide * 0.41,
            GaitVec3{1.0, 0.0, depthSign * 0.18}));
    };
    const double slipLeftSupportKnee = slipKneeAngle(slipLeft, true);
    const double slipLeftDeepKnee = slipKneeAngle(slipLeft, false);
    Check(slipLeftDeepKnee < slipLeftSupportKnee - 4.0 &&
        slipLeftDeepKnee < 145.0 && slipLeftSupportKnee < 155.0,
        "left slip produces a deeper left knee and a smaller right knee bend");

    const ActionSample parry = SampleAction(GetActionClip(ActionId::Parry), 0.25, 1.0);
    Check(parry.leadHandTargetEnabled && parry.leadHandDepth > 0.35 &&
        parry.leadHandForward < 0.35,
        "parry uses a compact outward lead-arm target");
    Check(parry.rearHandTargetEnabled && parry.rearHandForward >= 0.17 &&
        parry.rearHandY > -0.02 && parry.rearArmBendForward > 0.60,
        "parry keeps the rear hand in the reviewed center guard");
    Check(!parry.leadFootTargetEnabled && !parry.rearFootTargetEnabled &&
        std::abs(parry.rootOffsetForward) < 1e-9 &&
        std::abs(parry.rootOffsetLateral) < 1e-9 &&
        std::abs(parry.rootOffsetY) < 1e-9,
        "parry leaves the lower body and root planted");
    Check(std::abs(parry.bodyRotateY) * 180.0 / kPi >= 17.5 &&
        parry.leadShoulderForwardOffset >= 0.055 &&
        parry.rearShoulderForwardOffset <= -0.025 &&
        parry.leadShoulderYOffset < parry.rearShoulderYOffset,
        "parry exaggerates the shoulder-line counter-motion for the icon silhouette");
    const TwoBoneIkSolution parryLeadArm = SolveActionArm(parry, true, 1.0, planeSide);
    const TwoBoneIkSolution parryRearArm = SolveActionArm(parry, false, 1.0, planeSide);
    const double parryLeadElbow = JointInteriorAngleDegrees(parryLeadArm);
    const double parryRearElbow = JointInteriorAngleDegrees(parryRearArm);
    std::cout << "parry metrics: lead/rear elbow=" << parryLeadElbow << "/"
              << parryRearElbow << '\n';
    Check(parryLeadElbow >= 65.0 && parryLeadElbow <= 110.0,
        "parry keeps the sweeping lead elbow visibly bent");
    Check(parryRearElbow >= 40.0 && parryRearElbow <= 65.0,
        "parry preserves the reviewed compact rear-hand guard geometry");
    const ActionSample mirroredParry = SampleAction(GetActionClip(ActionId::Parry), 0.25, -1.0);
    Check(std::abs(JointInteriorAngleDegrees(SolveActionArm(
        mirroredParry, true, -1.0, planeSide)) - parryLeadElbow) < 1e-9,
        "parry lead-arm geometry mirrors without changing its elbow angle");

    const ActionClip& lightClip = GetActionClip(ActionId::LightHitReact);
    const ActionClip& heavyClip = GetActionClip(ActionId::HeavyStagger);
    const ActionSample light = SampleAction(lightClip, 0.14, 1.0);
    const ActionSample lightLeft = SampleAction(lightClip, 0.14, -1.0);
    const ActionSample heavy = SampleAction(heavyClip, 0.34, 1.0);
    Check(heavyClip.duration > lightClip.duration + 0.40,
        "heavy stagger lasts clearly longer than light hit react");
    Check(std::abs(heavy.rootOffsetForward) > std::abs(light.rootOffsetForward) + 0.10 &&
        std::abs(heavy.bodyRotateZ) > std::abs(light.bodyRotateZ) + 0.10,
        "heavy stagger has more displacement and rotation than light reaction");
    Check(std::abs(light.bodyRotateZ) >= 17.5 * kPi / 180.0 &&
        std::abs(light.bodyRotateY) >= 15.5 * kPi / 180.0 &&
        light.rootOffsetForward <= -0.079 && light.rootOffsetForward >= -0.081,
        "light hit react creates a readable short recoil without heavy displacement");
    Check(light.leadShoulderForwardOffset <= -0.059 &&
        light.leadShoulderYOffset >= 0.034 &&
        light.rearShoulderYOffset < 0.0 &&
        light.leadHandDepth > light.rearHandDepth + 0.20,
        "light hit react jolts one shoulder and arm away while the rear hand braces");
    Check(light.leadFootTargetEnabled && light.rearFootTargetEnabled &&
        light.footTargetRootCompensationWeight > 0.99 &&
        light.rootOffsetY >= 0.044,
        "light hit react plants both feet while the knees absorb the impact");
    Check(std::abs(light.bodyRotateZ + lightLeft.bodyRotateZ) < 1e-9 &&
        std::abs(light.bodyRotateY + lightLeft.bodyRotateY) < 1e-9 &&
        std::abs(light.rootOffsetForward - lightLeft.rootOffsetForward) < 1e-9,
        "light hit react mirrors recoil rotation while preserving displacement magnitude");
    const TwoBoneIkSolution lightLeadLeg = SolveSideKickLeg(light, true, 1.0, planeSide);
    const TwoBoneIkSolution lightRearLeg = SolveSideKickLeg(light, false, 1.0, planeSide);
    const double lightLeadKnee = JointInteriorAngleDegrees(lightLeadLeg);
    const double lightRearKnee = JointInteriorAngleDegrees(lightRearLeg);
    Check(lightLeadKnee >= 135.0 && lightLeadKnee <= 165.0 &&
        lightRearKnee >= 135.0 && lightRearKnee <= 165.0 &&
        std::abs(lightLeadKnee - lightRearKnee) < 10.0,
        "light hit react uses a shallow symmetric knee give");
    const ActionSample lightLate = SampleAction(lightClip, lightClip.duration - 1e-6, 1.0);
    Check(std::abs(lightLate.bodyRotateZ) < 1e-6 &&
        std::abs(lightLate.rootOffsetForward) < 1e-6 &&
        std::abs(lightLate.rootOffsetY) < 1e-6,
        "light hit react blends continuously back to neutral before completion");
    std::cout << "light hit metrics: recoil z/yaw="
              << std::abs(light.bodyRotateZ) * 180.0 / kPi << "/"
              << std::abs(light.bodyRotateY) * 180.0 / kPi
              << ", knee=" << lightLeadKnee << "/" << lightRearKnee << '\n';

    const ActionSample heavyImpact = SampleAction(heavyClip, 0.34, 1.0);
    const ActionSample heavyGather = SampleAction(heavyClip, 0.78, 1.0);
    const ActionSample heavyBeforeComplete = SampleAction(
        heavyClip, heavyClip.duration - 1e-6, 1.0);
    Check(std::abs(heavyImpact.bodyRotateZ) >= 29.5 * kPi / 180.0 &&
        std::abs(heavyImpact.bodyRotateY) >= 17.5 * kPi / 180.0 &&
        heavyImpact.rootOffsetForward <= -0.23,
        "heavy stagger creates a strong displaced upper-body silhouette");
    Check(heavyImpact.rearFootForwardOffset < -0.18 &&
        heavyGather.leadFootForwardOffset < -0.14 &&
        heavyImpact.rearFootTargetEnabled && heavyImpact.leadFootTargetEnabled,
        "heavy stagger catches with the rear foot before gathering the lead foot");
    Check(heavyImpact.leadHandDepth > heavyImpact.rearHandDepth + 0.40 &&
        heavyImpact.leadShoulderForwardOffset <= -0.095 &&
        heavyImpact.rearShoulderForwardOffset <= -0.04,
        "heavy stagger breaks the guard and drives both shoulders backward asymmetrically");
    Check(heavyClip.finalRootDisplacementForward <= -0.15 &&
        std::abs(heavyBeforeComplete.rootOffsetForward -
            heavyClip.finalRootDisplacementForward) < 1e-5 &&
        std::abs(heavyBeforeComplete.leadFootForwardOffset -
            heavyClip.finalRootDisplacementForward) < 1e-5 &&
        std::abs(heavyBeforeComplete.rearFootForwardOffset -
            heavyClip.finalRootDisplacementForward) < 1e-5,
        "heavy stagger aligns root and both feet before committing the retreat position");
    for (const double sampleTime : {0.18, 0.30, 0.52, 0.78, 1.02}) {
        const ActionSample sample = SampleAction(heavyClip, sampleTime, 1.0);
        for (const bool leadLeg : {false, true}) {
            const TwoBoneIkSolution leg = SolveSideKickLeg(sample, leadLeg, 1.0, planeSide);
            Check(std::abs(Distance(leg.root, leg.joint) - planeSide * 0.40) < 1e-6 &&
                std::abs(Distance(leg.joint, leg.end) - planeSide * 0.41) < 1e-6,
                "heavy stagger keeps both leg segment lengths fixed");
        }
    }
    std::cout << "heavy stagger metrics: peak/root/final="
              << std::abs(heavyImpact.bodyRotateZ) * 180.0 / kPi << "/"
              << heavyImpact.rootOffsetForward << "/"
              << heavyClip.finalRootDisplacementForward << '\n';

    const ActionClip& whiffClip = GetActionClip(ActionId::WhiffRecovery);
    const ActionClip& leadClip = GetActionClip(ActionId::LeadStraight);
    Check((whiffClip.recoverEnd - whiffClip.contactEnd) >
        (leadClip.recoverEnd - leadClip.contactEnd) + 0.15,
        "whiff recovery is longer than a compact punch recovery");
    const ActionSample whiff = SampleAction(whiffClip, 0.33, 1.0);
    Check(whiff.whiffRecoveryStrength > 0.5 && whiff.rootOffsetForward > 0.08,
        "whiff recovery exposes overextension channels");

    std::cout << "defense metrics: layback shoulder x="
              << withdrawnShoulderRight.x - neutralShoulderRight.x
              << ", guard lead/rear=" << laybackLeadGuardAngle << "/"
              << laybackRearGuardAngle
              << ", knee=" << minimumLaybackKnee << ".." << maximumLaybackKnee
              << ", max foot drift=" << maximumLaybackFootDrift
              << ", slip knee deep/support=" << slipLeftDeepKnee << "/"
              << slipLeftSupportKnee << '\n';
}

void TestMirroringKeepsTimeline()
{
    using namespace besktop;
    const ActionId actions[] = {
        ActionId::SwingPunch,
        ActionId::LeadStraight,
        ActionId::RearStraight,
        ActionId::Hook,
        ActionId::Uppercut,
        ActionId::FrontKick,
        ActionId::SideKick,
        ActionId::RoundhouseKick,
        ActionId::SpinningBackKick,
        ActionId::Layback,
        ActionId::SlipLeft,
        ActionId::SlipRight,
        ActionId::Parry,
        ActionId::LightHitReact,
        ActionId::HeavyStagger,
        ActionId::WhiffRecovery,
    };
    for (const ActionId id : actions) {
        const ActionClip& clip = GetActionClip(id);
        ActionPlayer right;
        ActionPlayer left;
        right.Start(id, 1.0);
        left.Start(id, -1.0);
        const double sampleTime = (clip.activeEnd + clip.contactEnd) * 0.5;
        right.Update(sampleTime);
        left.Update(sampleTime);
        Check(right.State().phase == left.State().phase, "mirror phase matches");
        Check(std::abs(right.State().localTimeSeconds - left.State().localTimeSeconds) < 1e-9,
            "mirror time matches");
        Check(right.ConsumeEvents() == left.ConsumeEvents(), "mirror event time matches");
        const ActionSample rightSample = right.Sample();
        const ActionSample leftSample = left.Sample();
        Check(std::abs(std::abs(rightSample.bodyRotateY) - std::abs(leftSample.bodyRotateY)) < 1e-9 &&
            std::abs(std::abs(rightSample.bodyRotateZ) - std::abs(leftSample.bodyRotateZ)) < 1e-9,
            "mirror keeps body rotation magnitudes");
        Check(std::abs(rightSample.rootOffsetForward - leftSample.rootOffsetForward) < 1e-9 &&
            std::abs(rightSample.rootOffsetY - leftSample.rootOffsetY) < 1e-9,
            "mirror keeps root timing and magnitude channels");
    }
}

void TestTurnMotionStateAndGeometry()
{
    using namespace besktop;
    constexpr double pi = 3.14159265358979323846;
    constexpr double planeSide = 48.0;
    constexpr double focalLength = 576.0;
    const TurnActorGeometry geometry = BuildTurnActorGeometry(planeSide, planeSide);
    const double bodyCenterOffset = geometry.bodyCenterOffset;
    const double localIconCenterOffset = -bodyCenterOffset;

    Check(std::abs(SampleObservationOrbitYaw(0.0)) < 1e-9,
        "observation orbit starts at zero yaw");
    Check(std::abs(SampleObservationOrbitYaw(2.0) - pi * 0.5) < 1e-9,
        "observation orbit reaches 90 degrees at quarter cycle");
    Check(std::abs(SampleObservationOrbitYaw(4.0) - pi) < 1e-9,
        "observation orbit reaches 180 degrees at half cycle");
    Check(std::abs(SampleObservationOrbitYaw(6.0) - pi * 1.5) < 1e-9,
        "observation orbit reaches 270 degrees at three-quarter cycle");
    Check(std::abs(SampleObservationOrbitYaw(8.0)) < 1e-9,
        "observation orbit wraps after one full cycle");
    Check(std::abs(SampleObservationOrbitYaw(2.0, -1.0)) < 1e-9,
        "invalid observation orbit duration safely disables rotation");
    const GaitVec3 orbitProbe{12.0, -4.0, 7.0};
    for (const double time : {0.0, 2.0, 4.0, 6.0, 7.999}) {
        const GaitVec3 rotatedProbe = RotateAroundVerticalAxis(
            orbitProbe, SampleObservationOrbitYaw(time));
        Check(std::abs(Distance(GaitVec3{}, rotatedProbe) -
            Distance(GaitVec3{}, orbitProbe)) < 1e-9,
            "observation orbit preserves local geometry lengths");
    }

    Check(std::abs(geometry.limbWidth - 5.0) < 1e-9,
        "48px actor keeps the expected limb width");
    Check(geometry.visibleMargin >= 3.0,
        "48px actor reserves a visible screen-space margin");
    Check(std::abs(geometry.bodyAxisGap -
        ((geometry.limbWidth * 0.5) + geometry.visibleMargin)) < 1e-9,
        "body axis gap includes limb radius and visible margin");

    const auto stableVisibleGap = [&](TurnFacing facing) {
        const double facingSign = facing == TurnFacing::Right ? 1.0 : -1.0;
        const double rotateX = pi * 2.4 / 180.0;
        const double rotateY = FacingYaw(facing) + facingSign * pi * 5.0 / 180.0;
        const double rotateZ = facingSign * pi * 2.8 / 180.0;
        const GaitVec3 shoulderCenter{0.0, -planeSide * 0.20, 0.0};
        const GaitVec3 hipCenter{0.0, planeSide * 0.68, 0.0};
        const TurnProjectedPoint shoulder = ProjectTurnPointWithRotation(
            shoulderCenter, rotateX, rotateY, rotateZ, focalLength);
        const TurnProjectedPoint hip = ProjectTurnPointWithRotation(
            hipCenter, rotateX, rotateY, rotateZ, focalLength);
        const double axisX = hip.x - shoulder.x;
        const double axisY = hip.y - shoulder.y;
        const double axisLength = std::hypot(axisX, axisY);
        const auto signedDistance = [&](const GaitVec3& local) {
            const TurnProjectedPoint point = ProjectTurnPointWithRotation(
                local, rotateX, rotateY, rotateZ, focalLength);
            return (axisX * (point.y - shoulder.y) -
                axisY * (point.x - shoulder.x)) / axisLength;
        };
        const double halfWidth = geometry.planeWidth * 0.5;
        const double halfHeight = geometry.planeHeight * 0.5;
        const GaitVec3 corners[] = {
            {localIconCenterOffset - halfWidth, -halfHeight, 0.0},
            {localIconCenterOffset + halfWidth, -halfHeight, 0.0},
            {localIconCenterOffset + halfWidth, halfHeight, 0.0},
            {localIconCenterOffset - halfWidth, halfHeight, 0.0},
        };
        const double firstDistance = signedDistance(corners[0]);
        double nearestEdgeDistance = std::abs(firstDistance);
        for (const GaitVec3& corner : corners) {
            const double distance = signedDistance(corner);
            Check(distance * firstDistance > 0.0,
                "stable icon vertices stay on one side of the projected body axis");
            nearestEdgeDistance = std::min(nearestEdgeDistance, std::abs(distance));
        }
        return nearestEdgeDistance - (geometry.limbWidth * 0.5);
    };
    const double rightVisibleGap = stableVisibleGap(TurnFacing::Right);
    const double leftVisibleGap = stableVisibleGap(TurnFacing::Left);
    Check(rightVisibleGap >= 3.0,
        "stable right-facing walk keeps a visible gap from the body axis");
    Check(leftVisibleGap >= 3.0,
        "stable left-facing walk keeps a visible gap from the body axis");
    Check(std::abs(rightVisibleGap - leftVisibleGap) < 0.05,
        "stable left and right visible gaps are symmetric");
    Check(ProjectTurnPoint(
        GaitVec3{localIconCenterOffset, 0.0, 0.0}, FacingYaw(TurnFacing::Right), focalLength).x < 0.0,
        "right-facing actor keeps the icon behind the body axis");
    Check(ProjectTurnPoint(
        GaitVec3{localIconCenterOffset, 0.0, 0.0}, FacingYaw(TurnFacing::Left), focalLength).x > 0.0,
        "left-facing actor keeps the icon behind the body axis");

    constexpr double capturedCenterX = 120.0;
    const double rightRootX = capturedCenterX - localIconCenterOffset;
    const double leftRootX = capturedCenterX + localIconCenterOffset;
    const double initialPositionError = std::max(
        std::abs((rightRootX + localIconCenterOffset) - capturedCenterX),
        std::abs((leftRootX - localIconCenterOffset) - capturedCenterX));
    Check(initialPositionError < 1e-9,
        "body-axis root conversion preserves the captured icon center");

    Check(ChooseTurnSafeInitialFacing(
        TurnFacing::Left, 30.0, localIconCenterOffset, planeSide * 0.5, 0.0, 320.0) ==
        TurnFacing::Right,
        "left-edge actor starts toward the safe turn arc");
    Check(ChooseTurnSafeInitialFacing(
        TurnFacing::Right, 290.0, localIconCenterOffset, planeSide * 0.5, 0.0, 320.0) ==
        TurnFacing::Left,
        "right-edge actor starts toward the safe turn arc");
    Check(ChooseTurnSafeInitialFacing(
        TurnFacing::Right, 160.0, localIconCenterOffset, planeSide * 0.5, 0.0, 320.0) ==
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
            GaitVec3{localIconCenterOffset - planeSide * 0.5, 0.0, 0.0}, yaw, focalLength);
        const TurnProjectedPoint right = ProjectTurnPoint(
            GaitVec3{localIconCenterOffset + planeSide * 0.5, 0.0, 0.0}, yaw, focalLength);
        return std::abs(right.x - left.x);
    };
    const double startWidth = projectedWidth(0.0);
    const double middleWidth = projectedWidth(pi * 0.5);
    const double endWidth = projectedWidth(pi);
    Check(middleWidth < startWidth * 0.05,
        "turn midpoint becomes a narrow plane edge");
    Check(std::abs(endWidth - startWidth) < 1e-9,
        "turn endpoint restores icon width");
    const GaitVec3 localIconCenter{localIconCenterOffset, 0.0, 0.0};
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
    Check(ProjectTurnPoint(localIconCenter, 0.0, focalLength).x < 0.0 && endIconCenterX > 0.0,
        "icon center finishes on the opposite side of body axis");
    Check(std::abs(middleIconCenterX) < 1e-9,
        "icon center passes body axis near the edge-on midpoint");
    Check(maximumIconCenterStep < planeSide * 0.06,
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
              << ", axis gap=" << geometry.bodyAxisGap
              << ", limb width=" << geometry.limbWidth
              << ", visible gap L/R=" << leftVisibleGap << "/" << rightVisibleGap
              << ", initial error=" << initialPositionError
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
    TestActionMetadataAndDefenseWindows();
    TestPunchPoseMathematics();
    TestKickPoseMathematics();
    TestDefenseAndFeedbackMathematics();
    TestMirroringKeepsTimeline();
    TestSideKickGeometryAndEvents();
    TestGaitGeometry();
    TestTurnMotionStateAndGeometry();
    if (failures == 0) {
        std::cout << "besktop_action_tests: all checks passed\n";
    }
    return failures == 0 ? 0 : 1;
}
