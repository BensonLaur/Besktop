#pragma once

#include <vector>

namespace besktop {

struct StageGuidePoint {
    double x = 0.0;
    double y = 0.0;
};

struct StageGuideRect {
    double left = 0.0;
    double top = 0.0;
    double right = 0.0;
    double bottom = 0.0;
};

enum class StageGuideMenuEntry {
    None,
    Feedback,
    Support,
    About,
};

enum class StageGuideHitTarget {
    None,
    Body,
    Feedback,
    Support,
    About,
    ViewProject,
    CloseCard,
    ConfirmExternalAction,
    ContinueWatching,
    SupportWeChat,
    SupportAlipay,
};

enum class StageGuideSupportProvider {
    WeChat,
    Alipay,
};

struct StageGuideMenuAvailability {
    bool feedback = false;
    bool support = false;
    bool about = true;
};

struct StageGuideMenuItemLayout {
    StageGuideMenuEntry entry = StageGuideMenuEntry::None;
    StageGuideRect rect{};
};

struct StageGuideLayoutInput {
    StageGuideRect workArea{};
    StageGuidePoint bodyCenter{};
    double bodySize = 62.0;
    double dpiScale = 1.0;
    double menuProgress = 0.0;
    StageGuideMenuAvailability availability{};
    bool showMenu = false;
    bool showAbout = false;
    bool showExternalConfirmation = false;
    StageGuideMenuEntry confirmingEntry = StageGuideMenuEntry::None;
    bool showSupport = false;
};

struct StageGuideLayout {
    StageGuideRect workArea{};
    StageGuideRect bodyRect{};
    StageGuideRect bodyHoverRect{};
    StageGuideRect menuBounds{};
    StageGuideRect interactionCorridor{};
    std::vector<StageGuideMenuItemLayout> menuItems;
    StageGuideRect cardRect{};
    StageGuideRect closeCardRect{};
    StageGuideRect projectButtonRect{};
    StageGuideRect confirmButtonRect{};
    StageGuideRect continueButtonRect{};
    StageGuideRect weChatButtonRect{};
    StageGuideRect alipayButtonRect{};
    StageGuideRect supportImageRect{};
    double supportScale = 1.0;
    double dpiScale = 1.0;
    double menuProgress = 0.0;
    bool menuBelowBody = false;
    bool showMenu = false;
    bool showAbout = false;
    bool showExternalConfirmation = false;
    StageGuideMenuEntry confirmingEntry = StageGuideMenuEntry::None;
    bool showSupport = false;
};

bool StageGuideRectIsValid(const StageGuideRect& rect);
bool StageGuidePointInRect(const StageGuidePoint& point, const StageGuideRect& rect);
StageGuideLayout ComputeStageGuideLayout(const StageGuideLayoutInput& input);
StageGuideHitTarget HitTestStageGuideLayout(
    const StageGuideLayout& layout,
    const StageGuidePoint& point);
bool IsStageGuideInteractivePoint(
    const StageGuideLayout& layout,
    const StageGuidePoint& point);
bool IsStageGuideClickableTarget(StageGuideHitTarget target);

} // namespace besktop
