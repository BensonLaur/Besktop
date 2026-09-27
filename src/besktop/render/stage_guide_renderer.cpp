#include "besktop/render/stage_guide_renderer.h"
#include "besktop/render/stage_guide_support_images.h"
#include "besktop/version.h"

#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <string>

namespace {

constexpr double kPi = 3.14159265358979323846;

float ToFloat(double value)
{
    return static_cast<float>(value);
}

BYTE Alpha(double value)
{
    return static_cast<BYTE>(std::clamp(value, 0.0, 255.0));
}

Gdiplus::RectF ToRect(const besktop::StageGuideRect& rect)
{
    return {
        ToFloat(rect.left),
        ToFloat(rect.top),
        ToFloat(std::max(0.0, rect.right - rect.left)),
        ToFloat(std::max(0.0, rect.bottom - rect.top)),
    };
}

void AddRoundedRectPath(
    Gdiplus::GraphicsPath& path,
    const Gdiplus::RectF& rect,
    float radius)
{
    const float diameter = std::min({radius * 2.0f, rect.Width, rect.Height});
    if (diameter <= 1.0f) {
        path.AddRectangle(rect);
        return;
    }
    path.AddArc(rect.X, rect.Y, diameter, diameter, 180.0f, 90.0f);
    path.AddArc(rect.GetRight() - diameter, rect.Y, diameter, diameter, 270.0f, 90.0f);
    path.AddArc(rect.GetRight() - diameter, rect.GetBottom() - diameter,
        diameter, diameter, 0.0f, 90.0f);
    path.AddArc(rect.X, rect.GetBottom() - diameter, diameter, diameter, 90.0f, 90.0f);
    path.CloseFigure();
}

void DrawCenteredText(
    Gdiplus::Graphics& graphics,
    const std::wstring& text,
    const Gdiplus::RectF& rect,
    const Gdiplus::Font& font,
    Gdiplus::Color color)
{
    Gdiplus::StringFormat format;
    format.SetAlignment(Gdiplus::StringAlignmentCenter);
    format.SetLineAlignment(Gdiplus::StringAlignmentCenter);
    format.SetTrimming(Gdiplus::StringTrimmingEllipsisCharacter);
    Gdiplus::SolidBrush brush(color);
    graphics.DrawString(text.c_str(), -1, &font, rect, &format, &brush);
}

void DrawText(
    Gdiplus::Graphics& graphics,
    const std::wstring& text,
    const Gdiplus::RectF& rect,
    const Gdiplus::Font& font,
    Gdiplus::Color color)
{
    Gdiplus::StringFormat format;
    format.SetAlignment(Gdiplus::StringAlignmentNear);
    format.SetLineAlignment(Gdiplus::StringAlignmentNear);
    format.SetFormatFlags(Gdiplus::StringFormatFlagsLineLimit);
    Gdiplus::SolidBrush brush(color);
    graphics.DrawString(text.c_str(), -1, &font, rect, &format, &brush);
}

void DrawChain(
    Gdiplus::Graphics& graphics,
    Gdiplus::Pen& pen,
    const Gdiplus::PointF& root,
    const Gdiplus::PointF& joint,
    const Gdiplus::PointF& end)
{
    const Gdiplus::PointF points[]{root, joint, end};
    graphics.DrawLines(&pen, points, 3);
}

void DrawCharacter(
    Gdiplus::Graphics& graphics,
    const besktop::StageGuideNpcState& state,
    const besktop::StageGuideLayout& layout)
{
    if (!besktop::StageGuideNpcIsVisible(state)) return;
    (void)layout;
    const double size = state.bodySize;
    const double gait = state.moving ? std::sin(state.motionPhase * 2.0 * kPi) : 0.0;
    const double bob = state.moving ?
        -std::abs(std::sin(state.motionPhase * 2.0 * kPi)) * size * 0.012 : 0.0;
    const double attention = state.phase == besktop::StageGuideNpcPhase::NoticingPointer ||
        besktop::StageGuideNpcShowsMenu(state) ? 1.0 : 0.0;
    const double leaveProgress = state.phase == besktop::StageGuideNpcPhase::LeavingForExternalAction ?
        std::clamp(
            state.leavingElapsedSeconds /
                besktop::GetStageGuideNpcTuning().externalActionLeaveSeconds,
            0.0,
            1.0) : 0.0;
    const BYTE characterAlpha = Alpha(255.0 * (1.0 - leaveProgress * 0.72));
    const float limbWidth = ToFloat(std::clamp(size * 0.105, 5.5, 9.5));
    Gdiplus::Pen shadow(Gdiplus::Color(Alpha(characterAlpha * 0.20), 0, 0, 0), limbWidth + 3.0f);
    shadow.SetStartCap(Gdiplus::LineCapRound);
    shadow.SetEndCap(Gdiplus::LineCapRound);
    Gdiplus::Pen limb(Gdiplus::Color(characterAlpha, 250, 253, 255), limbWidth);
    limb.SetStartCap(Gdiplus::LineCapRound);
    limb.SetEndCap(Gdiplus::LineCapRound);
    const float legWidth = ToFloat(std::clamp(size * 0.075, 4.0, 7.0));
    Gdiplus::Pen legShadow(
        Gdiplus::Color(Alpha(characterAlpha * 0.18), 0, 0, 0), legWidth + 2.0f);
    legShadow.SetStartCap(Gdiplus::LineCapRound);
    legShadow.SetEndCap(Gdiplus::LineCapRound);
    Gdiplus::Pen leg(Gdiplus::Color(characterAlpha, 238, 249, 255), legWidth);
    leg.SetStartCap(Gdiplus::LineCapRound);
    leg.SetEndCap(Gdiplus::LineCapRound);

    const double centerX = state.position.x;
    const double centerY = state.position.y + bob + leaveProgress * size * 0.08;
    const auto offset = [](const Gdiplus::PointF& point) {
        return Gdiplus::PointF(point.X + 2.2f, point.Y + 2.8f);
    };
    const auto drawChainWithShadow = [&](const Gdiplus::PointF& root,
                                         const Gdiplus::PointF& joint,
                                         const Gdiplus::PointF& end) {
        DrawChain(graphics, shadow, offset(root), offset(joint), offset(end));
        DrawChain(graphics, limb, root, joint, end);
    };

    // B仔始终正面朝向观众，以螃蟹横行为唯一移动语言，不复用普通演员的转身。
    for (int side = -1; side <= 1; side += 2) {
        for (int legIndex = 0; legIndex < 3; ++legIndex) {
            const double alternating = ((legIndex + (side > 0 ? 1 : 0)) % 2 == 0) ? gait : -gait;
            const double rootY = centerY + size * (-0.02 + legIndex * 0.12);
            const double jointDrop = legIndex == 0 ? -0.01 :
                (legIndex == 1 ? 0.07 : 0.15);
            const double footDrop = legIndex == 0 ? 0.08 :
                (legIndex == 1 ? 0.20 : 0.34);
            const double jointReach = legIndex == 0 ? 0.63 :
                (legIndex == 1 ? 0.64 : 0.60);
            const double footReach = legIndex == 0 ? 0.82 :
                (legIndex == 1 ? 0.79 : 0.72);
            const Gdiplus::PointF root(
                ToFloat(centerX + side * size * 0.43),
                ToFloat(rootY));
            const Gdiplus::PointF joint(
                ToFloat(centerX + side * size * (jointReach + alternating * 0.025)),
                ToFloat(rootY + size * jointDrop));
            const Gdiplus::PointF foot(
                ToFloat(centerX + side * size * (footReach + alternating * 0.050)),
                ToFloat(rootY + size * (footDrop - std::abs(alternating) * 0.012)));
            DrawChain(graphics, legShadow, offset(root), offset(joint), offset(foot));
            DrawChain(graphics, leg, root, joint, foot);
        }
    }

    const double eyeRootY = centerY - size * 0.23;
    const double eyeCenterY = centerY - size * 0.50;
    for (int side = -1; side <= 1; side += 2) {
        const Gdiplus::PointF root(
            ToFloat(centerX + side * size * 0.19), ToFloat(eyeRootY));
        const Gdiplus::PointF eyeBase(
            ToFloat(centerX + side * size * 0.19), ToFloat(eyeCenterY));
        graphics.DrawLine(&shadow, offset(root), offset(eyeBase));
        graphics.DrawLine(&limb, root, eyeBase);
    }

    Gdiplus::RectF body(
        ToFloat(centerX - size * 0.53),
        ToFloat(centerY - size * 0.27),
        ToFloat(size * 1.06),
        ToFloat(size * 0.62));
    Gdiplus::GraphicsPath bodyPath;
    AddRoundedRectPath(bodyPath, body, ToFloat(size * 0.23));
    Gdiplus::SolidBrush bodyShadow(Gdiplus::Color(Alpha(characterAlpha * 0.28), 0, 0, 0));
    Gdiplus::GraphicsPath shadowPath;
    AddRoundedRectPath(
        shadowPath,
        Gdiplus::RectF(body.X + 3.0f, body.Y + 4.0f, body.Width, body.Height),
        ToFloat(size * 0.23));
    graphics.FillPath(&bodyShadow, &shadowPath);
    Gdiplus::LinearGradientBrush bodyBrush(
        body,
        Gdiplus::Color(characterAlpha, 29, 151, 255),
        Gdiplus::Color(characterAlpha, 13, 92, 222),
        Gdiplus::LinearGradientModeForwardDiagonal);
    graphics.FillPath(&bodyBrush, &bodyPath);
    Gdiplus::Pen border(Gdiplus::Color(Alpha(characterAlpha * 0.82), 206, 239, 255), 2.0f);
    graphics.DrawPath(&border, &bodyPath);

    Gdiplus::FontFamily family(L"Segoe UI");
    Gdiplus::Font logoFont(
        &family,
        ToFloat(size * 0.48),
        Gdiplus::FontStyleBold,
        Gdiplus::UnitPixel);
    DrawCenteredText(
        graphics,
        L"B",
        Gdiplus::RectF(body.X, body.Y - ToFloat(size * 0.025), body.Width, body.Height),
        logoFont,
        Gdiplus::Color(characterAlpha, 255, 255, 255));

    const double pointerDx = state.pointerPresent ? std::clamp(
        (state.pointerPosition.x - state.position.x) / std::max(1.0, size * 2.2),
        -1.0, 1.0) : 0.0;
    const double pointerDy = state.pointerPresent ? std::clamp(
        (state.pointerPosition.y - state.position.y) / std::max(1.0, size * 2.2),
        -1.0, 1.0) : 0.0;
    const float eyeDiameter = ToFloat(std::max(10.0, size * 0.18));
    const float pupilDiameter = ToFloat(std::max(4.0, size * 0.075));
    Gdiplus::SolidBrush eyeWhite(Gdiplus::Color(characterAlpha, 250, 253, 255));
    Gdiplus::SolidBrush pupil(Gdiplus::Color(characterAlpha, 7, 48, 103));
    for (int side = -1; side <= 1; side += 2) {
        const float eyeX = ToFloat(centerX + side * size * 0.19);
        const float eyeY = ToFloat(eyeCenterY);
        graphics.FillEllipse(
            &eyeWhite,
            eyeX - eyeDiameter * 0.5f,
            eyeY - eyeDiameter * 0.5f,
            eyeDiameter,
            eyeDiameter);
        graphics.FillEllipse(
            &pupil,
            eyeX + ToFloat(pointerDx * size * 0.035) - pupilDiameter * 0.5f,
            eyeY + ToFloat(pointerDy * size * 0.030) - pupilDiameter * 0.5f,
            pupilDiameter,
            pupilDiameter);
    }

    const double wave = attention * (0.10 + 0.08 * std::sin(state.startupElapsedSeconds * 7.0));
    for (int side = -1; side <= 1; side += 2) {
        const double raise = side > 0 ? attention * 0.20 + wave : 0.0;
        const Gdiplus::PointF root(
            ToFloat(centerX + side * size * 0.43),
            ToFloat(centerY - size * 0.08));
        const Gdiplus::PointF elbow(
            ToFloat(centerX + side * size * 0.63),
            ToFloat(centerY - size * (0.16 + raise * 0.55)));
        const Gdiplus::PointF claw(
            ToFloat(centerX + side * size * 0.80),
            ToFloat(centerY - size * (0.20 + raise)));
        drawChainWithShadow(root, elbow, claw);
        const Gdiplus::PointF upperTip(
            ToFloat(claw.X + side * size * 0.13),
            ToFloat(claw.Y - size * 0.10));
        const Gdiplus::PointF lowerTip(
            ToFloat(claw.X + side * size * 0.13),
            ToFloat(claw.Y + size * 0.10));
        graphics.DrawLine(&shadow, offset(claw), offset(upperTip));
        graphics.DrawLine(&shadow, offset(claw), offset(lowerTip));
        graphics.DrawLine(&limb, claw, upperTip);
        graphics.DrawLine(&limb, claw, lowerTip);
    }

}

std::wstring MenuLabel(besktop::StageGuideMenuEntry entry)
{
    switch (entry) {
    case besktop::StageGuideMenuEntry::Feedback: return L"反馈";
    case besktop::StageGuideMenuEntry::Support: return L"支持作者";
    case besktop::StageGuideMenuEntry::About: return L"这是啥？";
    default: return {};
    }
}

void DrawMenuIcon(
    Gdiplus::Graphics& graphics,
    besktop::StageGuideMenuEntry entry,
    const Gdiplus::RectF& rect,
    BYTE alpha)
{
    const float size = std::min(rect.Width, rect.Height) * 0.35f;
    const float cx = rect.X + rect.Width * 0.18f;
    const float cy = rect.Y + rect.Height * 0.50f;
    Gdiplus::Pen pen(Gdiplus::Color(alpha, 38, 151, 239), 2.0f);
    pen.SetStartCap(Gdiplus::LineCapRound);
    pen.SetEndCap(Gdiplus::LineCapRound);
    if (entry == besktop::StageGuideMenuEntry::Feedback) {
        graphics.DrawRectangle(&pen, cx - size * 0.55f, cy - size * 0.42f, size, size * 0.70f);
        graphics.DrawLine(&pen, cx - size * 0.18f, cy + size * 0.28f,
            cx - size * 0.42f, cy + size * 0.52f);
    } else if (entry == besktop::StageGuideMenuEntry::Support) {
        Gdiplus::GraphicsPath heart;
        heart.AddBezier(cx, cy + size * 0.48f,
            cx - size * 0.85f, cy - size * 0.08f,
            cx - size * 0.45f, cy - size * 0.72f,
            cx, cy - size * 0.26f);
        heart.AddBezier(cx, cy - size * 0.26f,
            cx + size * 0.45f, cy - size * 0.72f,
            cx + size * 0.85f, cy - size * 0.08f,
            cx, cy + size * 0.48f);
        heart.CloseFigure();
        graphics.DrawPath(&pen, &heart);
    } else {
        graphics.DrawEllipse(&pen, cx - size * 0.48f, cy - size * 0.48f, size, size);
        Gdiplus::FontFamily family(L"Segoe UI");
        Gdiplus::Font font(&family, size * 0.82f, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
        DrawCenteredText(graphics, L"i",
            Gdiplus::RectF(cx - size * 0.5f, cy - size * 0.58f, size, size * 1.05f),
            font, Gdiplus::Color(alpha, 38, 151, 239));
    }
}

void DrawMenu(
    Gdiplus::Graphics& graphics,
    const besktop::StageGuideLayout& layout)
{
    if (!layout.showMenu || layout.menuProgress <= 0.01) return;
    const BYTE alpha = Alpha(255.0 * layout.menuProgress);
    Gdiplus::FontFamily family(L"Microsoft YaHei UI");
    Gdiplus::Font font(&family, ToFloat(14.0 * layout.dpiScale),
        Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
    for (const auto& item : layout.menuItems) {
        Gdiplus::RectF rect = ToRect(item.rect);
        Gdiplus::GraphicsPath path;
        AddRoundedRectPath(path, rect, ToFloat(13.0 * layout.dpiScale));
        Gdiplus::SolidBrush shadow(Gdiplus::Color(Alpha(alpha * 0.28), 0, 0, 0));
        Gdiplus::GraphicsPath shadowPath;
        AddRoundedRectPath(
            shadowPath,
            Gdiplus::RectF(rect.X + 2.5f, rect.Y + 3.0f, rect.Width, rect.Height),
            ToFloat(13.0 * layout.dpiScale));
        graphics.FillPath(&shadow, &shadowPath);
        Gdiplus::SolidBrush background(Gdiplus::Color(Alpha(alpha * 0.96), 245, 251, 255));
        graphics.FillPath(&background, &path);
        Gdiplus::Pen border(Gdiplus::Color(Alpha(alpha * 0.76), 80, 184, 245), 1.4f);
        graphics.DrawPath(&border, &path);
        DrawMenuIcon(graphics, item.entry, rect, alpha);
        DrawCenteredText(
            graphics,
            MenuLabel(item.entry),
            Gdiplus::RectF(rect.X + rect.Width * 0.28f, rect.Y,
                rect.Width * 0.68f, rect.Height),
            font,
            Gdiplus::Color(alpha, 18, 66, 108));
    }
}

struct CardTail {
    std::array<Gdiplus::PointF, 3> points{};
    bool visible = false;
};

CardTail BuildCardTail(const besktop::StageGuideLayout& layout)
{
    const double scale = layout.dpiScale;
    const double bodyCenterX = (layout.bodyRect.left + layout.bodyRect.right) * 0.5;
    const double tailInset = std::min(
        30.0 * scale,
        std::max(0.0, (layout.cardRect.right - layout.cardRect.left) * 0.5 - 1.0));
    const double tailCenterX = std::clamp(
        bodyCenterX,
        layout.cardRect.left + tailInset,
        layout.cardRect.right - tailInset);
    const double tailHalfWidth = 11.0 * scale;
    CardTail tail;
    if (layout.cardRect.bottom <= layout.bodyRect.top) {
        tail.points = {{
            {ToFloat(tailCenterX - tailHalfWidth), ToFloat(layout.cardRect.bottom - 1.0)},
            {ToFloat(tailCenterX + tailHalfWidth), ToFloat(layout.cardRect.bottom - 1.0)},
            {ToFloat(bodyCenterX), ToFloat(layout.bodyRect.top - 2.0 * scale)},
        }};
        tail.visible = true;
    } else if (layout.cardRect.top >= layout.bodyRect.bottom) {
        tail.points = {{
            {ToFloat(tailCenterX - tailHalfWidth), ToFloat(layout.cardRect.top + 1.0)},
            {ToFloat(tailCenterX + tailHalfWidth), ToFloat(layout.cardRect.top + 1.0)},
            {ToFloat(bodyCenterX), ToFloat(layout.bodyRect.bottom + 2.0 * scale)},
        }};
        tail.visible = true;
    }
    return tail;
}

void DrawCardBackground(
    Gdiplus::Graphics& graphics,
    const besktop::StageGuideLayout& layout)
{
    const Gdiplus::RectF rect = ToRect(layout.cardRect);
    const double scale = layout.dpiScale;
    const auto cardTail = BuildCardTail(layout);
    const auto& tail = cardTail.points;
    const bool hasTail = cardTail.visible;

    if (hasTail) {
        auto shadowTail = tail;
        for (Gdiplus::PointF& point : shadowTail) {
            point.X += 4.0f;
            point.Y += 5.0f;
        }
        Gdiplus::SolidBrush tailShadow(Gdiplus::Color(76, 0, 0, 0));
        graphics.FillPolygon(&tailShadow, shadowTail.data(), static_cast<INT>(shadowTail.size()));
    }
    Gdiplus::GraphicsPath shadowPath;
    AddRoundedRectPath(
        shadowPath,
        Gdiplus::RectF(rect.X + 4.0f, rect.Y + 5.0f, rect.Width, rect.Height),
        ToFloat(18.0 * scale));
    Gdiplus::SolidBrush shadow(Gdiplus::Color(76, 0, 0, 0));
    graphics.FillPath(&shadow, &shadowPath);
    Gdiplus::GraphicsPath path;
    AddRoundedRectPath(path, rect, ToFloat(18.0 * scale));
    Gdiplus::LinearGradientBrush background(
        rect,
        Gdiplus::Color(255, 22, 82, 142),
        Gdiplus::Color(255, 12, 46, 88),
        Gdiplus::LinearGradientModeVertical);
    if (hasTail) graphics.FillPolygon(&background, tail.data(), static_cast<INT>(tail.size()));
    graphics.FillPath(&background, &path);
    Gdiplus::Pen border(Gdiplus::Color(190, 126, 211, 255), 1.5f);
    graphics.DrawPath(&border, &path);
    if (hasTail) {
        graphics.DrawLine(&border, tail[0], tail[2]);
        graphics.DrawLine(&border, tail[2], tail[1]);
    }
}

void DrawCloseButton(
    Gdiplus::Graphics& graphics,
    const besktop::StageGuideRect& rect,
    double scale)
{
    Gdiplus::Pen pen(Gdiplus::Color(215, 223, 243, 255), ToFloat(2.0 * scale));
    pen.SetStartCap(Gdiplus::LineCapRound);
    pen.SetEndCap(Gdiplus::LineCapRound);
    const double inset = 9.0 * scale;
    graphics.DrawLine(&pen,
        ToFloat(rect.left + inset), ToFloat(rect.top + inset),
        ToFloat(rect.right - inset), ToFloat(rect.bottom - inset));
    graphics.DrawLine(&pen,
        ToFloat(rect.right - inset), ToFloat(rect.top + inset),
        ToFloat(rect.left + inset), ToFloat(rect.bottom - inset));
}

void DrawButton(
    Gdiplus::Graphics& graphics,
    const besktop::StageGuideRect& rect,
    const std::wstring& label,
    bool primary,
    double scale,
    const Gdiplus::Font& font);

void DrawAboutCard(
    Gdiplus::Graphics& graphics,
    const besktop::StageGuideLayout& layout)
{
    if (!layout.showAbout) return;
    DrawCardBackground(graphics, layout);
    DrawCloseButton(graphics, layout.closeCardRect, layout.dpiScale);
    const Gdiplus::RectF card = ToRect(layout.cardRect);
    Gdiplus::FontFamily family(L"Microsoft YaHei UI");
    Gdiplus::Font titleFont(&family, ToFloat(18.0 * layout.dpiScale),
        Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
    Gdiplus::Font bodyFont(&family, ToFloat(13.0 * layout.dpiScale),
        Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
    const Gdiplus::RectF titleRect(card.X + ToFloat(20.0 * layout.dpiScale),
        card.Y + ToFloat(16.0 * layout.dpiScale),
        card.Width - ToFloat(58.0 * layout.dpiScale), ToFloat(30.0 * layout.dpiScale));
    DrawText(graphics, L"这是 Besktop", titleRect,
        titleFont, Gdiplus::Color(255, 255, 255, 255));

    Gdiplus::Font versionFont(&family, ToFloat(10.8 * layout.dpiScale),
        Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
    Gdiplus::StringFormat versionFormat;
    versionFormat.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap);
    versionFormat.SetTrimming(Gdiplus::StringTrimmingEllipsisCharacter);
    Gdiplus::RectF measuredTitle;
    graphics.MeasureString(L"这是 Besktop", -1, &titleFont, titleRect,
        &versionFormat, &measuredTitle);
    const float versionX = titleRect.X + measuredTitle.Width + ToFloat(8.0 * layout.dpiScale);
    // Align the different font sizes by ascent, while reserving the close button.
    const float baselineOffset = titleFont.GetSize() * family.GetCellAscent(Gdiplus::FontStyleBold) /
        family.GetEmHeight(Gdiplus::FontStyleBold) -
        versionFont.GetSize() * family.GetCellAscent(Gdiplus::FontStyleRegular) /
        family.GetEmHeight(Gdiplus::FontStyleRegular);
    const float versionWidth = ToFloat(layout.closeCardRect.left - 8.0 * layout.dpiScale) - versionX;
    if (versionWidth > 0.0f) {
        Gdiplus::SolidBrush versionBrush(Gdiplus::Color(255, 184, 211, 234));
        graphics.DrawString(besktop::kBuildLabel, -1, &versionFont,
            Gdiplus::RectF(versionX, titleRect.Y + baselineOffset,
                versionWidth, titleRect.Height - baselineOffset),
            &versionFormat, &versionBrush);
    }
    const std::wstring body =
        L"Besktop 会让桌面图标醒来，在桌面上散步和互动。\n\n"
        L"放心，它只在屏幕上演出，不会移动、删除或改写你的真实文件，"
        L"也不自启动、不后台驻留、不提权。\n\n"
        L"P 自动互动  ·  Esc 退出\n"
        L"开源项目  BensonLaur/Besktop";
    DrawText(graphics, body,
        Gdiplus::RectF(card.X + ToFloat(20.0 * layout.dpiScale),
            card.Y + ToFloat(54.0 * layout.dpiScale),
            card.Width - ToFloat(40.0 * layout.dpiScale),
            card.Height - ToFloat(66.0 * layout.dpiScale)),
        bodyFont, Gdiplus::Color(238, 229, 244, 255));
    DrawButton(
        graphics,
        layout.projectButtonRect,
        L"查看项目",
        true,
        layout.dpiScale,
        bodyFont);
}

void DrawButton(
    Gdiplus::Graphics& graphics,
    const besktop::StageGuideRect& rect,
    const std::wstring& label,
    bool primary,
    double scale,
    const Gdiplus::Font& font)
{
    const Gdiplus::RectF value = ToRect(rect);
    Gdiplus::GraphicsPath path;
    AddRoundedRectPath(path, value, ToFloat(10.0 * scale));
    Gdiplus::SolidBrush background(primary ?
        Gdiplus::Color(250, 36, 159, 247) : Gdiplus::Color(216, 233, 244, 255));
    graphics.FillPath(&background, &path);
    DrawCenteredText(graphics, label, value, font,
        primary ? Gdiplus::Color(255, 255, 255, 255) : Gdiplus::Color(255, 21, 69, 110));
}

void DrawConfirmationCard(
    Gdiplus::Graphics& graphics,
    const besktop::StageGuideLayout& layout)
{
    if (!layout.showExternalConfirmation) return;
    DrawCardBackground(graphics, layout);
    DrawCloseButton(graphics, layout.closeCardRect, layout.dpiScale);
    const Gdiplus::RectF card = ToRect(layout.cardRect);
    Gdiplus::FontFamily family(L"Microsoft YaHei UI");
    Gdiplus::Font titleFont(&family, ToFloat(16.0 * layout.dpiScale),
        Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
    Gdiplus::Font bodyFont(&family, ToFloat(12.5 * layout.dpiScale),
        Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
    const bool feedback = layout.confirmingEntry == besktop::StageGuideMenuEntry::Feedback;
    const std::wstring title = feedback ? L"有想法？告诉我" : L"喜欢这场演出？";
    const std::wstring body = feedback ?
        L"将结束本次演出，并前往反馈页。" :
        L"将结束本次演出，并去看看如何支持 B仔。";
    DrawText(graphics, title,
        Gdiplus::RectF(card.X + ToFloat(18.0 * layout.dpiScale),
            card.Y + ToFloat(14.0 * layout.dpiScale),
            card.Width - ToFloat(58.0 * layout.dpiScale),
            ToFloat(26.0 * layout.dpiScale)),
        titleFont, Gdiplus::Color(255, 255, 255, 255));
    DrawText(graphics, body,
        Gdiplus::RectF(card.X + ToFloat(18.0 * layout.dpiScale),
            card.Y + ToFloat(50.0 * layout.dpiScale),
            card.Width - ToFloat(36.0 * layout.dpiScale),
            ToFloat(38.0 * layout.dpiScale)),
        bodyFont, Gdiplus::Color(235, 226, 243, 255));
    DrawButton(graphics, layout.continueButtonRect, feedback ? L"先不去" : L"继续看", false,
        layout.dpiScale, bodyFont);
    DrawButton(graphics, layout.confirmButtonRect, feedback ? L"去反馈" : L"去看看", true,
        layout.dpiScale, bodyFont);
}

void DrawSupportCard(
    Gdiplus::Graphics& graphics,
    const besktop::StageGuideNpcState& state,
    const besktop::StageGuideLayout& layout,
    const besktop::StageGuideSupportImages* images)
{
    if (!layout.showSupport) return;
    const double scale = layout.supportScale;
    auto cardLayout = layout;
    cardLayout.dpiScale = scale;
    DrawCardBackground(graphics, cardLayout);
    DrawCloseButton(graphics, layout.closeCardRect, scale);
    const auto card = ToRect(layout.cardRect);
    Gdiplus::FontFamily family(L"Microsoft YaHei UI");
    Gdiplus::Font titleFont(&family, ToFloat(20.0 * scale),
        Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
    Gdiplus::Font bodyFont(&family, ToFloat(13.0 * scale),
        Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
    Gdiplus::Font buttonFont(&family, ToFloat(15.0 * scale),
        Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
    DrawText(graphics, L"小螃蟹补给站",
        {card.X + ToFloat(20 * scale), card.Y + ToFloat(14 * scale),
            ToFloat(380 * scale), ToFloat(32 * scale)},
        titleFont, Gdiplus::Color(255, 255, 255));
    const bool weChat = state.supportProvider == besktop::StageGuideSupportProvider::WeChat;
    DrawButton(graphics, layout.weChatButtonRect, L"微信", weChat, scale, buttonFont);
    DrawButton(graphics, layout.alipayButtonRect, L"支付宝", !weChat, scale, buttonFont);
    if (images == nullptr || !images->Draw(graphics, state.supportProvider, layout.supportImageRect)) {
        DrawCenteredText(graphics, L"收款图片暂不可用，请继续欣赏演出。",
            ToRect(layout.supportImageRect), bodyFont, Gdiplus::Color(255, 255, 255));
    }
    DrawCenteredText(graphics,
        weChat ? L"用微信扫码，可选择补给档位或自行输入金额" : L"用支付宝扫码，自愿选择支持金额",
        {card.X + ToFloat(16 * scale), card.Y + ToFloat(708 * scale),
            card.Width - ToFloat(32 * scale), ToFloat(22 * scale)},
        bodyFont, Gdiplus::Color(255, 255, 255));
    DrawCenteredText(graphics, L"自愿支持开发与维护，不购买实物、不解锁功能。\n不打赏，也能完整使用当前免费功能。",
        {card.X + ToFloat(16 * scale), card.Y + ToFloat(734 * scale),
            card.Width - ToFloat(32 * scale), ToFloat(38 * scale)},
        bodyFont, Gdiplus::Color(230, 233, 244, 255));
    DrawButton(graphics, layout.continueButtonRect, L"继续观看", false, scale, buttonFont);
}

} // namespace

namespace besktop {

struct StageGuideCardCache::Impl {
    struct Key {
        std::array<std::array<double, 4>, 9> rectangles;
        double dpiScale;
        double supportScale;
        bool about;
        bool confirmation;
        bool support;
        StageGuideMenuEntry confirmingEntry;
        StageGuideSupportProvider provider;
        const StageGuideSupportImages* images;
        std::uint64_t imageRevision;
        bool operator==(const Key&) const = default;
    };

    static Key MakeKey(const StageGuideNpcState& state, const StageGuideLayout& layout,
        const StageGuideSupportImages* images)
    {
        const auto rect = [](const StageGuideRect& r) {
            return std::array<double, 4>{r.left, r.top, r.right, r.bottom};
        };
        return {{rect(layout.bodyRect), rect(layout.cardRect), rect(layout.closeCardRect),
            rect(layout.projectButtonRect), rect(layout.confirmButtonRect),
            rect(layout.continueButtonRect), rect(layout.weChatButtonRect),
            rect(layout.alipayButtonRect), rect(layout.supportImageRect)},
            layout.dpiScale, layout.supportScale, layout.showAbout,
            layout.showExternalConfirmation, layout.showSupport, layout.confirmingEntry,
            state.supportProvider, images, images ? images->Revision() : 0};
    }

    Key key{};
    Gdiplus::Rect bounds;
    std::unique_ptr<Gdiplus::Bitmap> bitmap;
    std::size_t builds = 0;
};

StageGuideCardCache::StageGuideCardCache() : impl_(std::make_unique<Impl>()) {}
StageGuideCardCache::~StageGuideCardCache() = default;

void StageGuideCardCache::Clear()
{
    impl_->bitmap.reset();
}

std::size_t StageGuideCardCache::BuildCount() const
{
    return impl_->builds;
}

bool StageGuideCardCache::Draw(Gdiplus::Graphics& graphics, const StageGuideNpcState& state,
    const StageGuideLayout& layout, const StageGuideSupportImages* images)
{
    if (!layout.showAbout && !layout.showExternalConfirmation && !layout.showSupport) return false;
    const auto key = Impl::MakeKey(state, layout, images);
    if (!impl_->bitmap || !(impl_->key == key)) {
        // Never keep the old provider's pixels as a fallback after a rebuild failure.
        Clear();
        if (!StageGuideRectIsValid(layout.cardRect)) return false;
        auto extent = layout.cardRect;
        auto tailLayout = layout;
        if (layout.showSupport) tailLayout.dpiScale = layout.supportScale;
        const auto tail = BuildCardTail(tailLayout);
        if (tail.visible) {
            for (const auto& point : tail.points) {
                extent.left = std::min(extent.left, static_cast<double>(point.X));
                extent.top = std::min(extent.top, static_cast<double>(point.Y));
                extent.right = std::max(extent.right, static_cast<double>(point.X));
                extent.bottom = std::max(extent.bottom, static_cast<double>(point.Y));
            }
        }
        // Include the speech tail, shadow (4,5) and antialiased border. Integer
        // translation preserves the original subpixel phase of text and QR modules.
        const double left = std::floor(extent.left) - 2;
        const double top = std::floor(extent.top) - 2;
        const double width = std::ceil(extent.right) + 6 - left;
        const double height = std::ceil(extent.bottom) + 7 - top;
        for (const double value : {left, top, width, height}) {
            if (!std::isfinite(value) || value < std::numeric_limits<INT>::min() ||
                value > std::numeric_limits<INT>::max()) return false;
        }
        // Bound the optional cache's memory; oversized cards still use direct drawing.
        if (width <= 0 || height <= 0 || width * height > 32.0 * 1024 * 1024) return false;
        const Gdiplus::Rect bounds(static_cast<INT>(left), static_cast<INT>(top),
            static_cast<INT>(width), static_cast<INT>(height));
        auto bitmap = std::make_unique<Gdiplus::Bitmap>(bounds.Width, bounds.Height, PixelFormat32bppPARGB);
        if (bitmap->GetLastStatus() != Gdiplus::Ok) return false;
        {
            Gdiplus::Graphics cached(bitmap.get());
            if (cached.GetLastStatus() != Gdiplus::Ok) return false;
            cached.Clear(Gdiplus::Color(0, 0, 0, 0));
            cached.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
            cached.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);
            cached.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
            cached.TranslateTransform(-static_cast<float>(bounds.X), -static_cast<float>(bounds.Y));
            DrawAboutCard(cached, layout);
            DrawConfirmationCard(cached, layout);
            DrawSupportCard(cached, state, layout, images);
            cached.Flush(Gdiplus::FlushIntentionSync);
            if (cached.GetLastStatus() != Gdiplus::Ok) return false;
        }
        impl_->bounds = bounds;
        impl_->key = key;
        impl_->bitmap = std::move(bitmap);
        ++impl_->builds;
    }
    const auto saved = graphics.Save();
    graphics.SetInterpolationMode(Gdiplus::InterpolationModeNearestNeighbor);
    graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
    const auto status = graphics.DrawImage(impl_->bitmap.get(), impl_->bounds,
        0, 0, impl_->bounds.Width, impl_->bounds.Height, Gdiplus::UnitPixel);
    graphics.Restore(saved);
    return status == Gdiplus::Ok;
}

void DrawStageGuideNpc(
    Gdiplus::Graphics& graphics,
    const StageGuideNpcState& state,
    const StageGuideLayout& layout,
    const StageGuideSupportImages* supportImages,
    StageGuideCardCache* cardCache)
{
    if (!StageGuideNpcIsVisible(state)) return;
    if (graphics.GetLastStatus() != Gdiplus::Ok) return;
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    graphics.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);
    graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
    DrawCharacter(graphics, state, layout);
    if (!layout.showAbout && !layout.showExternalConfirmation && !layout.showSupport) DrawMenu(graphics, layout);
    if (cardCache != nullptr && cardCache->Draw(graphics, state, layout, supportImages)) return;
    DrawAboutCard(graphics, layout);
    DrawConfirmationCard(graphics, layout);
    DrawSupportCard(graphics, state, layout, supportImages);
}

} // namespace besktop
