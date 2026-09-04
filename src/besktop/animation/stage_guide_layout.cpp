#include "besktop/animation/stage_guide_layout.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace {

double Width(const besktop::StageGuideRect& rect)
{
    return std::max(0.0, rect.right - rect.left);
}

double Height(const besktop::StageGuideRect& rect)
{
    return std::max(0.0, rect.bottom - rect.top);
}

besktop::StageGuideRect ClampRectToBounds(
    besktop::StageGuideRect rect,
    const besktop::StageGuideRect& bounds,
    double margin)
{
    const double availableWidth = std::max(1.0, Width(bounds) - margin * 2.0);
    const double availableHeight = std::max(1.0, Height(bounds) - margin * 2.0);
    const double width = std::min(Width(rect), availableWidth);
    const double height = std::min(Height(rect), availableHeight);
    rect.right = rect.left + width;
    rect.bottom = rect.top + height;

    const double minimumLeft = bounds.left + margin;
    const double maximumLeft = bounds.right - margin - width;
    const double minimumTop = bounds.top + margin;
    const double maximumTop = bounds.bottom - margin - height;
    rect.left = maximumLeft >= minimumLeft ?
        std::clamp(rect.left, minimumLeft, maximumLeft) : minimumLeft;
    rect.top = maximumTop >= minimumTop ?
        std::clamp(rect.top, minimumTop, maximumTop) : minimumTop;
    rect.right = rect.left + width;
    rect.bottom = rect.top + height;
    return rect;
}

double MenuItemWidth(besktop::StageGuideMenuEntry entry, double scale)
{
    switch (entry) {
    case besktop::StageGuideMenuEntry::Support:
        return 112.0 * scale;
    case besktop::StageGuideMenuEntry::About:
        return 100.0 * scale;
    case besktop::StageGuideMenuEntry::Feedback:
    default:
        return 88.0 * scale;
    }
}

besktop::StageGuideRect BuildCardRect(
    const besktop::StageGuideLayoutInput& input,
    double width,
    double height,
    double margin)
{
    const double scale = std::clamp(input.dpiScale, 0.75, 2.5);
    width = std::min(width * scale, std::max(1.0, Width(input.workArea) - margin * 2.0));
    height = std::min(height * scale, std::max(1.0, Height(input.workArea) - margin * 2.0));
    const double gap = 20.0 * scale;
    double top = input.bodyCenter.y - input.bodySize * 0.5 - gap - height;
    if (top < input.workArea.top + margin) {
        top = input.bodyCenter.y + input.bodySize * 0.5 + gap;
    }
    if (top + height > input.workArea.bottom - margin) {
        top = (input.workArea.top + input.workArea.bottom - height) * 0.5;
    }
    return ClampRectToBounds(
        {input.bodyCenter.x - width * 0.5, top,
            input.bodyCenter.x + width * 0.5, top + height},
        input.workArea,
        margin);
}

} // namespace

namespace besktop {

bool StageGuideRectIsValid(const StageGuideRect& rect)
{
    return std::isfinite(rect.left) && std::isfinite(rect.top) &&
        std::isfinite(rect.right) && std::isfinite(rect.bottom) &&
        rect.right > rect.left && rect.bottom > rect.top;
}

bool StageGuidePointInRect(const StageGuidePoint& point, const StageGuideRect& rect)
{
    return StageGuideRectIsValid(rect) &&
        point.x >= rect.left && point.x <= rect.right &&
        point.y >= rect.top && point.y <= rect.bottom;
}

StageGuideLayout ComputeStageGuideLayout(const StageGuideLayoutInput& input)
{
    StageGuideLayout layout;
    layout.workArea = input.workArea;
    layout.dpiScale = std::clamp(input.dpiScale, 0.75, 2.5);
    layout.menuProgress = std::clamp(input.menuProgress, 0.0, 1.0);
    layout.showMenu = input.showMenu;
    layout.showAbout = input.showAbout;
    layout.showExternalConfirmation = input.showExternalConfirmation;
    layout.confirmingEntry = input.confirmingEntry;

    const double bodySize = std::max(24.0, input.bodySize);
    layout.bodyRect = {
        input.bodyCenter.x - bodySize * 0.5,
        input.bodyCenter.y - bodySize * 0.5,
        input.bodyCenter.x + bodySize * 0.5,
        input.bodyCenter.y + bodySize * 0.5,
    };
    // The crab silhouette is wider than its branded shell: include claws and
    // outer walking legs while keeping the original vertical hover tolerance.
    layout.bodyHoverRect = {
        layout.bodyRect.left - bodySize * 0.50,
        layout.bodyRect.top - bodySize * 0.20,
        layout.bodyRect.right + bodySize * 0.50,
        layout.bodyRect.bottom + bodySize * 0.20,
    };

    if (layout.showMenu) {
        std::vector<StageGuideMenuEntry> entries;
        if (input.availability.feedback) entries.push_back(StageGuideMenuEntry::Feedback);
        if (input.availability.support) entries.push_back(StageGuideMenuEntry::Support);
        if (input.availability.about) entries.push_back(StageGuideMenuEntry::About);

        const double scale = layout.dpiScale;
        const double gap = 8.0 * scale;
        const double itemHeight = 42.0 * scale;
        const double edgeMargin = 12.0 * scale;
        double totalWidth = entries.empty() ? 0.0 : gap * static_cast<double>(entries.size() - 1);
        for (const StageGuideMenuEntry entry : entries) totalWidth += MenuItemWidth(entry, scale);
        totalWidth = std::min(totalWidth, std::max(1.0, Width(input.workArea) - edgeMargin * 2.0));

        const double verticalGap = 18.0 * scale;
        double menuTop = layout.bodyRect.top - verticalGap - itemHeight;
        layout.menuBelowBody = menuTop < input.workArea.top + edgeMargin;
        if (layout.menuBelowBody) menuTop = layout.bodyRect.bottom + verticalGap;
        const double finalTop = menuTop;
        menuTop += (layout.menuBelowBody ? -1.0 : 1.0) *
            (1.0 - layout.menuProgress) * 8.0 * scale;
        double menuLeft = input.bodyCenter.x - totalWidth * 0.5;
        menuLeft = std::clamp(
            menuLeft,
            input.workArea.left + edgeMargin,
            std::max(input.workArea.left + edgeMargin,
                input.workArea.right - edgeMargin - totalWidth));
        layout.menuBounds = {menuLeft, menuTop, menuLeft + totalWidth, menuTop + itemHeight};

        double cursor = menuLeft;
        for (const StageGuideMenuEntry entry : entries) {
            double itemWidth = MenuItemWidth(entry, scale);
            if (cursor + itemWidth > layout.menuBounds.right || entry == entries.back()) {
                itemWidth = std::max(1.0, layout.menuBounds.right - cursor);
            }
            layout.menuItems.push_back({entry, {cursor, menuTop, cursor + itemWidth, menuTop + itemHeight}});
            cursor += itemWidth + gap;
        }

        const double corridorHalfWidth = 24.0 * scale;
        const double menuCenterX = (layout.menuBounds.left + layout.menuBounds.right) * 0.5;
        const double corridorTop = layout.menuBelowBody ? layout.bodyRect.bottom : finalTop + itemHeight;
        const double corridorBottom = layout.menuBelowBody ? finalTop : layout.bodyRect.top;
        layout.interactionCorridor = {
            std::min(input.bodyCenter.x, menuCenterX) - corridorHalfWidth,
            std::min(corridorTop, corridorBottom),
            std::max(input.bodyCenter.x, menuCenterX) + corridorHalfWidth,
            std::max(corridorTop, corridorBottom),
        };
    }

    const double cardMargin = 14.0 * layout.dpiScale;
    if (layout.showAbout) {
        layout.cardRect = BuildCardRect(input, 470.0, 286.0, cardMargin);
    } else if (layout.showExternalConfirmation) {
        layout.cardRect = BuildCardRect(input, 410.0, 208.0, cardMargin);
    }

    if (StageGuideRectIsValid(layout.cardRect)) {
        const double scale = layout.dpiScale;
        const double closeSize = 30.0 * scale;
        const double inset = 14.0 * scale;
        layout.closeCardRect = {
            layout.cardRect.right - inset - closeSize,
            layout.cardRect.top + inset,
            layout.cardRect.right - inset,
            layout.cardRect.top + inset + closeSize,
        };
        if (layout.showExternalConfirmation) {
            const double buttonHeight = 38.0 * scale;
            const double buttonGap = 10.0 * scale;
            const double available = Width(layout.cardRect) - inset * 2.0 - buttonGap;
            const double buttonWidth = available * 0.5;
            const double top = layout.cardRect.bottom - inset - buttonHeight;
            layout.continueButtonRect = {
                layout.cardRect.left + inset,
                top,
                layout.cardRect.left + inset + buttonWidth,
                top + buttonHeight,
            };
            layout.confirmButtonRect = {
                layout.continueButtonRect.right + buttonGap,
                top,
                layout.cardRect.right - inset,
                top + buttonHeight,
            };
        }
    }
    return layout;
}

StageGuideHitTarget HitTestStageGuideLayout(
    const StageGuideLayout& layout,
    const StageGuidePoint& point)
{
    if (layout.showAbout) {
        if (StageGuidePointInRect(point, layout.closeCardRect)) return StageGuideHitTarget::CloseCard;
        return StageGuideHitTarget::None;
    }
    if (layout.showExternalConfirmation) {
        if (StageGuidePointInRect(point, layout.confirmButtonRect)) {
            return StageGuideHitTarget::ConfirmExternalAction;
        }
        if (StageGuidePointInRect(point, layout.continueButtonRect) ||
            StageGuidePointInRect(point, layout.closeCardRect)) {
            return StageGuideHitTarget::ContinueWatching;
        }
        return StageGuideHitTarget::None;
    }
    if (layout.showMenu && layout.menuProgress >= 0.82) {
        for (const StageGuideMenuItemLayout& item : layout.menuItems) {
            if (!StageGuidePointInRect(point, item.rect)) continue;
            switch (item.entry) {
            case StageGuideMenuEntry::Feedback:
                return StageGuideHitTarget::Feedback;
            case StageGuideMenuEntry::Support:
                return StageGuideHitTarget::Support;
            case StageGuideMenuEntry::About:
                return StageGuideHitTarget::About;
            default:
                break;
            }
        }
    }
    if (StageGuidePointInRect(point, layout.bodyHoverRect)) return StageGuideHitTarget::Body;
    return StageGuideHitTarget::None;
}

bool IsStageGuideInteractivePoint(
    const StageGuideLayout& layout,
    const StageGuidePoint& point)
{
    if (layout.showAbout || layout.showExternalConfirmation) {
        return StageGuidePointInRect(point, layout.cardRect);
    }
    if (StageGuidePointInRect(point, layout.bodyHoverRect)) return true;
    if (!layout.showMenu) return false;
    if (StageGuidePointInRect(point, layout.interactionCorridor)) return true;
    return std::any_of(layout.menuItems.begin(), layout.menuItems.end(), [&](const auto& item) {
        return StageGuidePointInRect(point, item.rect);
    });
}

bool IsStageGuideClickableTarget(StageGuideHitTarget target)
{
    switch (target) {
    case StageGuideHitTarget::Feedback:
    case StageGuideHitTarget::Support:
    case StageGuideHitTarget::About:
    case StageGuideHitTarget::CloseCard:
    case StageGuideHitTarget::ConfirmExternalAction:
    case StageGuideHitTarget::ContinueWatching:
        return true;
    default:
        return false;
    }
}

} // namespace besktop
