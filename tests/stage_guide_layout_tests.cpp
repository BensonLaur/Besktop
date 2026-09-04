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

    const auto confirmation = besktop::ComputeStageGuideLayout({
        workArea, {1160.0, 650.0}, 62.0, 1.5, 1.0, {true, true, true},
        true, false, true, besktop::StageGuideMenuEntry::Support,
    });
    passed &= Expect(Inside(workArea, confirmation.cardRect) &&
            Inside(workArea, confirmation.confirmButtonRect) &&
            Inside(workArea, confirmation.continueButtonRect),
        "confirmation card controls escaped the work area");

    if (!passed) return 1;
    std::cout << "besktop_stage_guide_layout_tests: all checks passed\n";
    return 0;
}
