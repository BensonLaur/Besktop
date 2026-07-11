#include "besktop/animation/action_player.h"

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

} // namespace

int main()
{
    TestParsing();
    TestPhaseBoundaries();
    TestSkippedContactIsConsumedOnce();
    TestLoopCanEmitAgain();
    TestMirroringKeepsTimeline();
    if (failures == 0) {
        std::cout << "besktop_action_tests: all checks passed\n";
    }
    return failures == 0 ? 0 : 1;
}
