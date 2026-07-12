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
    SlipLeft,
    SlipRight,
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

enum class ActionAttackType {
    None,
    Punch,
    Kick,
};

enum class ActionHitStrength {
    None,
    Light,
    Heavy,
};

enum class ActionDefenseWindowType {
    None,
    Evade,
    Parry,
};

struct ActionDefenseWindow {
    ActionDefenseWindowType type = ActionDefenseWindowType::None;
    double startSeconds = 0.0;
    double endSeconds = 0.0;
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
    double rootOffsetLateral = 0.0;
    double rootOffsetY = 0.0;
    // Upper-body-only translation. It is applied after body/arm projection so
    // the planted pelvis, leg IK and foot targets cannot be affected.
    double upperBodyOffsetForward = 0.0;
    double upperBodyOffsetDepth = 0.0;
    double upperBodyOffsetY = 0.0;
    double hitStrength = 0.0;
    double punchStrength = 0.0;
    double kickStrength = 0.0;
    double dodgeStrength = 0.0;
    double whiffRecoveryStrength = 0.0;
    double leadHandForward = 0.0;
    double leadHandY = 0.0;
    double leadHandDepth = 0.0;
    double rearHandForward = 0.0;
    double rearHandY = 0.0;
    double rearHandDepth = 0.0;
    double leadArmBendForward = 0.0;
    double rearArmBendForward = 0.0;
    // Local shoulder-girdle offsets, expressed as fractions of planeSide.
    // Forward follows the actor's canonical facing; positive Y points down.
    double leadShoulderForwardOffset = 0.0;
    double leadShoulderYOffset = 0.0;
    double leadShoulderDepthOffset = 0.0;
    double rearShoulderForwardOffset = 0.0;
    double rearShoulderYOffset = 0.0;
    double rearShoulderDepthOffset = 0.0;
    double handTargetWeight = 0.0;
    bool leadHandTargetEnabled = false;
    bool rearHandTargetEnabled = false;
    double leadFootForwardOffset = 0.0;
    double leadFootLift = 0.0;
    double leadFootDepthOffset = 0.0;
    double rearFootForwardOffset = 0.0;
    double rearFootLift = 0.0;
    double rearFootDepthOffset = 0.0;
    double footTargetWeight = 0.0;
    // Keeps animated foot targets in actor/world action space while the
    // pelvis yaws underneath them; IK absorbs the relative rotation.
    double footTargetYawCompensationWeight = 0.0;
    double lowerBodyActionRotationWeight = 0.0;
    double lowerBodyRotateX = 0.0;
    double lowerBodyRotateY = 0.0;
    double lowerBodyRotateZ = 0.0;
    bool leadFootTargetEnabled = false;
    bool rearFootTargetEnabled = false;
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
    ActionAttackType attackType = ActionAttackType::None;
    ActionHitStrength hitStrength = ActionHitStrength::None;
    ActionDefenseWindow defenseWindow{};
    bool whiffRecovery = false;
};

ActionId ParseActionId(std::wstring_view name);
std::wstring_view ActionIdName(ActionId id);
const ActionClip& GetActionClip(ActionId id);
ActionPhase ActionPhaseAt(const ActionClip& clip, double localTimeSeconds);
ActionDefenseWindowType ActionDefenseWindowAt(const ActionClip& clip, double localTimeSeconds);
ActionSample SampleAction(const ActionClip& clip, double localTimeSeconds, double direction);

} // namespace besktop
