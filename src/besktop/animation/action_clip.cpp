#include "besktop/animation/action_clip.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr std::uint32_t kContactEventMask = 1u << 0;

constexpr besktop::ActionEvent kLeadStraightEvents[] = {
    {besktop::ActionEventType::Contact, 0.30, kContactEventMask},
};
constexpr besktop::ActionEvent kLaybackEvents[] = {
    {besktop::ActionEventType::Contact, 0.28, kContactEventMask},
};
constexpr besktop::ActionEvent kLightHitReactEvents[] = {
    {besktop::ActionEventType::Contact, 0.16, kContactEventMask},
};

constexpr besktop::ActionClip kNoneClip{};
constexpr besktop::ActionClip kLeadStraightClip{
    besktop::ActionId::LeadStraight, 0.18, 0.30, 0.34, 0.58, 0.72,
    kLeadStraightEvents, std::size(kLeadStraightEvents)};
constexpr besktop::ActionClip kLaybackClip{
    besktop::ActionId::Layback, 0.16, 0.28, 0.34, 0.58, 0.76,
    kLaybackEvents, std::size(kLaybackEvents)};
constexpr besktop::ActionClip kLightHitReactClip{
    besktop::ActionId::LightHitReact, 0.08, 0.16, 0.22, 0.46, 0.62,
    kLightHitReactEvents, std::size(kLightHitReactEvents)};

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

} // namespace

namespace besktop {

ActionId ParseActionId(std::wstring_view name)
{
    if (name == L"lead_straight") {
        return ActionId::LeadStraight;
    }
    if (name == L"layback") {
        return ActionId::Layback;
    }
    if (name == L"light_hit_react") {
        return ActionId::LightHitReact;
    }
    return ActionId::None;
}

std::wstring_view ActionIdName(ActionId id)
{
    switch (id) {
    case ActionId::LeadStraight:
        return L"lead_straight";
    case ActionId::Layback:
        return L"layback";
    case ActionId::LightHitReact:
        return L"light_hit_react";
    case ActionId::None:
        return L"none";
    default:
        return L"reserved";
    }
}

const ActionClip& GetActionClip(ActionId id)
{
    switch (id) {
    case ActionId::LeadStraight:
        return kLeadStraightClip;
    case ActionId::Layback:
        return kLaybackClip;
    case ActionId::LightHitReact:
        return kLightHitReactClip;
    default:
        return kNoneClip;
    }
}

ActionPhase ActionPhaseAt(const ActionClip& clip, double localTimeSeconds)
{
    if (clip.id == ActionId::None || localTimeSeconds >= clip.duration) {
        return ActionPhase::Complete;
    }
    if (localTimeSeconds < clip.prepareEnd) {
        return ActionPhase::Prepare;
    }
    if (localTimeSeconds < clip.activeEnd) {
        return ActionPhase::Active;
    }
    if (localTimeSeconds < clip.contactEnd) {
        return ActionPhase::Contact;
    }
    if (localTimeSeconds < clip.recoverEnd) {
        return ActionPhase::Recover;
    }
    return ActionPhase::Complete;
}

ActionSample SampleAction(const ActionClip& clip, double localTimeSeconds, double direction)
{
    ActionSample sample;
    if (clip.id == ActionId::None || clip.duration <= 0.0) {
        return sample;
    }

    const double time = std::clamp(localTimeSeconds, 0.0, clip.duration);
    const double mirror = direction < 0.0 ? -1.0 : 1.0;
    const double actionWeight = 1.0 - Segment(time, clip.recoverEnd, clip.duration);
    sample.handTargetWeight = Segment(time, 0.0, std::min(0.08, clip.prepareEnd)) * actionWeight;

    sample.leadHandTargetEnabled = true;
    sample.rearHandTargetEnabled = true;
    sample.rearHandForward = 0.10;
    sample.rearHandY = -0.26;
    sample.rearHandDepth = 0.20;

    switch (clip.id) {
    case ActionId::LeadStraight: {
        const double prepare = Segment(time, 0.0, clip.prepareEnd);
        const double strike = HoldAndReturn(time, clip.prepareEnd, clip.activeEnd, clip.contactEnd, clip.recoverEnd);
        sample.bodyRotateY = mirror * ((-4.0 * prepare) + (13.0 * strike)) * kPi / 180.0 * actionWeight;
        sample.bodyRotateZ = mirror * (-3.0 * strike) * kPi / 180.0;
        sample.rootOffsetForward = 0.025 * strike;
        sample.punchStrength = strike;
        sample.leadHandForward = -0.03 * prepare + 0.59 * strike;
        sample.leadHandY = -0.04 + 0.04 * strike;
        sample.leadHandDepth = 0.24;
        break;
    }
    case ActionId::Layback: {
        const double lean = HoldAndReturn(time, 0.0, clip.activeEnd, clip.contactEnd, clip.recoverEnd);
        sample.bodyRotateZ = mirror * -19.0 * kPi / 180.0 * lean;
        sample.bodyRotateX = -5.0 * kPi / 180.0 * lean;
        sample.rootOffsetForward = -0.045 * lean;
        sample.rootOffsetY = 0.025 * lean;
        sample.dodgeStrength = lean;
        sample.leadHandForward = 0.10;
        sample.leadHandY = -0.31;
        sample.leadHandDepth = 0.24;
        break;
    }
    case ActionId::LightHitReact: {
        const double snap = HoldAndReturn(time, 0.0, clip.activeEnd, clip.contactEnd, clip.recoverEnd);
        const double follow = std::sin(Segment(time, 0.0, clip.contactEnd) * kPi) * actionWeight;
        sample.bodyRotateZ = mirror * -13.0 * kPi / 180.0 * snap;
        sample.bodyRotateY = mirror * -11.0 * kPi / 180.0 * snap;
        sample.rootOffsetForward = -0.065 * snap;
        sample.rootOffsetY = 0.018 * snap;
        sample.hitStrength = snap;
        sample.leadHandForward = -0.03 - 0.10 * follow;
        sample.leadHandY = -0.18 + 0.08 * follow;
        sample.leadHandDepth = 0.22;
        sample.rearHandForward = 0.02 - 0.08 * follow;
        sample.rearHandY = -0.24 + 0.05 * follow;
        break;
    }
    default:
        return ActionSample{};
    }
    return sample;
}

} // namespace besktop
