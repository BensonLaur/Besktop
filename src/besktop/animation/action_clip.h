#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace besktop {

enum class ActionId {
    None,
    SwingPunch,
    LeadStraight,
    RearStraight,
    Hook,
    Uppercut,
    FrontKick,
    SideKick,
    RoundhouseKick,
    SpinningBackKick,
    Layback,
    Slip,
    Parry,
    LightHitReact,
    HeavyStagger,
    WhiffRecovery,
};

enum class ActionPhase {
    Prepare,
    Active,
    Contact,
    Recover,
    Complete,
};

enum class ActionEventType {
    Contact,
};

struct ActionEvent {
    ActionEventType type = ActionEventType::Contact;
    double timeSeconds = 0.0;
    std::uint32_t mask = 0;
};

using ContactEvent = ActionEvent;

struct ActionSample {
    double bodyRotateX = 0.0;
    double bodyRotateY = 0.0;
    double bodyRotateZ = 0.0;
    double rootOffsetForward = 0.0;
    double rootOffsetY = 0.0;
    double hitStrength = 0.0;
    double punchStrength = 0.0;
    double dodgeStrength = 0.0;
    double leadHandForward = 0.0;
    double leadHandY = 0.0;
    double leadHandDepth = 0.0;
    double rearHandForward = 0.0;
    double rearHandY = 0.0;
    double rearHandDepth = 0.0;
    double handTargetWeight = 0.0;
    bool leadHandTargetEnabled = false;
    bool rearHandTargetEnabled = false;
};

struct ActionClip {
    ActionId id = ActionId::None;
    double prepareEnd = 0.0;
    double activeEnd = 0.0;
    double contactEnd = 0.0;
    double recoverEnd = 0.0;
    double duration = 0.0;
    const ActionEvent* events = nullptr;
    std::size_t eventCount = 0;
};

ActionId ParseActionId(std::wstring_view name);
std::wstring_view ActionIdName(ActionId id);
const ActionClip& GetActionClip(ActionId id);
ActionPhase ActionPhaseAt(const ActionClip& clip, double localTimeSeconds);
ActionSample SampleAction(const ActionClip& clip, double localTimeSeconds, double direction);

} // namespace besktop
