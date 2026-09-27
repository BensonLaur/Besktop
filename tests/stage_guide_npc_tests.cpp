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
        besktop::StageGuideNpcShowsSupport(state),
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
    passed &= Expect(std::abs(first.position.y - first.target.y) <= 1e-9,
        "crab guide entry was not horizontal");
    const double entryLaneY = first.position.y;
    besktop::UpdateStageGuideNpc(first, Input(0.10));
    passed &= Expect(std::abs(first.position.y - entryLaneY) <= 1e-9,
        "crab guide changed vertical lanes while entering");

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

    besktop::StageGuideNpcState clawHover;
    besktop::InitializeStageGuideNpc(clawHover, {78u, 48.0, true, {true, true, true}});
    besktop::UpdateStageGuideNpc(clawHover, Input(0.0));
    const auto clawLayout = Layout(clawHover);
    const besktop::StageGuidePoint clawPoint{
        clawHover.position.x + clawHover.bodySize * 0.92,
        clawHover.position.y,
    };
    auto clawPointer = Pointer(clawLayout, clawPoint);
    besktop::UpdateStageGuideNpc(clawHover, Input(0.01, clawPointer));
    besktop::UpdateStageGuideNpc(clawHover, Input(0.24, clawPointer));
    passed &= Expect(clawHover.phase == besktop::StageGuideNpcPhase::PresentingMenu,
        "hovering a visible outer claw did not present the menu");

    besktop::StageGuideNpcState reservationHover;
    besktop::InitializeStageGuideNpc(
        reservationHover, {79u, 48.0, true, {true, true, true}});
    besktop::UpdateStageGuideNpc(reservationHover, Input(0.0));
    const auto reservationHoverLayout = Layout(reservationHover);
    const auto reservationPointer = Pointer(
        reservationHoverLayout, reservationHover.position);
    const std::vector<besktop::StageGuideReservation> overlappingReservation{{
        {reservationHover.position, 90.0},
    }};
    besktop::UpdateStageGuideNpc(
        reservationHover, Input(0.01, reservationPointer, overlappingReservation));
    for (int frame = 0; frame < 8; ++frame) {
        besktop::UpdateStageGuideNpc(
            reservationHover, Input(0.04, reservationPointer, overlappingReservation));
    }
    passed &= Expect(
        reservationHover.phase == besktop::StageGuideNpcPhase::PresentingMenu,
        "an overlapping encounter repeatedly reset the pointer dwell");

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
        "configured support entry did not open the local card");
    supportLayout = Layout(support);
    passed &= Expect(supportLayout.showSupport &&
            support.supportProvider == besktop::StageGuideSupportProvider::WeChat &&
            besktop::StageGuideNpcIsFrozen(support),
        "support card did not default to WeChat or freeze the guide");
    const auto supportPosition = support.position;
    besktop::UpdateStageGuideNpc(support, Input(30.0));
    passed &= Expect(besktop::StageGuideNpcShowsSupport(support) &&
            support.position.x == supportPosition.x && support.position.y == supportPosition.y,
        "support card moved or timed out during scanning");
    const besktop::StageGuidePoint alipayPoint{
        (supportLayout.alipayButtonRect.left + supportLayout.alipayButtonRect.right) * 0.5,
        (supportLayout.alipayButtonRect.top + supportLayout.alipayButtonRect.bottom) * 0.5,
    };
    passed &= Expect(besktop::HandleStageGuideClick(
            support, besktop::HitTestStageGuideLayout(supportLayout, alipayPoint)) &&
            support.supportProvider == besktop::StageGuideSupportProvider::Alipay,
        "Alipay tab did not switch the local image");
    passed &= Expect(besktop::HandleStageGuideClick(support, besktop::StageGuideHitTarget::SupportWeChat) &&
            support.supportProvider == besktop::StageGuideSupportProvider::WeChat,
        "WeChat tab did not switch back");
    passed &= Expect(!besktop::HandleStageGuideClick(support, besktop::StageGuideHitTarget::None) &&
            !besktop::HandleStageGuideClick(support, besktop::StageGuideHitTarget::ViewProject) &&
            besktop::StageGuideNpcShowsSupport(support),
        "support card dismissed or navigated from an unrelated click");
    besktop::ClearStageGuidePointerInteraction(support, false);
    besktop::UpdateStageGuideNpc(support, Input(30.0, {}, overlappingReservation));
    passed &= Expect(
        besktop::ConsumeStageGuideExternalAction(support) ==
            besktop::StageGuideExternalAction::None && besktop::StageGuideNpcShowsSupport(support),
        "local support unexpectedly requested an external action or closed");
    auto supportCancel = support;
    passed &= Expect(besktop::HandleStageGuideClick(support, besktop::StageGuideHitTarget::ContinueWatching) &&
            support.phase == besktop::StageGuideNpcPhase::PresentingMenu,
        "continue watching did not close the support card");
    auto supportClose = supportCancel;
    passed &= Expect(besktop::HandleStageGuideClick(supportClose, besktop::StageGuideHitTarget::CloseCard) &&
            supportClose.phase == besktop::StageGuideNpcPhase::PresentingMenu,
        "close button did not close the support card");
    besktop::ClearStageGuidePointerInteraction(supportCancel, true);
    passed &= Expect(supportCancel.phase == besktop::StageGuideNpcPhase::Roaming,
        "focus loss did not clean up the support interaction");

    besktop::StageGuideNpcState about;
    besktop::InitializeStageGuideNpc(about, {93u, 48.0, true, {false, false, true}});
    besktop::UpdateStageGuideNpc(about, Input(0.0));
    OpenMenu(about);
    passed &= Expect(!besktop::HandleStageGuideClick(about, besktop::StageGuideHitTarget::Support),
        "unavailable support resources still allowed opening the card");
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
    auto projectAction = about;
    besktop::ClearStageGuidePointerInteraction(about, true);
    passed &= Expect(about.phase == besktop::StageGuideNpcPhase::Roaming &&
            about.menuProgress == 0.0 && !about.pointerPresent,
        "focus/cancel pointer cleanup did not dismiss the guide panels");

    const auto aboutCardLayout = Layout(projectAction);
    const besktop::StageGuidePoint projectButtonPoint{
        (aboutCardLayout.projectButtonRect.left + aboutCardLayout.projectButtonRect.right) * 0.5,
        (aboutCardLayout.projectButtonRect.top + aboutCardLayout.projectButtonRect.bottom) * 0.5,
    };
    passed &= Expect(
        besktop::HandleStageGuideClick(
            projectAction, besktop::HitTestStageGuideLayout(aboutCardLayout, projectButtonPoint)) &&
            projectAction.phase == besktop::StageGuideNpcPhase::LeavingForExternalAction,
        "project button did not start the safe external action flow");
    besktop::UpdateStageGuideNpc(projectAction, Input(0.30));
    passed &= Expect(
        besktop::ConsumeStageGuideExternalAction(projectAction) ==
                besktop::StageGuideExternalAction::Project &&
            besktop::ConsumeStageGuideExternalAction(projectAction) ==
                besktop::StageGuideExternalAction::None,
        "project action was not consumed exactly once");

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
    passed &= Expect(std::abs(avoidance.position.y - before.y) <= 1e-9 &&
            (!avoidance.targetValid || std::abs(avoidance.target.y - before.y) <= 1e-9),
        "crab guide changed vertical lanes while roaming");
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
