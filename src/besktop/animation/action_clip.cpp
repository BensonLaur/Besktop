#include "besktop/animation/action_clip.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr std::uint32_t kContactEventMask = 1u << 0;

using besktop::ActionAttackType;
using besktop::ActionClip;
using besktop::ActionDefenseWindow;
using besktop::ActionDefenseWindowType;
using besktop::ActionEvent;
using besktop::ActionEventType;
using besktop::ActionHitStrength;
using besktop::ActionId;

constexpr ActionEvent kLeadStraightEvents[] = {{ActionEventType::Contact, 0.30, kContactEventMask}};
constexpr ActionEvent kRearStraightEvents[] = {{ActionEventType::Contact, 0.33, kContactEventMask}};
constexpr ActionEvent kUppercutEvents[] = {{ActionEventType::Contact, 0.38, kContactEventMask}};
constexpr ActionEvent kHookEvents[] = {{ActionEventType::Contact, 0.34, kContactEventMask}};
constexpr ActionEvent kSwingPunchEvents[] = {{ActionEventType::Contact, 0.48, kContactEventMask}};
constexpr ActionEvent kFrontKickEvents[] = {{ActionEventType::Contact, 0.35, kContactEventMask}};
constexpr ActionEvent kSideKickEvents[] = {{ActionEventType::Contact, 0.35, kContactEventMask}};
constexpr ActionEvent kRoundhouseKickEvents[] = {{ActionEventType::Contact, 0.43, kContactEventMask}};
constexpr ActionEvent kSpinningBackKickEvents[] = {{ActionEventType::Contact, 0.62, kContactEventMask}};

constexpr ActionClip kNoneClip{};
constexpr ActionClip kLeadStraightClip{
    ActionId::LeadStraight, 0.18, 0.30, 0.34, 0.58, 0.72,
    kLeadStraightEvents, std::size(kLeadStraightEvents), ActionAttackType::Punch, ActionHitStrength::Light};
constexpr ActionClip kRearStraightClip{
    ActionId::RearStraight, 0.20, 0.33, 0.38, 0.64, 0.80,
    kRearStraightEvents, std::size(kRearStraightEvents), ActionAttackType::Punch, ActionHitStrength::Heavy};
constexpr ActionClip kUppercutClip{
    ActionId::Uppercut, 0.22, 0.38, 0.44, 0.70, 0.88,
    kUppercutEvents, std::size(kUppercutEvents), ActionAttackType::Punch, ActionHitStrength::Heavy};
constexpr ActionClip kHookClip{
    ActionId::Hook, 0.20, 0.34, 0.40, 0.65, 0.82,
    kHookEvents, std::size(kHookEvents), ActionAttackType::Punch, ActionHitStrength::Light};
constexpr ActionClip kSwingPunchClip{
    ActionId::SwingPunch, 0.28, 0.48, 0.54, 0.86, 1.04,
    kSwingPunchEvents, std::size(kSwingPunchEvents), ActionAttackType::Punch, ActionHitStrength::Heavy};
constexpr ActionClip kLaybackClip{
    ActionId::Layback, 0.16, 0.28, 0.34, 0.58, 0.76,
    nullptr, 0, ActionAttackType::None, ActionHitStrength::None,
    ActionDefenseWindow{ActionDefenseWindowType::Evade, 0.16, 0.40}};
constexpr ActionClip kSlipLeftClip{
    ActionId::SlipLeft, 0.14, 0.26, 0.32, 0.52, 0.66,
    nullptr, 0, ActionAttackType::None, ActionHitStrength::None,
    ActionDefenseWindow{ActionDefenseWindowType::Evade, 0.14, 0.36}};
constexpr ActionClip kSlipRightClip{
    ActionId::SlipRight, 0.14, 0.26, 0.32, 0.52, 0.66,
    nullptr, 0, ActionAttackType::None, ActionHitStrength::None,
    ActionDefenseWindow{ActionDefenseWindowType::Evade, 0.14, 0.36}};
constexpr ActionClip kParryClip{
    ActionId::Parry, 0.12, 0.22, 0.28, 0.42, 0.56,
    nullptr, 0, ActionAttackType::None, ActionHitStrength::None,
    ActionDefenseWindow{ActionDefenseWindowType::Parry, 0.12, 0.30}};
constexpr ActionClip kFrontKickClip{
    ActionId::FrontKick, 0.20, 0.35, 0.41, 0.72, 0.88,
    kFrontKickEvents, std::size(kFrontKickEvents), ActionAttackType::Kick, ActionHitStrength::Light};
constexpr ActionClip kSideKickClip{
    ActionId::SideKick, 0.20, 0.35, 0.41, 0.72, 0.86,
    kSideKickEvents, std::size(kSideKickEvents), ActionAttackType::Kick, ActionHitStrength::Heavy};
constexpr ActionClip kRoundhouseKickClip{
    ActionId::RoundhouseKick, 0.24, 0.43, 0.49, 0.79, 1.02,
    kRoundhouseKickEvents, std::size(kRoundhouseKickEvents), ActionAttackType::Kick, ActionHitStrength::Heavy};
constexpr ActionClip kSpinningBackKickClip{
    ActionId::SpinningBackKick, 0.32, 0.62, 0.68, 1.16, 1.38,
    kSpinningBackKickEvents, std::size(kSpinningBackKickEvents), ActionAttackType::Kick, ActionHitStrength::Heavy,
    {}, false, 0.16};
constexpr ActionClip kLightHitReactClip{
    ActionId::LightHitReact, 0.08, 0.16, 0.22, 0.46, 0.62};
constexpr ActionClip kHeavyStaggerClip{
    ActionId::HeavyStagger, 0.16, 0.30, 0.38, 0.82, 1.12,
    nullptr, 0, ActionAttackType::None, ActionHitStrength::None,
    {}, false, -0.16};
constexpr ActionClip kWhiffRecoveryClip{
    ActionId::WhiffRecovery, 0.18, 0.30, 0.36, 0.78, 0.96,
    nullptr, 0, ActionAttackType::None, ActionHitStrength::None, {}, true};

double Clamp01(double value)
{
    return std::clamp(value, 0.0, 1.0);
}

double Smooth(double value)
{
    const double t = Clamp01(value);
    return t * t * (3.0 - 2.0 * t);
}

double Segment(double time, double start, double end)
{
    return end > start ? Smooth((time - start) / (end - start)) : (time >= end ? 1.0 : 0.0);
}

double HoldAndReturn(double time, double riseStart, double riseEnd, double fallStart, double fallEnd)
{
    return Segment(time, riseStart, riseEnd) * (1.0 - Segment(time, fallStart, fallEnd));
}

void EnableHands(besktop::ActionSample& sample, double weight)
{
    sample.leadHandTargetEnabled = true;
    sample.rearHandTargetEnabled = true;
    sample.handTargetWeight = weight;
}

void SetGuardHands(besktop::ActionSample& sample)
{
    sample.leadHandForward = 0.08;
    sample.leadHandY = -0.27;
    sample.leadHandDepth = 0.24;
    sample.rearHandForward = 0.06;
    sample.rearHandY = -0.25;
    sample.rearHandDepth = 0.21;
}

void EnableFeet(besktop::ActionSample& sample)
{
    sample.leadFootTargetEnabled = true;
    sample.rearFootTargetEnabled = true;
    sample.footTargetWeight = 1.0;
    sample.lowerBodyActionRotationWeight = 0.0;
}

} // namespace

namespace besktop {

ActionId ParseActionId(std::wstring_view name)
{
    constexpr std::pair<std::wstring_view, ActionId> names[] = {
        {L"swing_punch", ActionId::SwingPunch},
        {L"lead_straight", ActionId::LeadStraight},
        {L"rear_straight", ActionId::RearStraight},
        {L"hook", ActionId::Hook},
        {L"uppercut", ActionId::Uppercut},
        {L"front_kick", ActionId::FrontKick},
        {L"side_kick", ActionId::SideKick},
        {L"roundhouse_kick", ActionId::RoundhouseKick},
        {L"spinning_back_kick", ActionId::SpinningBackKick},
        {L"layback", ActionId::Layback},
        {L"slip_left", ActionId::SlipLeft},
        {L"slip_right", ActionId::SlipRight},
        {L"parry", ActionId::Parry},
        {L"light_hit_react", ActionId::LightHitReact},
        {L"heavy_stagger", ActionId::HeavyStagger},
        {L"whiff_recovery", ActionId::WhiffRecovery},
    };
    for (const auto& [candidate, id] : names) {
        if (name == candidate) {
            return id;
        }
    }
    return ActionId::None;
}

std::wstring_view ActionIdName(ActionId id)
{
    switch (id) {
    case ActionId::SwingPunch: return L"swing_punch";
    case ActionId::LeadStraight: return L"lead_straight";
    case ActionId::RearStraight: return L"rear_straight";
    case ActionId::Hook: return L"hook";
    case ActionId::Uppercut: return L"uppercut";
    case ActionId::FrontKick: return L"front_kick";
    case ActionId::SideKick: return L"side_kick";
    case ActionId::RoundhouseKick: return L"roundhouse_kick";
    case ActionId::SpinningBackKick: return L"spinning_back_kick";
    case ActionId::Layback: return L"layback";
    case ActionId::SlipLeft: return L"slip_left";
    case ActionId::SlipRight: return L"slip_right";
    case ActionId::Parry: return L"parry";
    case ActionId::LightHitReact: return L"light_hit_react";
    case ActionId::HeavyStagger: return L"heavy_stagger";
    case ActionId::WhiffRecovery: return L"whiff_recovery";
    case ActionId::None: return L"none";
    }
    return L"none";
}

const ActionClip& GetActionClip(ActionId id)
{
    switch (id) {
    case ActionId::SwingPunch: return kSwingPunchClip;
    case ActionId::LeadStraight: return kLeadStraightClip;
    case ActionId::RearStraight: return kRearStraightClip;
    case ActionId::Hook: return kHookClip;
    case ActionId::Uppercut: return kUppercutClip;
    case ActionId::FrontKick: return kFrontKickClip;
    case ActionId::SideKick: return kSideKickClip;
    case ActionId::RoundhouseKick: return kRoundhouseKickClip;
    case ActionId::SpinningBackKick: return kSpinningBackKickClip;
    case ActionId::Layback: return kLaybackClip;
    case ActionId::SlipLeft: return kSlipLeftClip;
    case ActionId::SlipRight: return kSlipRightClip;
    case ActionId::Parry: return kParryClip;
    case ActionId::LightHitReact: return kLightHitReactClip;
    case ActionId::HeavyStagger: return kHeavyStaggerClip;
    case ActionId::WhiffRecovery: return kWhiffRecoveryClip;
    default: return kNoneClip;
    }
}

ActionPhase ActionPhaseAt(const ActionClip& clip, double localTimeSeconds)
{
    if (clip.id == ActionId::None || localTimeSeconds >= clip.duration) return ActionPhase::Complete;
    if (localTimeSeconds < clip.prepareEnd) return ActionPhase::Prepare;
    if (localTimeSeconds < clip.activeEnd) return ActionPhase::Active;
    if (localTimeSeconds < clip.contactEnd) return ActionPhase::Contact;
    if (localTimeSeconds < clip.recoverEnd) return ActionPhase::Recover;
    return ActionPhase::Complete;
}

ActionDefenseWindowType ActionDefenseWindowAt(const ActionClip& clip, double localTimeSeconds)
{
    return clip.defenseWindow.type != ActionDefenseWindowType::None &&
        localTimeSeconds >= clip.defenseWindow.startSeconds &&
        localTimeSeconds < clip.defenseWindow.endSeconds ?
        clip.defenseWindow.type : ActionDefenseWindowType::None;
}

ActionSample SampleAction(const ActionClip& clip, double localTimeSeconds, double direction)
{
    ActionSample sample;
    if (clip.id == ActionId::None || clip.duration <= 0.0 || localTimeSeconds >= clip.duration) {
        return sample;
    }

    const double time = std::clamp(localTimeSeconds, 0.0, clip.duration);
    const double mirror = direction < 0.0 ? -1.0 : 1.0;
    const double actionWeight = 1.0 - Segment(time, clip.recoverEnd, clip.duration);
    const double handWeight = Segment(time, 0.0, std::min(0.08, clip.prepareEnd)) * actionWeight;

    switch (clip.id) {
    case ActionId::LeadStraight: {
        const double prepare = Segment(time, 0.0, clip.prepareEnd);
        const double strike = HoldAndReturn(time, clip.prepareEnd, clip.activeEnd, clip.contactEnd, clip.recoverEnd);
        EnableHands(sample, handWeight);
        SetGuardHands(sample);
        // Drive the punch from the shoulder girdle and torso instead of
        // treating the fist as an isolated end effector. Pitch lifts the lead
        // shoulder and drops the rear shoulder because their local depths are
        // opposite; yaw advances/retracts them around the shared body axis.
        sample.bodyRotateX = 4.0 * kPi / 180.0 * strike;
        sample.bodyRotateY = mirror * ((-4.0 * prepare) + (15.0 * strike)) * kPi / 180.0 * actionWeight;
        sample.bodyRotateZ = mirror * -3.5 * kPi / 180.0 * strike;
        sample.rootOffsetForward = 0.04 * strike;
        sample.rootOffsetY = 0.018 * strike;
        sample.punchStrength = strike;
        sample.leadShoulderForwardOffset = 0.05 * strike;
        sample.leadShoulderYOffset = -0.02 * strike;
        sample.rearShoulderForwardOffset = -0.02 * strike;
        sample.rearShoulderYOffset = 0.012 * strike;
        // The striking hand starts and recovers in a real guard target rather
        // than collapsing back onto its shoulder. This keeps the fixed-length
        // two-bone arm from folding into a false elbow-strike silhouette.
        sample.leadHandForward = 0.32 - 0.02 * prepare + 0.29 * strike;
        sample.leadHandY = -0.12 + 0.12 * strike;
        sample.leadHandDepth = 0.08 + 0.16 * strike;
        // Keep the non-striking hand in a compact guard in front of the
        // torso. The lower target works with the downward IK bend hint so the
        // elbow stays below the shoulder instead of reading as an elbow hit.
        sample.rearHandForward = 0.18;
        sample.rearHandY = -0.01;
        sample.rearHandDepth = 0.12;
        sample.rearArmBendForward = 0.62;
        break;
    }
    case ActionId::RearStraight: {
        const double coil = Segment(time, 0.0, clip.prepareEnd) * actionWeight;
        const double strike = HoldAndReturn(time, clip.prepareEnd, clip.activeEnd, clip.contactEnd, clip.recoverEnd);
        EnableHands(sample, handWeight);
        SetGuardHands(sample);
        // Load the rear shoulder behind the body axis, then drive it through
        // with a larger torso turn than the lead straight. Negative pitch
        // raises the rear shoulder and lowers the lead shoulder because the
        // rear shoulder occupies the opposite local depth plane.
        sample.bodyRotateX = -5.0 * kPi / 180.0 * strike;
        sample.bodyRotateY = mirror * (8.0 * coil - 24.0 * strike) * kPi / 180.0;
        sample.bodyRotateZ = mirror * -4.5 * strike * kPi / 180.0;
        sample.rootOffsetForward = 0.055 * strike;
        sample.rootOffsetY = 0.022 * strike;
        sample.punchStrength = strike;
        sample.rearShoulderForwardOffset = 0.065 * strike;
        sample.rearShoulderYOffset = -0.025 * strike;
        sample.leadShoulderForwardOffset = -0.025 * strike;
        sample.leadShoulderYOffset = 0.014 * strike;
        // The rear fist travels from a stable guard through one long
        // extension. Keeping a real guard target avoids a false elbow strike
        // during both the load and recovery portions of the clip.
        sample.rearHandForward = 0.32 - 0.02 * coil + 0.30 * strike;
        sample.rearHandY = -0.12 + 0.12 * strike;
        sample.rearHandDepth = 0.08 + 0.15 * strike;
        // The lead hand stays compact in front of the torso while the rear
        // shoulder rotates through the punch.
        sample.leadHandForward = 0.18;
        sample.leadHandY = -0.01;
        sample.leadHandDepth = 0.12;
        sample.leadArmBendForward = 0.62;
        break;
    }
    case ActionId::Uppercut: {
        const double load = Segment(time, 0.0, clip.prepareEnd) * actionWeight;
        const double strike = HoldAndReturn(time, clip.prepareEnd, clip.activeEnd, clip.contactEnd, clip.recoverEnd);
        EnableHands(sample, handWeight);
        SetGuardHands(sample);
        // Compress slightly into the rear side, then release upward through
        // the rear shoulder. The rear shoulder is the striking shoulder for
        // this first uppercut clip, matching rear_straight's logical side.
        sample.bodyRotateX = (3.0 * load - 7.0 * strike) * kPi / 180.0;
        sample.bodyRotateY = mirror * (5.0 * load - 14.0 * strike) * kPi / 180.0;
        sample.bodyRotateZ = mirror * -2.5 * strike * kPi / 180.0;
        sample.rootOffsetForward = 0.025 * strike;
        sample.rootOffsetY = 0.035 * load - 0.08 * strike;
        sample.punchStrength = strike;
        sample.rearShoulderForwardOffset = -0.01 * load + 0.04 * strike;
        sample.rearShoulderYOffset = 0.018 * load - 0.045 * strike;
        sample.leadShoulderForwardOffset = -0.015 * strike;
        sample.leadShoulderYOffset = 0.012 * strike;
        // Chamber with a bent elbow below the torso, then drive the fist
        // upward and slightly forward. The target never collapses onto the
        // shoulder, so prepare and recovery do not resemble elbow strikes.
        sample.rearHandForward = 0.32 - 0.03 * load + 0.09 * strike;
        sample.rearHandY = -0.08 + 0.22 * load - 0.42 * strike;
        sample.rearHandDepth = 0.10 + 0.05 * load + 0.08 * strike;
        sample.rearArmBendForward = 0.28;
        // Reuse the reviewed compact guard on the non-striking lead side.
        sample.leadHandForward = 0.18;
        sample.leadHandY = -0.01;
        sample.leadHandDepth = 0.12;
        sample.leadArmBendForward = 0.62;
        break;
    }
    case ActionId::Hook: {
        const double coil = Segment(time, 0.0, clip.prepareEnd) * actionWeight;
        const double strike = HoldAndReturn(time, clip.prepareEnd, clip.activeEnd, clip.contactEnd, clip.recoverEnd);
        const double curvedArc = std::sin(Clamp01(strike) * kPi * 0.5);
        EnableHands(sample, handWeight);
        SetGuardHands(sample);
        // A compact lead hook is carried by the lead shoulder around the body
        // axis. It keeps a bent elbow and a short horizontal depth arc rather
        // than extending like a straight punch.
        sample.bodyRotateX = 3.0 * kPi / 180.0 * strike;
        sample.bodyRotateY = mirror * (-8.0 * coil + 20.0 * strike) * kPi / 180.0;
        sample.bodyRotateZ = mirror * -2.5 * strike * kPi / 180.0;
        sample.rootOffsetForward = 0.025 * strike;
        sample.rootOffsetY = 0.012 * strike;
        sample.punchStrength = strike;
        sample.leadShoulderForwardOffset = 0.05 * strike;
        sample.leadShoulderYOffset = -0.015 * strike;
        sample.rearShoulderForwardOffset = -0.025 * strike;
        sample.rearShoulderYOffset = 0.012 * strike;
        sample.leadHandForward = 0.34 - 0.02 * coil + 0.06 * strike;
        sample.leadHandY = -0.10;
        sample.leadHandDepth = 0.10 + 0.28 * curvedArc;
        sample.leadArmBendForward = 0.22;
        sample.rearHandForward = 0.18;
        sample.rearHandY = -0.01;
        sample.rearHandDepth = 0.12;
        sample.rearArmBendForward = 0.62;
        break;
    }
    case ActionId::SwingPunch: {
        const double coil = Segment(time, 0.0, clip.prepareEnd) * actionWeight;
        const double strike = HoldAndReturn(time, clip.prepareEnd, clip.activeEnd, clip.contactEnd, clip.recoverEnd);
        const double curvedArc = std::sin(Clamp01(strike) * kPi * 0.5);
        EnableHands(sample, handWeight);
        SetGuardHands(sample);
        // The exaggerated swing uses the same lead-side shoulder chain as the
        // compact hook, but with a longer load, wider depth arc and larger
        // torso turn. The fist remains a valid two-bone IK target throughout.
        sample.bodyRotateX = 5.0 * kPi / 180.0 * strike;
        sample.bodyRotateY = mirror * (-12.0 * coil + 40.0 * strike) * kPi / 180.0;
        sample.bodyRotateZ = mirror * -7.0 * strike * kPi / 180.0;
        sample.rootOffsetForward = 0.07 * strike;
        sample.rootOffsetY = 0.025 * strike;
        sample.punchStrength = strike;
        sample.leadShoulderForwardOffset = 0.07 * strike;
        sample.leadShoulderYOffset = -0.02 * strike;
        sample.leadShoulderDepthOffset = 0.015 * strike;
        sample.rearShoulderForwardOffset = -0.03 * strike;
        sample.rearShoulderYOffset = 0.015 * strike;
        sample.leadHandForward = 0.40 - 0.04 * coil - 0.06 * strike;
        sample.leadHandY = -0.08;
        sample.leadHandDepth = 0.10 - 0.06 * coil + 0.58 * curvedArc;
        sample.leadArmBendForward = 0.12;
        sample.rearHandForward = 0.18;
        sample.rearHandY = -0.01;
        sample.rearHandDepth = 0.12;
        sample.rearArmBendForward = 0.61;
        break;
    }
    case ActionId::Layback: {
        const double lean = HoldAndReturn(time, 0.0, clip.activeEnd, clip.contactEnd, clip.recoverEnd);
        EnableFeet(sample);
        EnableHands(sample, handWeight);
        SetGuardHands(sample);
        // Withdraw the shoulder line while keeping the actor root and feet
        // nearly planted. Only a small fraction of the upper-body rotation is
        // allowed into the lower-body projection.
        sample.bodyRotateZ = mirror * -25.0 * kPi / 180.0 * lean;
        sample.bodyRotateX = -7.0 * kPi / 180.0 * lean;
        sample.lowerBodyActionRotationWeight = 0.04;
        sample.rootOffsetForward = -0.015 * lean;
        // Lower the hips into a shallow crouch while lifting both local foot
        // targets by the same amount. After root translation the feet remain
        // planted, and fixed-length IK bends both knees toward headingSign.
        sample.rootOffsetY = 0.035 * lean;
        sample.leadFootLift = 0.035 * lean;
        sample.rearFootLift = 0.035 * lean;
        sample.leadShoulderForwardOffset = -0.065 * lean;
        sample.leadShoulderYOffset = -0.012 * lean;
        sample.rearShoulderForwardOffset = -0.065 * lean;
        sample.rearShoulderYOffset = -0.012 * lean;
        sample.leadHandForward = 0.18;
        sample.leadHandY = -0.01;
        sample.leadHandDepth = 0.12;
        sample.leadArmBendForward = 0.62;
        sample.rearHandForward = 0.18;
        sample.rearHandY = -0.01;
        sample.rearHandDepth = 0.12;
        sample.rearArmBendForward = 0.62;
        sample.dodgeStrength = lean;
        break;
    }
    case ActionId::SlipLeft:
    case ActionId::SlipRight: {
        const double side = clip.id == ActionId::SlipLeft ? -1.0 : 1.0;
        const double slip = HoldAndReturn(time, 0.0, clip.activeEnd, clip.contactEnd, clip.recoverEnd);
        const bool slippingSideIsLead = side * mirror > 0.0;
        EnableFeet(sample);
        EnableHands(sample, handWeight);
        SetGuardHands(sample);
        // Reuse the verified layback crouch path: lower the actor root and
        // raise each planted foot target by nearly the same amount. The
        // slipping-side target rises slightly farther, producing a deeper
        // fixed-length IK bend without introducing a second leg solver.
        sample.rootOffsetY = 0.05 * slip;
        sample.upperBodyOffsetDepth = side * 0.30 * slip;
        sample.upperBodyOffsetY = 0.18 * slip;
        sample.leadFootLift = (slippingSideIsLead ? 0.065 : 0.05) * slip;
        sample.rearFootLift = (slippingSideIsLead ? 0.05 : 0.065) * slip;
        // Side slips bend around the actor's forward axis. This is distinct
        // from layback's screen-space Z lean and reads correctly under the
        // 360-degree diagnostic camera.
        sample.bodyRotateX = -side * 45.0 * kPi / 180.0 * slip;
        sample.bodyRotateY = mirror * side * 7.0 * kPi / 180.0 * slip;
        sample.lowerBodyActionRotationWeight = 0.03;
        sample.leadHandForward = 0.18;
        sample.leadHandY = -0.01;
        sample.leadHandDepth = 0.12;
        sample.leadArmBendForward = 0.62;
        sample.rearHandForward = 0.18;
        sample.rearHandY = -0.01;
        sample.rearHandDepth = 0.12;
        sample.rearArmBendForward = 0.62;
        sample.dodgeStrength = slip;
        break;
    }
    case ActionId::Parry: {
        const double parry = HoldAndReturn(time, 0.0, clip.activeEnd, clip.contactEnd, clip.recoverEnd);
        EnableHands(sample, handWeight);
        SetGuardHands(sample);
        // A parry is a compact lead-forearm sweep, not a raised block. Start
        // from the reviewed in-front guard, keep the elbow bent and carry the
        // fist a short distance outward while the rear hand protects center.
        sample.leadHandForward = 0.26 + 0.04 * parry;
        sample.leadHandY = -0.04 - 0.10 * parry;
        sample.leadHandDepth = 0.14 + 0.32 * parry;
        sample.leadArmBendForward = 0.48;
        sample.rearHandForward = 0.18;
        sample.rearHandY = -0.01;
        sample.rearHandDepth = 0.12;
        sample.rearArmBendForward = 0.62;
        // The icon actor has no visible chest or shoulder blades, so the
        // shoulder-line counter-motion must be exaggerated enough to carry
        // the icon plane. The lead shoulder advances, lifts and opens while
        // the rear shoulder retracts and settles.
        sample.leadShoulderForwardOffset = 0.06 * parry;
        sample.leadShoulderYOffset = -0.025 * parry;
        sample.leadShoulderDepthOffset = 0.055 * parry;
        sample.rearShoulderForwardOffset = -0.03 * parry;
        sample.rearShoulderYOffset = 0.015 * parry;
        sample.rearShoulderDepthOffset = -0.015 * parry;
        sample.bodyRotateX = 4.0 * parry * kPi / 180.0;
        sample.bodyRotateY = mirror * 18.0 * parry * kPi / 180.0;
        sample.bodyRotateZ = mirror * -2.5 * parry * kPi / 180.0;
        break;
    }
    case ActionId::FrontKick:
    case ActionId::SideKick:
    case ActionId::RoundhouseKick: {
        const bool sideKick = clip.id == ActionId::SideKick;
        const bool roundhouse = clip.id == ActionId::RoundhouseKick;
        const double recoverChamberEnd = roundhouse ? 0.67 : 0.55;
        const double chamber = Segment(time, 0.0, clip.prepareEnd) *
            (1.0 - Segment(time, recoverChamberEnd, clip.recoverEnd));
        const double extension = Segment(time, clip.prepareEnd, clip.activeEnd) *
            (1.0 - Segment(time, clip.contactEnd, recoverChamberEnd));
        const double balance = std::max(chamber * 0.55, extension);
        const double sideTurn = sideKick ?
            Segment(time, 0.0, clip.prepareEnd * 0.82) *
                (1.0 - Segment(time, recoverChamberEnd, clip.recoverEnd)) :
            0.0;
        const double roundhouseTurn = roundhouse ?
            Segment(time, 0.0, clip.prepareEnd * 0.85) *
                (1.0 - Segment(time, recoverChamberEnd, clip.recoverEnd)) :
            0.0;
        EnableFeet(sample);
        EnableHands(sample, handWeight);
        SetGuardHands(sample);
        sample.leadFootForwardOffset = (roundhouse ? 0.10 : 0.11) * chamber +
            (roundhouse ? 0.54 : (sideKick ? 0.70 : 0.59)) * extension;
        sample.leadFootLift = (roundhouse ? 0.30 : (sideKick ? 0.28 : 0.20)) * chamber +
            (roundhouse ? 0.44 : (sideKick ? 0.50 : 0.27)) * extension;
        sample.leadFootDepthOffset = (roundhouse ? 0.16 : (sideKick ? 0.03 : 0.0)) * chamber +
            (roundhouse ? 0.48 : (sideKick ? 0.02 : 0.0)) * extension;
        sample.bodyRotateY = mirror * (roundhouse ? 60.0 * roundhouseTurn :
            (sideKick ? 78.0 * sideTurn : 6.0 * balance)) * kPi / 180.0;
        sample.bodyRotateZ = mirror * (roundhouse ? -16.0 : (sideKick ? -19.0 : -11.0)) * kPi / 180.0 * balance;
        sample.lowerBodyRotateY = mirror * (roundhouse ? 52.0 * roundhouseTurn :
            (sideKick ? 78.0 * sideTurn : 0.0)) * kPi / 180.0;
        sample.kickStrength = extension;
        if (!sideKick && !roundhouse) {
            // A front kick keeps both hands in front while the icon torso
            // counters the extending leg. The stronger lean is intentional:
            // a card-backed actor needs a readable silhouette, not the subtle
            // balance adjustment of a realistic human rig.
            sample.bodyRotateX = 3.0 * kPi / 180.0 * balance;
            sample.leadHandForward = 0.20;
            sample.leadHandY = -0.02;
            sample.leadHandDepth = 0.13;
            sample.leadArmBendForward = 0.62;
            sample.rearHandForward = 0.18;
            sample.rearHandY = -0.01;
            sample.rearHandDepth = 0.12;
            sample.rearArmBendForward = 0.62;
            sample.leadShoulderForwardOffset = -0.02 * balance;
            sample.leadShoulderYOffset = 0.012 * balance;
            sample.rearShoulderForwardOffset = 0.025 * balance;
            sample.rearShoulderYOffset = -0.015 * balance;
        } else if (sideKick) {
            // Turn the card-backed torso clearly side-on and counterbalance
            // the higher horizontal leg with a guarded shoulder line. This
            // must read differently from front_kick even without a human hip
            // silhouette.
            sample.bodyRotateX = 0.0;
            sample.footTargetYawCompensationWeight = 1.0;
            sample.upperBodyOffsetForward = -0.10 * balance;
            sample.upperBodyOffsetY = 0.18 * balance;
            sample.leadHandForward = 0.18;
            sample.leadHandY = -0.02;
            sample.leadHandDepth = 0.14;
            sample.leadArmBendForward = 0.60;
            sample.rearHandForward = 0.16;
            sample.rearHandY = -0.01;
            sample.rearHandDepth = 0.12;
            sample.rearArmBendForward = 0.62;
            // For the right/lead-leg preview, the kicking-side shoulder rises
            // above the opposite shoulder as the torso leans away. Mirroring
            // keeps the same anatomical relationship for the other facing.
            sample.leadShoulderForwardOffset = -0.02 * balance;
            sample.leadShoulderYOffset = -0.05 * balance;
            sample.leadShoulderDepthOffset = -0.015 * balance;
            sample.rearShoulderForwardOffset = 0.02 * balance;
            sample.rearShoulderYOffset = 0.035 * balance;
            sample.rearShoulderDepthOffset = 0.02 * balance;
        } else {
            // The roundhouse is driven by a continuing pelvis sweep. Keep the
            // feet in action space while the hips turn underneath them, then
            // counterbalance with a guarded, asymmetrical shoulder line.
            sample.footTargetYawCompensationWeight = 1.0;
            sample.bodyRotateX = 3.0 * kPi / 180.0 * balance;
            sample.upperBodyOffsetForward = -0.04 * balance;
            sample.upperBodyOffsetY = 0.06 * balance;
            sample.leadHandForward = 0.16;
            sample.leadHandY = -0.03;
            sample.leadHandDepth = 0.15;
            sample.leadArmBendForward = 0.56;
            sample.rearHandForward = 0.19;
            sample.rearHandY = -0.01;
            sample.rearHandDepth = 0.12;
            sample.rearArmBendForward = 0.62;
            sample.leadShoulderForwardOffset = 0.04 * balance;
            sample.leadShoulderYOffset = -0.035 * balance;
            sample.leadShoulderDepthOffset = 0.03 * balance;
            sample.rearShoulderForwardOffset = -0.03 * balance;
            sample.rearShoulderYOffset = 0.025 * balance;
            sample.rearShoulderDepthOffset = -0.015 * balance;
        }
        break;
    }
    case ActionId::SpinningBackKick: {
        // The shoulders lead, the pelvis follows, Contact happens while the
        // actor is still back-facing, and Recover completes the full turn.
        // Keeping these curves separate prevents the old behavior where the
        // icon finished 360 degrees before the kick actually contacted.
        const double bodySpinTurns =
            0.36 * Segment(time, 0.0, clip.prepareEnd) +
            0.20 * Segment(time, clip.prepareEnd, clip.activeEnd) +
            0.04 * Segment(time, clip.activeEnd, clip.contactEnd) +
            0.40 * Segment(time, clip.contactEnd, clip.recoverEnd);
        const double pelvisSpinTurns =
            0.28 * Segment(time, 0.0, clip.prepareEnd) +
            0.23 * Segment(time, clip.prepareEnd, clip.activeEnd) +
            0.04 * Segment(time, clip.activeEnd, clip.contactEnd) +
            0.45 * Segment(time, clip.contactEnd, clip.recoverEnd);
        const double step = Segment(time, 0.0, 0.18);
        const double chamber = Segment(time, 0.16, clip.prepareEnd) *
            (1.0 - Segment(time, 0.88, 1.04));
        const double extension = Segment(time, clip.prepareEnd, clip.activeEnd) *
            (1.0 - Segment(time, clip.contactEnd, 0.84));
        const double landingStep = Segment(time, 1.04, 1.30);
        const double balance = std::max(chamber * 0.45, extension);
        const double rightHandRise = HoldAndReturn(time, 0.0, 0.18, 0.28, 0.48);
        const double handDepthSwap = HoldAndReturn(time, 0.16, 0.38, 0.80, 1.06);
        EnableFeet(sample);
        EnableHands(sample, handWeight);
        SetGuardHands(sample);
        sample.bodyRotateY = mirror * 2.0 * kPi * bodySpinTurns;
        sample.lowerBodyRotateY = mirror * 2.0 * kPi * pelvisSpinTurns;
        sample.footTargetYawCompensationWeight = 1.0;
        sample.footTargetRootCompensationWeight = 1.0;
        // Move the body axis around the planted right/lead foot. The 0.18
        // term approximates the projected hip radius and makes the support
        // leg nearly vertical throughout the spin instead of leaning back to
        // an unmoved root. A full turn finishes 0.16 planeSide forward.
        sample.rootOffsetForward = 0.16 * step -
            0.22 * std::sin(std::abs(sample.lowerBodyRotateY));
        sample.rootOffsetY = 0.14 * balance;
        sample.bodyRotateX = 0.0;
        sample.bodyRotateZ = mirror * -72.0 * kPi / 180.0 * extension;
        sample.upperBodyOffsetForward = 0.12 * extension;
        sample.upperBodyOffsetY = 0.18 * extension;
        // Facing right: the right/lead foot steps forward and becomes the
        // planted pivot. The left/rear leg chambers and performs the kick.
        sample.leadFootForwardOffset = 0.16 * step;
        sample.leadFootDepthOffset = 0.02 * step;
        // After rechambering, bring the kicking foot onto the actor's new
        // root position before root motion is committed. Without this late
        // landing step the foot stays planted at the old position and jumps
        // forward by 0.16 planeSide on the first neutral frame.
        sample.rearFootForwardOffset =
            0.10 * chamber + 1.05 * extension + 0.16 * landingStep;
        sample.rearFootLift = 0.32 * chamber + 0.60 * extension;
        sample.rearFootDepthOffset = 0.10 * chamber + 0.04 * extension;
        sample.kickStrength = extension;
        sample.leadHandForward = 0.20;
        sample.leadHandY = -0.02 - 0.28 * rightHandRise;
        sample.leadHandDepth = 0.14 - 0.28 * handDepthSwap;
        sample.leadArmBendForward = 0.52;
        sample.rearHandForward = 0.18;
        sample.rearHandY = -0.01 - 0.08 * handDepthSwap;
        sample.rearHandDepth = 0.12 - 0.32 * handDepthSwap;
        sample.rearArmBendForward = 0.62;
        sample.leadShoulderForwardOffset = 0.03 * balance;
        sample.leadShoulderYOffset = -0.025 * rightHandRise + 0.03 * extension;
        sample.leadShoulderDepthOffset = -0.025 * handDepthSwap;
        sample.rearShoulderForwardOffset = -0.025 * balance;
        sample.rearShoulderYOffset = -0.025 * extension;
        sample.rearShoulderDepthOffset = 0.035 * handDepthSwap;
        break;
    }
    case ActionId::LightHitReact: {
        // A light hit should read as a sharp involuntary recoil, not as an
        // intentional layback. The shoulder line snaps first, the planted
        // knees absorb a much smaller downward impulse, then a restrained
        // counter-sway settles the actor back into guard.
        const double snap = HoldAndReturn(time, 0.0, 0.10, clip.contactEnd, 0.44);
        const double armJolt = HoldAndReturn(time, 0.02, 0.11, 0.20, 0.40);
        const double kneeGive = HoldAndReturn(time, 0.03, 0.14, 0.26, clip.recoverEnd);
        const double settle = HoldAndReturn(time, 0.24, 0.34, 0.42, 0.54);
        EnableHands(sample, handWeight);
        SetGuardHands(sample);
        EnableFeet(sample);
        sample.footTargetRootCompensationWeight = 1.0;
        sample.bodyRotateX = 5.0 * kPi / 180.0 * snap;
        sample.bodyRotateZ = mirror * ((-18.0 * snap) + (3.0 * settle)) * kPi / 180.0;
        sample.bodyRotateY = mirror * ((-16.0 * snap) + (2.0 * settle)) * kPi / 180.0;
        sample.rootOffsetForward = -0.08 * snap;
        sample.rootOffsetY = 0.045 * kneeGive;
        sample.upperBodyOffsetForward = -0.055 * snap;
        sample.upperBodyOffsetY = 0.025 * snap;
        sample.hitStrength = snap;
        sample.lowerBodyActionRotationWeight = 0.08;
        // The near hand is knocked away and down for a frame while the rear
        // hand braces in front of the body. Their unequal targets keep the
        // silhouette distinct from an attacking punch or symmetric guard.
        sample.leadHandForward = 0.13 - 0.10 * armJolt;
        sample.leadHandY = -0.10 + 0.11 * armJolt;
        sample.leadHandDepth = 0.22 + 0.14 * armJolt;
        sample.leadArmBendForward = 0.48;
        sample.rearHandForward = 0.18 - 0.03 * armJolt;
        sample.rearHandY = -0.02 + 0.025 * armJolt;
        sample.rearHandDepth = 0.11;
        sample.rearArmBendForward = 0.64;
        sample.leadShoulderForwardOffset = -0.06 * snap;
        sample.leadShoulderYOffset = 0.035 * snap;
        sample.leadShoulderDepthOffset = 0.035 * snap;
        sample.rearShoulderForwardOffset = -0.018 * snap;
        sample.rearShoulderYOffset = -0.012 * snap;
        sample.rearShoulderDepthOffset = -0.018 * snap;
        break;
    }
    case ActionId::HeavyStagger: {
        const double impact = HoldAndReturn(time, 0.0, 0.16, 0.46, 0.88);
        const double armFlail = HoldAndReturn(time, 0.03, 0.18, 0.40, 0.76);
        const double legLoad = HoldAndReturn(time, 0.04, 0.18, 0.62, 0.96);
        const double retreat = Segment(time, 0.05, 0.34);
        const double settleForward = Segment(time, 0.70, 1.02);
        const double rearCatch = Segment(time, 0.08, 0.34);
        const double rearCatchLift = HoldAndReturn(time, 0.08, 0.18, 0.25, 0.40);
        const double leadGather = Segment(time, 0.38, 0.78);
        const double leadGatherLift = HoldAndReturn(time, 0.38, 0.52, 0.62, 0.80);
        EnableHands(sample, handWeight);
        SetGuardHands(sample);
        EnableFeet(sample);
        sample.footTargetYawCompensationWeight = 1.0;
        sample.footTargetRootCompensationWeight = 1.0;
        sample.bodyRotateZ = mirror * -30.0 * kPi / 180.0 * impact;
        sample.bodyRotateY = mirror * -18.0 * kPi / 180.0 * impact;
        sample.bodyRotateX = -11.0 * kPi / 180.0 * impact;
        sample.rootOffsetForward = -0.24 * retreat + 0.08 * settleForward;
        sample.rootOffsetY = 0.075 * legLoad;
        sample.upperBodyOffsetForward = -0.08 * impact;
        sample.upperBodyOffsetY = 0.045 * impact;
        sample.hitStrength = impact;
        sample.lowerBodyActionRotationWeight = 0.16;
        // The rear foot catches the displaced center of mass first; the lead
        // foot then gathers underneath the body. Both finish at the clip's
        // committed -0.16 root displacement so the return to neutral cannot
        // teleport either foot.
        sample.rearFootForwardOffset = -0.19 * rearCatch + 0.03 * settleForward;
        sample.rearFootLift = 0.10 * rearCatchLift;
        sample.rearFootDepthOffset = -0.05 * rearCatch * (1.0 - settleForward);
        sample.leadFootForwardOffset = -0.16 * leadGather;
        sample.leadFootLift = 0.065 * leadGatherLift;
        // Break the guard asymmetrically: the near arm is thrown outward and
        // down while the far hand trails behind the shoulder before recovery.
        sample.leadHandForward = 0.04 - 0.12 * armFlail;
        sample.leadHandY = 0.02 + 0.16 * armFlail;
        sample.leadHandDepth = 0.34 + 0.16 * armFlail;
        sample.leadArmBendForward = 0.35;
        sample.rearHandForward = -0.02 - 0.08 * armFlail;
        sample.rearHandY = 0.04 + 0.10 * armFlail;
        sample.rearHandDepth = 0.02;
        sample.rearArmBendForward = 0.42;
        sample.leadShoulderForwardOffset = -0.10 * impact;
        sample.leadShoulderYOffset = 0.055 * impact;
        sample.leadShoulderDepthOffset = 0.055 * impact;
        sample.rearShoulderForwardOffset = -0.045 * impact;
        sample.rearShoulderYOffset = 0.018 * impact;
        sample.rearShoulderDepthOffset = -0.035 * impact;
        break;
    }
    case ActionId::WhiffRecovery: {
        // Torso and striking arm recover on deliberately different curves:
        // the hips start braking first, while the missed hand continues past
        // the target line and remains exposed for a short punish window.
        const double torsoOverreach = HoldAndReturn(time, 0.0, 0.18, 0.40, 0.72);
        const double armOverreach = HoldAndReturn(time, 0.0, 0.22, 0.46, 0.82);
        const double brace = HoldAndReturn(time, 0.08, 0.22, 0.54, 0.82);
        const double leadStep = HoldAndReturn(time, 0.06, 0.20, 0.48, 0.82);
        const double leadStepLift = HoldAndReturn(time, 0.06, 0.14, 0.20, 0.34);
        const double settle = HoldAndReturn(time, 0.68, 0.78, 0.82, 0.94);
        EnableHands(sample, handWeight);
        SetGuardHands(sample);
        EnableFeet(sample);
        sample.footTargetYawCompensationWeight = 1.0;
        sample.footTargetRootCompensationWeight = 1.0;
        sample.bodyRotateX = 6.0 * kPi / 180.0 * torsoOverreach;
        sample.bodyRotateY = mirror * ((32.0 * torsoOverreach) - (4.0 * settle)) * kPi / 180.0;
        sample.bodyRotateZ = mirror * ((15.0 * torsoOverreach) - (2.5 * settle)) * kPi / 180.0;
        sample.rootOffsetForward = 0.17 * torsoOverreach;
        sample.rootOffsetY = 0.045 * brace;
        sample.upperBodyOffsetForward = 0.065 * torsoOverreach;
        sample.upperBodyOffsetY = 0.025 * torsoOverreach;
        sample.whiffRecoveryStrength = std::max(torsoOverreach, armOverreach);
        sample.lowerBodyActionRotationWeight = 0.22;
        sample.leadFootForwardOffset = 0.09 * leadStep;
        sample.leadFootLift = 0.04 * leadStepLift;
        // Keep the missed hand beyond the target line after the hips have
        // already begun to recover. The other hand also drops slightly for
        // balance, leaving a readable but short-lived opening.
        sample.leadHandForward = 0.30 + 0.34 * armOverreach;
        sample.leadHandY = -0.10 + 0.15 * armOverreach;
        sample.leadHandDepth = 0.16 + 0.24 * armOverreach;
        sample.leadArmBendForward = 0.28;
        sample.rearHandForward = 0.12 - 0.08 * torsoOverreach;
        sample.rearHandY = -0.01 + 0.10 * torsoOverreach;
        sample.rearHandDepth = 0.10;
        sample.rearArmBendForward = 0.48;
        sample.leadShoulderForwardOffset = 0.10 * torsoOverreach;
        sample.leadShoulderYOffset = 0.035 * torsoOverreach;
        sample.leadShoulderDepthOffset = 0.06 * torsoOverreach;
        sample.rearShoulderForwardOffset = -0.04 * torsoOverreach;
        sample.rearShoulderYOffset = -0.012 * torsoOverreach;
        sample.rearShoulderDepthOffset = -0.035 * torsoOverreach;
        break;
    }
    default:
        return ActionSample{};
    }
    return sample;
}

} // namespace besktop
