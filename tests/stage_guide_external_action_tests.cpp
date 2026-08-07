#include "besktop/app/stage_guide_external_action.h"

#include <iostream>
#include <string>
#include <vector>

namespace {

bool Expect(bool condition, const char* message)
{
    if (condition) return true;
    std::cerr << message << '\n';
    return false;
}

} // namespace

int main()
{
    bool passed = true;
    std::vector<std::string> order;
    bool destroyed = false;
    const auto result = besktop::ExecuteStageGuideExternalAction(
        besktop::StageGuideExternalAction::Feedback,
        {
            [&] { order.push_back("stop"); },
            [&] {
                order.push_back("destroy");
                destroyed = true;
            },
            [&] {
                order.push_back("check");
                return destroyed;
            },
            [&](besktop::StageGuideExternalAction action) {
                order.push_back("dispatch");
                return action == besktop::StageGuideExternalAction::Feedback;
            },
        });
    passed &= Expect(result == besktop::StageGuideExternalDispatchResult::Completed,
        "approved external action did not complete");
    passed &= Expect(order == std::vector<std::string>({"stop", "destroy", "check", "dispatch"}),
        "external action ordering changed");

    order.clear();
    const auto blocked = besktop::ExecuteStageGuideExternalAction(
        besktop::StageGuideExternalAction::Support,
        {
            [&] { order.push_back("stop"); },
            [&] { order.push_back("destroy"); },
            [&] {
                order.push_back("check");
                return false;
            },
            [&](besktop::StageGuideExternalAction) {
                order.push_back("dispatch");
                return true;
            },
        });
    passed &= Expect(blocked == besktop::StageGuideExternalDispatchResult::WindowDestroyFailed,
        "dispatch did not stop when the stage remained alive");
    passed &= Expect(order == std::vector<std::string>({"stop", "destroy", "check"}),
        "browser dispatch ran before confirmed window destruction");

    int callbackCount = 0;
    const auto noAction = besktop::ExecuteStageGuideExternalAction(
        besktop::StageGuideExternalAction::None,
        {
            [&] { ++callbackCount; },
            [&] { ++callbackCount; },
            [&] {
                ++callbackCount;
                return true;
            },
            [&](besktop::StageGuideExternalAction) {
                ++callbackCount;
                return true;
            },
        });
    passed &= Expect(noAction == besktop::StageGuideExternalDispatchResult::NoAction &&
            callbackCount == 0,
        "empty external action invoked a handler");

    if (!passed) return 1;
    std::cout << "besktop_stage_guide_external_action_tests: all checks passed\n";
    return 0;
}
