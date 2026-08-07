#include "besktop/animation/stage_guide_npc.h"

#include <cmath>
#include <iostream>
#include <vector>

namespace {

constexpr besktop::StageGuideRect kWorkArea{0.0, 0.0, 1200.0, 760.0};

bool Expect(bool condition, const char* message)
{
    if (condition) return true;
    std::cerr << message << '\n';
    return false;
}

besktop::StageGuideNpcUpdateInput Input(
    double deltaSeconds,
    const besktop::StageGuidePointerInput& pointer = {},
    const std::vector<besktop::StageGuideReservation>& reservations = {})
{
    return {deltaSeconds, kWorkArea, reservations, pointer};
}

besktop::StageGuideLayout Layout(const besktop::StageGuideNpcState& state)
{
    return besktop::ComputeStageGuideLayout({
        kWorkArea,
        state.position,
        state.bodySize,
        1.0,
        state.menuProgress,
        state.availability,
        besktop::StageGuideNpcShowsMenu(state),
        besktop::StageGuideNpcShowsAbout(state),
        besktop::StageGuideNpcShowsExternalConfirmation(state),
        state.confirmingEntry,
    });
}

besktop::StageGuidePointerInput Pointer(
    const besktop::StageGuideLayout& layout,
    const besktop::StageGuidePoint& point)
{
    return {
        true,
        besktop::StageGuidePointInRect(point, layout.bodyHoverRect),
        besktop::IsStageGuideInteractivePoint(layout, point),
        point,
        besktop::HitTestStageGuideLayout(layout, point),
    };
}

void OpenMenu(besktop::StageGuideNpcState& state)
{
    auto layout = Layout(state);
    const auto center = state.position;
    auto pointer = Pointer(layout, center);
    besktop::UpdateStageGuideNpc(state, Input(0.01, pointer));
    besktop::UpdateStageGuideNpc(state, Input(0.23, pointer));
    layout = Layout(state);
    pointer = Pointer(layout, center);
    besktop::UpdateStageGuideNpc(state, Input(0.20, pointer));
}

} // namespace

int main()
{
    bool passed = true;

    besktop::StageGuideNpcState first;
    besktop::StageGuideNpcState second;
    besktop::InitializeStageGuideNpc(first, {12345u, 48.0, false, {}});
    besktop::InitializeStageGuideNpc(second, {12345u, 48.0, false, {}});
    passed &= Expect(first.fallbackEntrySeconds == second.fallbackEntrySeconds &&
            first.fallbackEntrySeconds >= 20.0 && first.fallbackEntrySeconds <= 30.0,
        "fallback entry was not stable or bounded");
    besktop::UpdateStageGuideNpc(first, Input(first.fallbackEntrySeconds - 0.01));
    passed &= Expect(first.phase == besktop::StageGuideNpcPhase::Dormant,
        "fallback entry occurred too early");
    besktop::UpdateStageGuideNpc(first, Input(0.02));
    passed &= Expect(first.phase == besktop::StageGuideNpcPhase::Entering,
        "fallback entry did not start after its stable deadline");

    besktop::StageGuideNpcState encounter;
    besktop::InitializeStageGuideNpc(encounter, {6789u, 48.0, false, {}});
    passed &= Expect(besktop::NotifyStageGuideFirstEncounterCompleted(encounter, {400.0, 300.0}),
        "first encounter was not consumed");
    const double firstDelay = encounter.postEncounterDelayRemaining;
    passed &= Expect(!besktop::NotifyStageGuideFirstEncounterCompleted(encounter, {700.0, 300.0}) &&
            encounter.postEncounterDelayRemaining == firstDelay,
        "first encounter trigger consumed more than once");
    besktop::UpdateStageGuideNpc(encounter, Input(firstDelay + 0.01));
    passed &= Expect(encounter.phase == besktop::StageGuideNpcPhase::Entering,
        "post-encounter delayed entry did not start");

    besktop::StageGuideNpcState hover;
    besktop::InitializeStageGuideNpc(hover, {77u, 48.0, true, {true, true, true}});
    besktop::UpdateStageGuideNpc(hover, Input(0.0));
    auto hoverLayout = Layout(hover);
    auto bodyPointer = Pointer(hoverLayout, hover.position);
    besktop::UpdateStageGuideNpc(hover, Input(0.01, bodyPointer));
    besktop::UpdateStageGuideNpc(hover, Input(0.20, bodyPointer));
    passed &= Expect(hover.phase == besktop::StageGuideNpcPhase::NoticingPointer,
        "hover threshold fired too early");
    besktop::UpdateStageGuideNpc(hover, Input(0.03, bodyPointer));
    passed &= Expect(hover.phase == besktop::StageGuideNpcPhase::PresentingMenu,
        "hover threshold did not present the menu");
    hoverLayout = Layout(hover);
    bodyPointer = Pointer(hoverLayout, hover.position);
    besktop::UpdateStageGuideNpc(hover, Input(0.20, bodyPointer));
    hoverLayout = Layout(hover);
    const auto corridorPoint = besktop::StageGuidePoint{
        (hoverLayout.interactionCorridor.left + hoverLayout.interactionCorridor.right) * 0.5,
        (hoverLayout.interactionCorridor.top + hoverLayout.interactionCorridor.bottom) * 0.5,
    };
    besktop::UpdateStageGuideNpc(hover, Input(0.10, Pointer(hoverLayout, corridorPoint)));
    const auto aboutRect = hoverLayout.menuItems.back().rect;
    const auto aboutPoint = besktop::StageGuidePoint{
        (aboutRect.left + aboutRect.right) * 0.5,
        (aboutRect.top + aboutRect.bottom) * 0.5,
    };
    besktop::UpdateStageGuideNpc(hover, Input(0.10, Pointer(hoverLayout, aboutPoint)));
    passed &= Expect(hover.phase == besktop::StageGuideNpcPhase::PresentingMenu,
        "body-corridor-menu traversal closed the menu");
    besktop::UpdateStageGuideNpc(hover, Input(0.51));
    passed &= Expect(hover.phase == besktop::StageGuideNpcPhase::PresentingMenu,
        "menu closed before leave grace expired");
    besktop::UpdateStageGuideNpc(hover, Input(0.02));
    passed &= Expect(hover.phase == besktop::StageGuideNpcPhase::Roaming,
        "menu did not close after leave grace");

    besktop::StageGuideNpcState actions;
    besktop::InitializeStageGuideNpc(actions, {91u, 48.0, true, {true, true, true}});
    besktop::UpdateStageGuideNpc(actions, Input(0.0));
    OpenMenu(actions);
    auto actionLayout = Layout(actions);
    const auto feedbackRect = actionLayout.menuItems.front().rect;
    const besktop::StageGuidePoint feedbackPoint{
        (feedbackRect.left + feedbackRect.right) * 0.5,
        (feedbackRect.top + feedbackRect.bottom) * 0.5,
    };
    passed &= Expect(besktop::HandleStageGuideClick(
            actions, besktop::HitTestStageGuideLayout(actionLayout, feedbackPoint)),
        "configured feedback entry did not open confirmation");
    actionLayout = Layout(actions);
    const besktop::StageGuidePoint confirmPoint{
        (actionLayout.confirmButtonRect.left + actionLayout.confirmButtonRect.right) * 0.5,
        (actionLayout.confirmButtonRect.top + actionLayout.confirmButtonRect.bottom) * 0.5,
    };
    passed &= Expect(besktop::HandleStageGuideClick(
            actions, besktop::HitTestStageGuideLayout(actionLayout, confirmPoint)),
        "feedback confirmation click was not accepted");
    besktop::UpdateStageGuideNpc(actions, Input(0.30));
    passed &= Expect(
        besktop::ConsumeStageGuideExternalAction(actions) ==
            besktop::StageGuideExternalAction::Feedback &&
        besktop::ConsumeStageGuideExternalAction(actions) ==
            besktop::StageGuideExternalAction::None,
        "feedback action was not consumed exactly once");

    besktop::StageGuideNpcState support;
    besktop::InitializeStageGuideNpc(support, {92u, 48.0, true, {true, true, true}});
    besktop::UpdateStageGuideNpc(support, Input(0.0));
    OpenMenu(support);
    auto supportLayout = Layout(support);
    const auto supportRect = supportLayout.menuItems[1].rect;
    const besktop::StageGuidePoint supportPoint{
        (supportRect.left + supportRect.right) * 0.5,
        (supportRect.top + supportRect.bottom) * 0.5,
    };
    passed &= Expect(besktop::HandleStageGuideClick(
            support, besktop::HitTestStageGuideLayout(supportLayout, supportPoint)),
        "configured support entry did not open confirmation");
    supportLayout = Layout(support);
    const besktop::StageGuidePoint supportConfirmPoint{
        (supportLayout.confirmButtonRect.left + supportLayout.confirmButtonRect.right) * 0.5,
        (supportLayout.confirmButtonRect.top + supportLayout.confirmButtonRect.bottom) * 0.5,
    };
    passed &= Expect(besktop::HandleStageGuideClick(
            support, besktop::HitTestStageGuideLayout(supportLayout, supportConfirmPoint)),
        "support confirmation click was not accepted");
    besktop::UpdateStageGuideNpc(support, Input(0.30));
    passed &= Expect(
        besktop::ConsumeStageGuideExternalAction(support) ==
            besktop::StageGuideExternalAction::Support &&
        besktop::ConsumeStageGuideExternalAction(support) ==
            besktop::StageGuideExternalAction::None,
        "support action was not consumed exactly once");

    besktop::StageGuideNpcState about;
    besktop::InitializeStageGuideNpc(about, {93u, 48.0, true, {false, false, true}});
    besktop::UpdateStageGuideNpc(about, Input(0.0));
    OpenMenu(about);
    const auto aboutOnlyLayout = Layout(about);
    passed &= Expect(aboutOnlyLayout.menuItems.size() == 1,
        "unconfigured external entries were not hidden");
    const auto aboutOnlyRect = aboutOnlyLayout.menuItems.front().rect;
    const besktop::StageGuidePoint aboutOnlyPoint{
        (aboutOnlyRect.left + aboutOnlyRect.right) * 0.5,
        (aboutOnlyRect.top + aboutOnlyRect.bottom) * 0.5,
    };
    passed &= Expect(besktop::HandleStageGuideClick(
            about, besktop::HitTestStageGuideLayout(aboutOnlyLayout, aboutOnlyPoint)) &&
            about.phase == besktop::StageGuideNpcPhase::ShowingAbout &&
            besktop::ConsumeStageGuideExternalAction(about) ==
                besktop::StageGuideExternalAction::None,
        "about card produced an external action");
    besktop::ClearStageGuidePointerInteraction(about, true);
    passed &= Expect(about.phase == besktop::StageGuideNpcPhase::Roaming &&
            about.menuProgress == 0.0 && !about.pointerPresent,
        "focus/cancel pointer cleanup did not dismiss the guide panels");

    besktop::StageGuideNpcState avoidance;
    besktop::InitializeStageGuideNpc(avoidance, {101u, 48.0, true, {}});
    besktop::UpdateStageGuideNpc(avoidance, Input(0.0));
    avoidance.phase = besktop::StageGuideNpcPhase::Roaming;
    avoidance.roamingWaitRemaining = 0.0;
    avoidance.target = {900.0, avoidance.position.y};
    avoidance.targetValid = true;
    avoidance.moving = true;
    const auto before = avoidance.position;
    std::vector<besktop::StageGuideReservation> reservations{{
        {{(before.x + 900.0) * 0.5, before.y}, 90.0},
    }};
    const auto avoidanceStep = besktop::UpdateStageGuideNpc(
        avoidance, Input(0.016, {}, reservations));
    const double movement = std::hypot(
        avoidance.position.x - before.x,
        avoidance.position.y - before.y);
    passed &= Expect(movement <= besktop::GetStageGuideNpcTuning().roamingSpeed * 0.016 + 0.01,
        "reservation avoidance teleported the guide");
    passed &= Expect(avoidanceStep.targetChanged || avoidanceStep.waitingForSafePath,
        "unsafe reservation path did not replan or wait");
    if (avoidance.targetValid) {
        passed &= Expect(besktop::StageGuidePointIsSafe(
                avoidance.target, kWorkArea, reservations,
                avoidance.bodySize * besktop::GetStageGuideNpcTuning().reservationMarginScale),
            "reservation avoidance chose an unsafe target");
    }

    if (!passed) return 1;
    std::cout << "besktop_stage_guide_npc_tests: all checks passed\n";
    return 0;
}
