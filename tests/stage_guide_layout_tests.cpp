#include "besktop/animation/stage_guide_layout.h"

#include <array>
#include <cmath>
#include <iostream>

namespace {

bool Expect(bool condition, const char* message)
{
    if (condition) return true;
    std::cerr << message << '\n';
    return false;
}

bool Inside(
    const besktop::StageGuideRect& outer,
    const besktop::StageGuideRect& inner)
{
    return !besktop::StageGuideRectIsValid(inner) ||
        (inner.left >= outer.left - 0.01 && inner.top >= outer.top - 0.01 &&
            inner.right <= outer.right + 0.01 && inner.bottom <= outer.bottom + 0.01);
}

} // namespace

int main()
{
    bool passed = true;
    const besktop::StageGuideRect workArea{0.0, 0.0, 1280.0, 760.0};
    const std::array<double, 3> scales{1.0, 1.25, 1.5};
    const std::array<besktop::StageGuidePoint, 4> edgeCenters{{
        {120.0, 90.0},
        {1160.0, 90.0},
        {120.0, 650.0},
        {1160.0, 650.0},
    }};
    for (const double scale : scales) {
        for (const auto center : edgeCenters) {
            const auto layout = besktop::ComputeStageGuideLayout({
                workArea,
                center,
                62.0,
                scale,
                1.0,
                {true, true, true},
                true,
                true,
                false,
                besktop::StageGuideMenuEntry::None,
            });
            passed &= Expect(Inside(workArea, layout.bodyRect),
                "body escaped the work area at an edge/DPI case");
            passed &= Expect(Inside(workArea, layout.menuBounds),
                "menu escaped the work area at an edge/DPI case");
            passed &= Expect(Inside(workArea, layout.cardRect),
                "about card escaped the work area at an edge/DPI case");
            passed &= Expect(Inside(workArea, layout.projectButtonRect),
                "project button escaped the work area at an edge/DPI case");
            passed &= Expect(layout.menuItems.size() == 3,
                "diagnostic layout did not expose all three entries");
        }
    }

    const auto topLayout = besktop::ComputeStageGuideLayout({
        workArea, {640.0, 60.0}, 62.0, 1.0, 1.0, {true, true, true},
        true, false, false, besktop::StageGuideMenuEntry::None,
    });
    passed &= Expect(topLayout.menuBelowBody,
        "menu did not flip below a guide near the top edge");
    passed &= Expect(std::abs(
            (topLayout.bodyHoverRect.right - topLayout.bodyHoverRect.left) /
                (topLayout.bodyRect.right - topLayout.bodyRect.left) - 2.0) < 1e-9 &&
            std::abs(
                (topLayout.bodyHoverRect.bottom - topLayout.bodyHoverRect.top) /
                    (topLayout.bodyRect.bottom - topLayout.bodyRect.top) - 1.4) < 1e-9,
        "crab hover region did not cover the full wide silhouette");
    const besktop::StageGuidePoint clawPoint{
        640.0 + 62.0 * 0.92,
        60.0,
    };
    passed &= Expect(
        besktop::HitTestStageGuideLayout(topLayout, clawPoint) ==
            besktop::StageGuideHitTarget::Body,
        "visible outer claw was not included in the body hover target");
    const besktop::StageGuidePoint corridorCenter{
        (topLayout.interactionCorridor.left + topLayout.interactionCorridor.right) * 0.5,
        (topLayout.interactionCorridor.top + topLayout.interactionCorridor.bottom) * 0.5,
    };
    passed &= Expect(besktop::IsStageGuideInteractivePoint(topLayout, corridorCenter),
        "body-menu corridor was not interactive");
    const auto aboutItem = topLayout.menuItems.back();
    const besktop::StageGuidePoint aboutCenter{
        (aboutItem.rect.left + aboutItem.rect.right) * 0.5,
        (aboutItem.rect.top + aboutItem.rect.bottom) * 0.5,
    };
    passed &= Expect(
        besktop::HitTestStageGuideLayout(topLayout, aboutCenter) ==
            besktop::StageGuideHitTarget::About,
        "about entry hit test failed");

    const auto aboutCard = besktop::ComputeStageGuideLayout({
        workArea, {640.0, 650.0}, 62.0, 1.0, 1.0, {true, true, true},
        true, true, false, besktop::StageGuideMenuEntry::None,
    });
    const besktop::StageGuidePoint projectButtonCenter{
        (aboutCard.projectButtonRect.left + aboutCard.projectButtonRect.right) * 0.5,
        (aboutCard.projectButtonRect.top + aboutCard.projectButtonRect.bottom) * 0.5,
    };
    passed &= Expect(
        besktop::HitTestStageGuideLayout(aboutCard, projectButtonCenter) ==
                besktop::StageGuideHitTarget::ViewProject &&
            besktop::IsStageGuideClickableTarget(
                besktop::StageGuideHitTarget::ViewProject),
        "project button hit test failed");

    const auto confirmation = besktop::ComputeStageGuideLayout({
        workArea, {1160.0, 650.0}, 62.0, 1.5, 1.0, {true, true, true},
        true, false, true, besktop::StageGuideMenuEntry::Support,
    });
    passed &= Expect(Inside(workArea, confirmation.cardRect) &&
            Inside(workArea, confirmation.confirmButtonRect) &&
            Inside(workArea, confirmation.continueButtonRect),
        "confirmation card controls escaped the work area");
    passed &= Expect(
        std::abs((confirmation.cardRect.right - confirmation.cardRect.left) - 480.0) < 0.01 &&
            std::abs((confirmation.cardRect.bottom - confirmation.cardRect.top) - 228.0) < 0.01,
        "confirmation card did not use the compact DPI-aware size");

    for (const auto area : std::array<besktop::StageGuideRect, 4>{{
            {0, 0, 1600, 952}, {0, 0, 1280, 720}, {40, 0, 800, 560}, {-1280, 40, 0, 1024}}}) {
        for (const double scale : {1.0, 1.25, 1.5, 2.0, 2.5}) {
            besktop::StageGuideLayoutInput input;
            input.workArea = area;
            input.bodyCenter = {area.left + 100, area.bottom - 100};
            input.dpiScale = scale;
            input.showSupport = true;
            const auto card = besktop::ComputeStageGuideLayout(input);
            for (const auto rect : {card.cardRect, card.closeCardRect, card.weChatButtonRect,
                    card.alipayButtonRect, card.supportImageRect, card.continueButtonRect}) {
                passed &= Expect(besktop::StageGuideRectIsValid(rect) && Inside(area, rect) &&
                        Inside(card.cardRect, rect), "support control escaped card/work area");
            }
            passed &= Expect(card.weChatButtonRect.right < card.alipayButtonRect.left &&
                    card.weChatButtonRect.bottom < card.supportImageRect.top &&
                    card.supportImageRect.bottom < card.continueButtonRect.top,
                "support controls overlap at a small-screen/DPI case");
            const auto hitCenter = [&](const besktop::StageGuideRect& rect) {
                return besktop::HitTestStageGuideLayout(card,
                    {(rect.left + rect.right) * 0.5, (rect.top + rect.bottom) * 0.5});
            };
            passed &= Expect(hitCenter(card.weChatButtonRect) == besktop::StageGuideHitTarget::SupportWeChat &&
                    hitCenter(card.alipayButtonRect) == besktop::StageGuideHitTarget::SupportAlipay &&
                    hitCenter(card.closeCardRect) == besktop::StageGuideHitTarget::CloseCard &&
                    hitCenter(card.continueButtonRect) == besktop::StageGuideHitTarget::ContinueWatching &&
                    hitCenter(card.supportImageRect) == besktop::StageGuideHitTarget::None,
                "support card hit test failed");
        }
    }
    if (!passed) return 1;
    std::cout << "besktop_stage_guide_layout_tests: all checks passed\n";
    return 0;
}
