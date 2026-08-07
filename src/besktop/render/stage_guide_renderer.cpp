#include "besktop/render/stage_guide_renderer.h"

#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>

#include <algorithm>
#include <cmath>
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
    const double size = state.bodySize;
    const double gait = state.moving ? std::sin(state.motionPhase * 2.0 * kPi) : 0.0;
    const double bob = state.moving ? -std::abs(std::sin(state.motionPhase * 2.0 * kPi)) * size * 0.025 : 0.0;
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

    const double centerX = state.position.x;
    const double centerY = state.position.y + bob + leaveProgress * size * 0.08;
    const double shoulderY = centerY + size * 0.04;
    const double hipY = centerY + size * 0.44;
    const double armSwing = gait * size * 0.13;
    const double wave = attention * (0.45 + 0.18 * std::sin(state.startupElapsedSeconds * 7.0));

    const Gdiplus::PointF leftShoulder(ToFloat(centerX - size * 0.40), ToFloat(shoulderY));
    const Gdiplus::PointF leftElbow(
        ToFloat(centerX - size * (0.60 + wave * 0.10)),
        ToFloat(shoulderY + size * (0.28 + armSwing / size)));
    const Gdiplus::PointF leftHand(
        ToFloat(centerX - size * (0.72 + wave * 0.08)),
        ToFloat(shoulderY + size * (0.53 + armSwing / size)));
    const Gdiplus::PointF rightShoulder(ToFloat(centerX + size * 0.40), ToFloat(shoulderY));
    const Gdiplus::PointF rightElbow(
        ToFloat(centerX + size * (0.56 + wave * 0.12)),
        ToFloat(shoulderY + size * (attention > 0.0 ? -0.18 : 0.30 - armSwing / size)));
    const Gdiplus::PointF rightHand(
        ToFloat(centerX + size * (attention > 0.0 ? 0.48 : 0.70)),
        ToFloat(shoulderY + size * (attention > 0.0 ? -0.52 - wave * 0.08 : 0.55 - armSwing / size)));

    const Gdiplus::PointF leftHip(ToFloat(centerX - size * 0.18), ToFloat(hipY));
    const Gdiplus::PointF leftKnee(
        ToFloat(centerX - size * 0.22 + gait * size * 0.12),
        ToFloat(hipY + size * 0.38));
    const Gdiplus::PointF leftFoot(
        ToFloat(centerX - size * 0.25 + gait * size * 0.20),
        ToFloat(hipY + size * 0.78 - std::max(0.0, gait) * size * 0.09));
    const Gdiplus::PointF rightHip(ToFloat(centerX + size * 0.18), ToFloat(hipY));
    const Gdiplus::PointF rightKnee(
        ToFloat(centerX + size * 0.22 - gait * size * 0.12),
        ToFloat(hipY + size * 0.38));
    const Gdiplus::PointF rightFoot(
        ToFloat(centerX + size * 0.25 - gait * size * 0.20),
        ToFloat(hipY + size * 0.78 - std::max(0.0, -gait) * size * 0.09));

    const auto offset = [](const Gdiplus::PointF& point) {
        return Gdiplus::PointF(point.X + 2.2f, point.Y + 2.8f);
    };
    DrawChain(graphics, shadow, offset(leftShoulder), offset(leftElbow), offset(leftHand));
    DrawChain(graphics, shadow, offset(leftHip), offset(leftKnee), offset(leftFoot));
    DrawChain(graphics, shadow, offset(rightHip), offset(rightKnee), offset(rightFoot));
    DrawChain(graphics, limb, leftShoulder, leftElbow, leftHand);
    DrawChain(graphics, limb, leftHip, leftKnee, leftFoot);
    DrawChain(graphics, limb, rightHip, rightKnee, rightFoot);

    Gdiplus::RectF body = ToRect(layout.bodyRect);
    body.Y += ToFloat(bob + leaveProgress * size * 0.08);
    Gdiplus::GraphicsPath bodyPath;
    AddRoundedRectPath(bodyPath, body, ToFloat(size * 0.20));
    Gdiplus::SolidBrush bodyShadow(Gdiplus::Color(Alpha(characterAlpha * 0.28), 0, 0, 0));
    Gdiplus::GraphicsPath shadowPath;
    AddRoundedRectPath(
        shadowPath,
        Gdiplus::RectF(body.X + 3.0f, body.Y + 4.0f, body.Width, body.Height),
        ToFloat(size * 0.20));
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
        ToFloat(size * 0.67),
        Gdiplus::FontStyleBold,
        Gdiplus::UnitPixel);
    DrawCenteredText(
        graphics,
        L"B",
        Gdiplus::RectF(body.X, body.Y - ToFloat(size * 0.035), body.Width, body.Height),
        logoFont,
        Gdiplus::Color(characterAlpha, 255, 255, 255));

    if (state.pointerPresent && attention > 0.0) {
        const double dx = std::clamp(
            (state.pointerPosition.x - state.position.x) / std::max(1.0, size * 2.2),
            -1.0,
            1.0);
        const double dy = std::clamp(
            (state.pointerPosition.y - state.position.y) / std::max(1.0, size * 2.2),
            -1.0,
            1.0);
        Gdiplus::SolidBrush eye(Gdiplus::Color(Alpha(characterAlpha * 0.82), 6, 60, 122));
        const float eyeSize = ToFloat(std::max(2.6, size * 0.055));
        graphics.FillEllipse(&eye,
            ToFloat(centerX - size * 0.055 + dx * size * 0.025) - eyeSize * 0.5f,
            ToFloat(centerY - size * 0.15 + dy * size * 0.020) - eyeSize * 0.5f,
            eyeSize, eyeSize);
        graphics.FillEllipse(&eye,
            ToFloat(centerX + size * 0.12 + dx * size * 0.025) - eyeSize * 0.5f,
            ToFloat(centerY + size * 0.06 + dy * size * 0.020) - eyeSize * 0.5f,
            eyeSize, eyeSize);
    }

    DrawChain(graphics, shadow, offset(rightShoulder), offset(rightElbow), offset(rightHand));
    DrawChain(graphics, limb, rightShoulder, rightElbow, rightHand);
}

std::wstring MenuLabel(besktop::StageGuideMenuEntry entry)
{
    switch (entry) {
    case besktop::StageGuideMenuEntry::Feedback: return L"反馈";
    case besktop::StageGuideMenuEntry::Support: return L"支持 B仔";
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
    Gdiplus::SolidBrush brush(Gdiplus::Color(alpha, 38, 151, 239));
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
        graphics.FillPath(&brush, &heart);
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

void DrawCardBackground(
    Gdiplus::Graphics& graphics,
    const besktop::StageGuideRect& cardRect,
    double scale)
{
    const Gdiplus::RectF rect = ToRect(cardRect);
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
        Gdiplus::Color(246, 22, 74, 129),
        Gdiplus::Color(246, 12, 42, 82),
        Gdiplus::LinearGradientModeVertical);
    graphics.FillPath(&background, &path);
    Gdiplus::Pen border(Gdiplus::Color(190, 126, 211, 255), 1.5f);
    graphics.DrawPath(&border, &path);
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

void DrawAboutCard(
    Gdiplus::Graphics& graphics,
    const besktop::StageGuideLayout& layout)
{
    if (!layout.showAbout) return;
    DrawCardBackground(graphics, layout.cardRect, layout.dpiScale);
    DrawCloseButton(graphics, layout.closeCardRect, layout.dpiScale);
    const Gdiplus::RectF card = ToRect(layout.cardRect);
    Gdiplus::FontFamily family(L"Microsoft YaHei UI");
    Gdiplus::Font titleFont(&family, ToFloat(21.0 * layout.dpiScale),
        Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
    Gdiplus::Font bodyFont(&family, ToFloat(14.0 * layout.dpiScale),
        Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
    DrawText(graphics, L"这是 Besktop",
        Gdiplus::RectF(card.X + ToFloat(24.0 * layout.dpiScale),
            card.Y + ToFloat(20.0 * layout.dpiScale),
            card.Width - ToFloat(70.0 * layout.dpiScale),
            ToFloat(34.0 * layout.dpiScale)),
        titleFont, Gdiplus::Color(255, 255, 255, 255));
    const std::wstring body =
        L"桌面娱乐演出，让真实桌面图标在安全舞台中醒来。\n\n"
        L"• 不移动、删除或改写真实桌面文件\n"
        L"• 不自启动、不后台驻留、不提权\n"
        L"• P 切换自动互动，Esc 退出\n\n"
        L"公开项目：BensonLaur/Besktop";
    DrawText(graphics, body,
        Gdiplus::RectF(card.X + ToFloat(24.0 * layout.dpiScale),
            card.Y + ToFloat(66.0 * layout.dpiScale),
            card.Width - ToFloat(48.0 * layout.dpiScale),
            card.Height - ToFloat(82.0 * layout.dpiScale)),
        bodyFont, Gdiplus::Color(238, 229, 244, 255));
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
    DrawCardBackground(graphics, layout.cardRect, layout.dpiScale);
    DrawCloseButton(graphics, layout.closeCardRect, layout.dpiScale);
    const Gdiplus::RectF card = ToRect(layout.cardRect);
    Gdiplus::FontFamily family(L"Microsoft YaHei UI");
    Gdiplus::Font titleFont(&family, ToFloat(18.0 * layout.dpiScale),
        Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
    Gdiplus::Font bodyFont(&family, ToFloat(13.0 * layout.dpiScale),
        Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
    const std::wstring actionName = layout.confirmingEntry == besktop::StageGuideMenuEntry::Feedback ?
        L"反馈" : L"支持 B仔";
    DrawText(graphics, actionName,
        Gdiplus::RectF(card.X + ToFloat(22.0 * layout.dpiScale),
            card.Y + ToFloat(18.0 * layout.dpiScale), card.Width - 80.0f,
            ToFloat(30.0 * layout.dpiScale)),
        titleFont, Gdiplus::Color(255, 255, 255, 255));
    DrawText(graphics, L"将结束本次演出并打开官方网页。",
        Gdiplus::RectF(card.X + ToFloat(22.0 * layout.dpiScale),
            card.Y + ToFloat(66.0 * layout.dpiScale),
            card.Width - ToFloat(44.0 * layout.dpiScale),
            ToFloat(46.0 * layout.dpiScale)),
        bodyFont, Gdiplus::Color(235, 226, 243, 255));
    DrawButton(graphics, layout.continueButtonRect, L"继续观看", false,
        layout.dpiScale, bodyFont);
    DrawButton(graphics, layout.confirmButtonRect, L"打开网页", true,
        layout.dpiScale, bodyFont);
}

} // namespace

namespace besktop {

void DrawStageGuideNpc(
    Gdiplus::Graphics& graphics,
    const StageGuideNpcState& state,
    const StageGuideLayout& layout)
{
    if (!StageGuideNpcIsVisible(state)) return;
    if (graphics.GetLastStatus() != Gdiplus::Ok) return;
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    graphics.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);
    graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
    DrawCharacter(graphics, state, layout);
    DrawMenu(graphics, layout);
    DrawAboutCard(graphics, layout);
    DrawConfirmationCard(graphics, layout);
}

} // namespace besktop
