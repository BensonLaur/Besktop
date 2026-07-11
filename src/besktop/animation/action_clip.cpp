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
    kSpinningBackKickEvents, std::size(kSpinningBackKickEvents), ActionAttackType::Kick, ActionHitStrength::Heavy};
constexpr ActionClip kLightHitReactClip{
    ActionId::LightHitReact, 0.08, 0.16, 0.22, 0.46, 0.62};
constexpr ActionClip kHeavyStaggerClip{
    ActionId::HeavyStagger, 0.16, 0.30, 0.38, 0.82, 1.12};
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
        sample.bodyRotateY = mirror * ((-4.0 * prepare) + (13.0 * strike)) * kPi / 180.0 * actionWeight;
        sample.bodyRotateZ = mirror * -3.0 * kPi / 180.0 * strike;
        sample.rootOffsetForward = 0.025 * strike;
        sample.punchStrength = strike;
        sample.leadHandForward = -0.03 * prepare + 0.59 * strike;
        sample.leadHandY = -0.04 + 0.04 * strike;
        break;
    }
    case ActionId::RearStraight: {
        const double coil = Segment(time, 0.0, clip.prepareEnd) * actionWeight;
        const double strike = HoldAndReturn(time, clip.prepareEnd, clip.activeEnd, clip.contactEnd, clip.recoverEnd);
        EnableHands(sample, handWeight);
        SetGuardHands(sample);
        sample.bodyRotateY = mirror * (-8.0 * coil + 24.0 * strike) * kPi / 180.0;
        sample.bodyRotateZ = mirror * -4.0 * strike * kPi / 180.0;
        sample.rootOffsetForward = 0.04 * strike;
        sample.punchStrength = strike;
        sample.rearHandForward = -0.06 * coil + 0.60 * strike;
        sample.rearHandY = -0.03;
        sample.rearHandDepth = 0.22;
        break;
    }
    case ActionId::Uppercut: {
        const double load = Segment(time, 0.0, clip.prepareEnd) * actionWeight;
        const double strike = HoldAndReturn(time, clip.prepareEnd, clip.activeEnd, clip.contactEnd, clip.recoverEnd);
        EnableHands(sample, handWeight);
        SetGuardHands(sample);
        sample.bodyRotateX = (5.0 * load - 8.0 * strike) * kPi / 180.0;
        sample.bodyRotateY = mirror * 14.0 * strike * kPi / 180.0;
        sample.rootOffsetY = 0.055 * load - 0.035 * strike;
        sample.punchStrength = strike;
        sample.rearHandForward = 0.02 + 0.23 * strike;
        sample.rearHandY = 0.15 * load - 0.45 * strike;
        sample.rearHandDepth = 0.18 + 0.08 * strike;
        break;
    }
    case ActionId::Hook:
    case ActionId::SwingPunch: {
        const bool swing = clip.id == ActionId::SwingPunch;
        const double coil = Segment(time, 0.0, clip.prepareEnd) * actionWeight;
        const double strike = HoldAndReturn(time, clip.prepareEnd, clip.activeEnd, clip.contactEnd, clip.recoverEnd);
        const double curvedArc = std::sin(Clamp01(strike) * kPi * 0.5);
        EnableHands(sample, handWeight);
        SetGuardHands(sample);
        const double arc = swing ? 0.48 : 0.31;
        sample.bodyRotateY = mirror * ((swing ? -12.0 : -8.0) * coil + (swing ? 40.0 : 20.0) * strike) * kPi / 180.0;
        sample.bodyRotateZ = mirror * (swing ? -8.0 : -4.0) * strike * kPi / 180.0;
        sample.rootOffsetForward = (swing ? 0.09 : 0.035) * strike;
        sample.punchStrength = strike;
        sample.leadHandForward = (swing ? -0.18 : -0.08) * coil + (swing ? 0.36 : 0.27) * strike;
        sample.leadHandY = -0.18 + (swing ? 0.08 : 0.02) * strike;
        sample.leadHandDepth = 0.20 + arc * curvedArc;
        break;
    }
    case ActionId::Layback: {
        const double lean = HoldAndReturn(time, 0.0, clip.activeEnd, clip.contactEnd, clip.recoverEnd);
        EnableHands(sample, handWeight);
        SetGuardHands(sample);
        sample.bodyRotateZ = mirror * -19.0 * kPi / 180.0 * lean;
        sample.bodyRotateX = -5.0 * kPi / 180.0 * lean;
        sample.lowerBodyActionRotationWeight = 0.18;
        sample.rootOffsetForward = -0.045 * lean;
        sample.rootOffsetY = 0.025 * lean;
        sample.dodgeStrength = lean;
        break;
    }
    case ActionId::SlipLeft:
    case ActionId::SlipRight: {
        const double side = clip.id == ActionId::SlipLeft ? -1.0 : 1.0;
        const double slip = HoldAndReturn(time, 0.0, clip.activeEnd, clip.contactEnd, clip.recoverEnd);
        EnableHands(sample, handWeight);
        SetGuardHands(sample);
        sample.rootOffsetLateral = side * 0.13 * slip;
        sample.bodyRotateZ = side * 11.0 * kPi / 180.0 * slip;
        sample.bodyRotateY = mirror * side * 5.0 * kPi / 180.0 * slip;
        sample.dodgeStrength = slip;
        break;
    }
    case ActionId::Parry: {
        const double parry = HoldAndReturn(time, 0.0, clip.activeEnd, clip.contactEnd, clip.recoverEnd);
        EnableHands(sample, handWeight);
        SetGuardHands(sample);
        sample.leadHandForward = 0.12 + 0.08 * parry;
        sample.leadHandY = -0.27 - 0.06 * parry;
        sample.leadHandDepth = 0.24 + 0.20 * parry;
        sample.bodyRotateY = mirror * 5.0 * parry * kPi / 180.0;
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
        EnableFeet(sample);
        EnableHands(sample, handWeight);
        SetGuardHands(sample);
        sample.leadFootForwardOffset = (roundhouse ? 0.08 : 0.11) * chamber +
            (roundhouse ? 0.43 : (sideKick ? 0.565 : 0.59)) * extension;
        sample.leadFootLift = (roundhouse ? 0.25 : 0.20) * chamber +
            (roundhouse ? 0.31 : (sideKick ? 0.315 : 0.27)) * extension;
        sample.leadFootDepthOffset = (roundhouse ? 0.10 : (sideKick ? 0.03 : 0.0)) * chamber +
            (roundhouse ? 0.42 : (sideKick ? 0.02 : 0.0)) * extension;
        sample.bodyRotateY = mirror * (roundhouse ? 24.0 : (sideKick ? 10.0 : 4.0)) * kPi / 180.0 * balance;
        sample.bodyRotateZ = mirror * (roundhouse ? -11.0 : (sideKick ? -8.0 : -5.0)) * kPi / 180.0 * balance;
        sample.lowerBodyRotateY = mirror * (roundhouse ? 18.0 : 0.0) * kPi / 180.0 * balance;
        sample.kickStrength = extension;
        sample.leadHandForward = -0.10 - 0.08 * balance;
        sample.leadHandY = -0.33 + 0.05 * balance;
        sample.rearHandForward = -0.18 + 0.05 * balance;
        sample.rearHandY = -0.17 - 0.04 * balance;
        break;
    }
    case ActionId::SpinningBackKick: {
        const double spin = Segment(time, 0.0, clip.activeEnd);
        const double chamber = Segment(time, 0.08, clip.prepareEnd) *
            (1.0 - Segment(time, 0.94, clip.recoverEnd));
        const double extension = Segment(time, clip.prepareEnd, clip.activeEnd) *
            (1.0 - Segment(time, clip.contactEnd, 0.88));
        EnableFeet(sample);
        EnableHands(sample, handWeight);
        SetGuardHands(sample);
        sample.bodyRotateY = mirror * 2.0 * kPi * spin;
        sample.bodyRotateZ = mirror * -7.0 * kPi / 180.0 * std::max(chamber * 0.4, extension);
        sample.leadFootForwardOffset = 0.08 * chamber + 0.60 * extension;
        sample.leadFootLift = 0.20 * chamber + 0.27 * extension;
        sample.leadFootDepthOffset = -0.04 * chamber - 0.10 * extension;
        sample.kickStrength = extension;
        sample.leadHandForward = -0.16;
        sample.leadHandY = -0.30;
        sample.rearHandForward = -0.20;
        sample.rearHandY = -0.16;
        break;
    }
    case ActionId::LightHitReact: {
        const double snap = HoldAndReturn(time, 0.0, clip.activeEnd, clip.contactEnd, clip.recoverEnd);
        const double follow = std::sin(Segment(time, 0.0, clip.contactEnd) * kPi) * actionWeight;
        EnableHands(sample, handWeight);
        SetGuardHands(sample);
        sample.bodyRotateZ = mirror * -13.0 * kPi / 180.0 * snap;
        sample.bodyRotateY = mirror * -11.0 * kPi / 180.0 * snap;
        sample.rootOffsetForward = -0.065 * snap;
        sample.rootOffsetY = 0.018 * snap;
        sample.hitStrength = snap;
        sample.lowerBodyActionRotationWeight = 0.15;
        sample.leadHandForward -= 0.10 * follow;
        sample.leadHandY += 0.08 * follow;
        sample.rearHandForward -= 0.08 * follow;
        sample.rearHandY += 0.05 * follow;
        break;
    }
    case ActionId::HeavyStagger: {
        const double impact = HoldAndReturn(time, 0.0, clip.activeEnd, 0.58, clip.recoverEnd);
        const double retreat = HoldAndReturn(time, clip.prepareEnd, clip.contactEnd, 0.70, clip.recoverEnd);
        EnableHands(sample, handWeight);
        SetGuardHands(sample);
        sample.bodyRotateZ = mirror * -27.0 * kPi / 180.0 * impact;
        sample.bodyRotateX = -9.0 * kPi / 180.0 * impact;
        sample.rootOffsetForward = -0.25 * retreat;
        sample.rootOffsetY = 0.045 * impact;
        sample.hitStrength = impact;
        sample.lowerBodyActionRotationWeight = 0.10;
        break;
    }
    case ActionId::WhiffRecovery: {
        const double overreach = HoldAndReturn(time, 0.0, clip.activeEnd, 0.58, clip.recoverEnd);
        EnableHands(sample, handWeight);
        SetGuardHands(sample);
        sample.bodyRotateY = mirror * 23.0 * kPi / 180.0 * overreach;
        sample.bodyRotateZ = mirror * 10.0 * kPi / 180.0 * overreach;
        sample.rootOffsetForward = 0.15 * overreach;
        sample.rootOffsetY = 0.025 * overreach;
        sample.whiffRecoveryStrength = overreach;
        sample.lowerBodyActionRotationWeight = 0.10;
        sample.leadHandForward = 0.42 * overreach;
        sample.leadHandY = -0.06 + 0.10 * overreach;
        sample.leadHandDepth = 0.32;
        break;
    }
    default:
        return ActionSample{};
    }
    return sample;
}

} // namespace besktop
