#include "besktop/animation/icon_fight_scene.h"

#include "besktop/logging/logger.h"

#include <objidl.h>
#include <gdiplus.h>

#include <algorithm>
#include <cmath>
#include <iterator>
#include <string>

namespace {

constexpr double kPi = 3.14159265358979323846;

double Clamp01(double value)
{
    return std::clamp(value, 0.0, 1.0);
}

double SmoothStep(double edge0, double edge1, double value)
{
    if (edge0 == edge1) {
        return value >= edge1 ? 1.0 : 0.0;
    }

    const double t = Clamp01((value - edge0) / (edge1 - edge0));
    return t * t * (3.0 - 2.0 * t);
}

double Pulse(double value, double start, double end)
{
    const double t = SmoothStep(start, end, value);
    if (t <= 0.0 || t >= 1.0) {
        return 0.0;
    }
    return std::sin(t * kPi);
}

float ToFloat(double value)
{
    return static_cast<float>(value);
}

Gdiplus::Color WithAlpha(unsigned char alpha, unsigned char red, unsigned char green, unsigned char blue)
{
    return Gdiplus::Color(alpha, red, green, blue);
}

std::wstring FirstGlyph(const std::wstring& label)
{
    if (label.empty()) {
        return L"?";
    }
    return label.substr(0, 1);
}

void DrawCenteredString(
    Gdiplus::Graphics& graphics,
    const std::wstring& text,
    const Gdiplus::RectF& rect,
    const Gdiplus::Font& font,
    const Gdiplus::Color& color)
{
    Gdiplus::StringFormat format;
    format.SetAlignment(Gdiplus::StringAlignmentCenter);
    format.SetLineAlignment(Gdiplus::StringAlignmentCenter);
    Gdiplus::SolidBrush brush(color);
    graphics.DrawString(text.c_str(), -1, &font, rect, &format, &brush);
}

struct ActorPose {
    double x = 0.0;
    double y = 0.0;
    double bob = 0.0;
    double bodyRotation = 0.0;
    double bodyScaleX = 1.0;
    double bodyScaleY = 1.0;
    double facing = 1.0;
    double punch = 0.0;
    double kick = 0.0;
    double dodge = 0.0;
    double hit = 0.0;
    double limbGrow = 0.0;
    double labelAlpha = 1.0;
    bool attackingRight = true;
};

ActorPose BuildPose(const besktop::IconFightScene::IconActor& actor, double elapsedSeconds)
{
    ActorPose pose;
    pose.x = actor.baseX;
    pose.y = actor.baseY;
    pose.limbGrow = SmoothStep(0.85, 1.75, elapsedSeconds);
    pose.labelAlpha = 1.0 - SmoothStep(0.55, 1.45, elapsedSeconds);
    pose.bodyScaleX = 1.0 - (0.10 * std::sin((elapsedSeconds * 4.0) + actor.role));
    pose.bodyScaleY = 1.0 + (0.04 * std::sin((elapsedSeconds * 5.0) + actor.role));
    pose.bodyRotation = std::sin((elapsedSeconds * 3.0) + actor.role) * 4.0;
    pose.bob = std::sin((elapsedSeconds * 6.0) + actor.role) * 3.0;

    if (elapsedSeconds < 1.75) {
        return pose;
    }

    const double fightTime = elapsedSeconds - 1.75;
    const double loop = std::fmod(fightTime, 5.2);
    const double gather = SmoothStep(0.0, 1.18, fightTime);
    pose.x = actor.baseX + ((actor.battleX - actor.baseX) * gather);
    pose.y = actor.baseY + ((actor.battleY - actor.baseY) * gather);

    if (actor.role == 0) {
        pose.facing = 1.0;
        pose.x += SmoothStep(0.0, 0.95, loop) * 34.0;
        pose.punch = Pulse(loop, 0.90, 1.55) + (0.72 * Pulse(loop, 3.30, 3.92));
        pose.hit = Pulse(loop, 2.54, 3.12);
        pose.x -= pose.hit * 42.0;
        pose.y -= pose.hit * 10.0;
        pose.bodyRotation += pose.punch * 12.0 - pose.hit * 18.0;
        pose.bodyScaleX -= pose.punch * 0.12;
        pose.bodyScaleY += pose.punch * 0.06;
        pose.attackingRight = true;
    } else if (actor.role == 1) {
        pose.facing = -1.0;
        pose.x -= SmoothStep(0.0, 0.90, loop) * 16.0;
        pose.dodge = Pulse(loop, 0.95, 1.60);
        pose.kick = Pulse(loop, 2.32, 3.12);
        pose.hit = Pulse(loop, 3.42, 4.05);
        pose.x += pose.dodge * 44.0;
        pose.y -= pose.dodge * 24.0;
        pose.x -= pose.kick * 18.0;
        pose.x += pose.hit * 30.0;
        pose.bodyRotation += -pose.kick * 18.0 + pose.dodge * 14.0 + pose.hit * 18.0;
        pose.bodyScaleX -= pose.kick * 0.18;
        pose.bodyScaleY += pose.kick * 0.05;
        pose.attackingRight = false;
    } else {
        pose.facing = -1.0;
        pose.x += std::sin(fightTime * 1.45 + actor.role) * 24.0;
        pose.y += std::sin(fightTime * 2.20 + actor.role) * 8.0;
        const double join = SmoothStep(3.05, 4.45, loop);
        pose.x -= join * 44.0;
        pose.punch = Pulse(loop, 4.02, 4.70);
        pose.hit = Pulse(loop, 1.45, 1.92) * 0.65;
        pose.bodyRotation += pose.punch * -14.0 + pose.hit * 12.0;
        pose.bodyScaleY += pose.punch * 0.08;
        pose.attackingRight = false;
    }

    pose.bob += std::sin(fightTime * 12.0 + actor.role) * 2.0;
    return pose;
}

void DrawImpact(Gdiplus::Graphics& graphics, double x, double y, double strength)
{
    if (strength <= 0.03) {
        return;
    }

    const unsigned char alpha = static_cast<unsigned char>(std::clamp(strength * 230.0, 0.0, 230.0));
    Gdiplus::Pen yellow(Gdiplus::Color(alpha, 255, 236, 86), 5.0f);
    yellow.SetStartCap(Gdiplus::LineCapRound);
    yellow.SetEndCap(Gdiplus::LineCapRound);
    Gdiplus::Pen white(Gdiplus::Color(alpha, 255, 255, 255), 3.0f);
    white.SetStartCap(Gdiplus::LineCapRound);
    white.SetEndCap(Gdiplus::LineCapRound);

    const float cx = ToFloat(x);
    const float cy = ToFloat(y);
    const float radius = ToFloat(18.0 + 20.0 * strength);
    graphics.DrawLine(&yellow, cx - radius, cy, cx + radius, cy);
    graphics.DrawLine(&yellow, cx, cy - radius, cx, cy + radius);
    graphics.DrawLine(&white, cx - radius * 0.7f, cy - radius * 0.7f, cx + radius * 0.7f, cy + radius * 0.7f);
    graphics.DrawLine(&white, cx + radius * 0.7f, cy - radius * 0.7f, cx - radius * 0.7f, cy + radius * 0.7f);
}

void DrawLimbs(Gdiplus::Graphics& graphics, const besktop::IconFightScene::IconActor& actor, const ActorPose& pose)
{
    if (pose.limbGrow <= 0.01) {
        return;
    }

    const double alphaValue = 255.0 * pose.limbGrow;
    const unsigned char alpha = static_cast<unsigned char>(std::clamp(alphaValue, 0.0, 255.0));
    Gdiplus::Pen shadow(Gdiplus::Color(static_cast<BYTE>(alpha * 0.42), 0, 0, 0), 11.0f);
    shadow.SetStartCap(Gdiplus::LineCapRound);
    shadow.SetEndCap(Gdiplus::LineCapRound);
    Gdiplus::Pen limb(Gdiplus::Color(alpha, 250, 253, 255), 7.0f);
    limb.SetStartCap(Gdiplus::LineCapRound);
    limb.SetEndCap(Gdiplus::LineCapRound);

    const double x = pose.x;
    const double y = pose.y + pose.bob;
    const double w = actor.width;
    const double h = actor.height;
    const double f = pose.facing;
    const double grow = pose.limbGrow;

    const Gdiplus::PointF armFrontStart(ToFloat(x + f * w * 0.24), ToFloat(y - h * 0.10));
    const Gdiplus::PointF armFrontEnd(
        ToFloat(x + f * (w * 0.52 + pose.punch * 88.0) * grow),
        ToFloat(y - h * 0.14 - pose.punch * 10.0 + pose.hit * 8.0));
    const Gdiplus::PointF armBackStart(ToFloat(x - f * w * 0.22), ToFloat(y - h * 0.04));
    const Gdiplus::PointF armBackEnd(
        ToFloat(x - f * (w * 0.58 + 16.0 * std::sin(pose.punch * kPi)) * grow),
        ToFloat(y + h * 0.10 + pose.dodge * 8.0));

    const Gdiplus::PointF hipFront(ToFloat(x + f * w * 0.14), ToFloat(y + h * 0.32));
    const Gdiplus::PointF footFront(
        ToFloat(x + f * (w * 0.28 + pose.kick * 108.0) * grow),
        ToFloat(y + h * 0.74 - pose.kick * 40.0));
    const Gdiplus::PointF hipBack(ToFloat(x - f * w * 0.14), ToFloat(y + h * 0.32));
    const Gdiplus::PointF footBack(
        ToFloat(x - f * (w * 0.34 + pose.hit * 24.0) * grow),
        ToFloat(y + h * 0.78 + pose.hit * 10.0));

    const Gdiplus::PointF segments[][2] = {
        {armBackStart, armBackEnd},
        {armFrontStart, armFrontEnd},
        {hipBack, footBack},
        {hipFront, footFront},
    };

    for (const auto& segment : segments) {
        graphics.DrawLine(&shadow, segment[0], segment[1]);
        graphics.DrawLine(&limb, segment[0], segment[1]);
    }

    if (pose.punch > 0.15) {
        DrawImpact(graphics, armFrontEnd.X + ToFloat(f * 18.0), armFrontEnd.Y, pose.punch);
    }
    if (pose.kick > 0.15) {
        DrawImpact(graphics, footFront.X + ToFloat(f * 20.0), footFront.Y, pose.kick);
    }
}

void DrawActorBody(Gdiplus::Graphics& graphics, const besktop::IconFightScene::IconActor& actor, const ActorPose& pose)
{
    const double centerX = pose.x;
    const double centerY = pose.y + pose.bob;
    const double width = actor.width * 0.92 * pose.bodyScaleX;
    const double height = actor.height * 0.92 * pose.bodyScaleY;
    const double halfW = width * 0.5;
    const double halfH = height * 0.5;
    const double tilt = std::sin((pose.bodyRotation / 18.0) + (pose.bodyScaleX * 2.0)) * 9.0;

    const Gdiplus::PointF shadowPoints[] = {
        {ToFloat(centerX - halfW + tilt + 6.0), ToFloat(centerY - halfH + 8.0)},
        {ToFloat(centerX + halfW + tilt + 6.0), ToFloat(centerY - halfH + 8.0)},
        {ToFloat(centerX + halfW - tilt + 6.0), ToFloat(centerY + halfH + 8.0)},
        {ToFloat(centerX - halfW - tilt + 6.0), ToFloat(centerY + halfH + 8.0)},
    };
    Gdiplus::SolidBrush shadowBrush(Gdiplus::Color(90, 0, 0, 0));
    graphics.FillPolygon(&shadowBrush, shadowPoints, static_cast<INT>(std::size(shadowPoints)));

    const Gdiplus::PointF bodyPoints[] = {
        {ToFloat(centerX - halfW + tilt), ToFloat(centerY - halfH)},
        {ToFloat(centerX + halfW + tilt), ToFloat(centerY - halfH)},
        {ToFloat(centerX + halfW - tilt), ToFloat(centerY + halfH)},
        {ToFloat(centerX - halfW - tilt), ToFloat(centerY + halfH)},
    };
    const Gdiplus::RectF bodyBounds(
        ToFloat(centerX - halfW - std::abs(tilt)),
        ToFloat(centerY - halfH),
        ToFloat(width + std::abs(tilt) * 2.0),
        ToFloat(height));
    Gdiplus::LinearGradientBrush bodyBrush(
        bodyBounds,
        WithAlpha(255, actor.red, actor.green, actor.blue),
        WithAlpha(255, static_cast<unsigned char>(std::max(20, actor.red - 34)), static_cast<unsigned char>(std::max(20, actor.green - 34)), static_cast<unsigned char>(std::max(20, actor.blue - 34))),
        Gdiplus::LinearGradientModeForwardDiagonal);
    graphics.FillPolygon(&bodyBrush, bodyPoints, static_cast<INT>(std::size(bodyPoints)));

    Gdiplus::Pen border(Gdiplus::Color(210, 255, 255, 255), 2.0f);
    graphics.DrawPolygon(&border, bodyPoints, static_cast<INT>(std::size(bodyPoints)));

    Gdiplus::SolidBrush shine(Gdiplus::Color(86, 255, 255, 255));
    graphics.FillEllipse(
        &shine,
        ToFloat(centerX - width * 0.32 + tilt * 0.55),
        ToFloat(centerY - height * 0.34),
        ToFloat(width * 0.24),
        ToFloat(height * 0.16));

    Gdiplus::FontFamily fontFamily(L"Microsoft YaHei UI");
    Gdiplus::Font font(&fontFamily, 18.0f, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
    DrawCenteredString(
        graphics,
        FirstGlyph(actor.label),
        Gdiplus::RectF(ToFloat(centerX - width * 0.35), ToFloat(centerY - height * 0.34), ToFloat(width * 0.70), ToFloat(height * 0.68)),
        font,
        Gdiplus::Color(245, 255, 255, 255));
}

void DrawActorLabel(
    Gdiplus::Graphics& graphics,
    const besktop::IconFightScene::IconActor& actor,
    const ActorPose& pose,
    double elapsedSeconds)
{
    if (pose.labelAlpha <= 0.02) {
        return;
    }

    const double shake = SmoothStep(0.25, 0.80, elapsedSeconds) * (1.0 - SmoothStep(1.05, 1.45, elapsedSeconds));
    const double jitterX = std::sin((elapsedSeconds * 66.0) + actor.role) * 5.0 * shake;
    const double jitterY = std::cos((elapsedSeconds * 73.0) + actor.role) * 3.0 * shake;
    const unsigned char alpha = static_cast<unsigned char>(std::clamp(pose.labelAlpha * 230.0, 0.0, 230.0));

    Gdiplus::FontFamily fontFamily(L"Microsoft YaHei UI");
    Gdiplus::Font font(&fontFamily, 15.0f, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
    DrawCenteredString(
        graphics,
        actor.label,
        Gdiplus::RectF(
            ToFloat(pose.x - 82.0 + jitterX),
            ToFloat(pose.y + actor.height * 0.48 + 8.0 + jitterY),
            164.0f,
            28.0f),
        font,
        Gdiplus::Color(alpha, 252, 252, 252));
}

const wchar_t* PhaseName(besktop::IconFightScene::ScenePhase phase)
{
    switch (phase) {
    case besktop::IconFightScene::ScenePhase::TextShaking:
        return L"text-shaking";
    case besktop::IconFightScene::ScenePhase::Awakening:
        return L"awakening";
    case besktop::IconFightScene::ScenePhase::Fighting:
        return L"fighting";
    case besktop::IconFightScene::ScenePhase::Calm:
    default:
        return L"calm";
    }
}

} // namespace

namespace besktop {

void IconFightScene::Reset(const DesktopSnapshot& snapshot, const RECT& clientRect)
{
    actors_.clear();
    monitorBounds_ = snapshot.monitorBounds;
    elapsedSeconds_ = 0.0;
    phase_ = ScenePhase::Calm;

    const unsigned char colors[][3] = {
        {44, 135, 255},
        {34, 185, 104},
        {245, 72, 88},
        {255, 176, 42},
        {137, 93, 255},
    };
    const double clientWidth = static_cast<double>(clientRect.right - clientRect.left);
    const double clientHeight = static_cast<double>(clientRect.bottom - clientRect.top);
    const double monitorWidth = static_cast<double>(snapshot.monitorBounds.right - snapshot.monitorBounds.left);
    const double monitorHeight = static_cast<double>(snapshot.monitorBounds.bottom - snapshot.monitorBounds.top);
    const double scaleX = monitorWidth > 0.0 ? clientWidth / monitorWidth : 1.0;
    const double scaleY = monitorHeight > 0.0 ? clientHeight / monitorHeight : 1.0;
    const double iconScale = std::max(scaleX, scaleY);
    const double battleCenterX = clientWidth * 0.50;
    const double battleCenterY = clientHeight * 0.64;
    constexpr double kBattleOffsetX[] = {-116.0, 0.0, 116.0, -214.0, 214.0};
    constexpr double kBattleOffsetY[] = {0.0, 0.0, 0.0, -72.0, 72.0};

    const size_t count = std::min<size_t>(snapshot.icons.size(), 5);
    for (size_t index = 0; index < count; ++index) {
        const DesktopIconSnapshot& icon = snapshot.icons[index];
        IconActor actor;
        actor.label = icon.displayName.empty() ? (L"Icon " + std::to_wstring(index + 1)) : icon.displayName;
        actor.baseX = (((icon.bounds.left + icon.bounds.right) * 0.5) - snapshot.monitorBounds.left) * scaleX;
        actor.baseY = (((icon.bounds.top + icon.bounds.bottom) * 0.5) - snapshot.monitorBounds.top) * scaleY;
        actor.battleX = battleCenterX + kBattleOffsetX[index % std::size(kBattleOffsetX)];
        actor.battleY = battleCenterY + kBattleOffsetY[index % std::size(kBattleOffsetY)];
        actor.width = std::max(78.0, static_cast<double>(icon.bounds.right - icon.bounds.left) * iconScale * 0.70);
        actor.height = actor.width;
        actor.role = static_cast<int>(index);
        actor.red = colors[index % std::size(colors)][0];
        actor.green = colors[index % std::size(colors)][1];
        actor.blue = colors[index % std::size(colors)][2];
        actors_.push_back(actor);
    }

    while (actors_.size() < 3) {
        const int index = static_cast<int>(actors_.size());
        IconActor actor;
        actor.label = L"Icon " + std::to_wstring(index + 1);
        actor.baseX = (clientRect.right - clientRect.left) * 0.5 + (index - 1) * 116.0;
        actor.baseY = (clientRect.bottom - clientRect.top) * 0.64;
        actor.battleX = actor.baseX;
        actor.battleY = actor.baseY;
        actor.role = index;
        actor.red = colors[index % std::size(colors)][0];
        actor.green = colors[index % std::size(colors)][1];
        actor.blue = colors[index % std::size(colors)][2];
        actors_.push_back(actor);
    }

    LogInfo(L"icon fight scene reset; actors: " + std::to_wstring(actors_.size()));
    for (const IconActor& actor : actors_) {
        LogInfo(
            L"icon actor: " + actor.label +
            L" @ " + std::to_wstring(static_cast<int>(actor.baseX)) +
            L"," + std::to_wstring(static_cast<int>(actor.baseY)));
    }
}

void IconFightScene::Update(double elapsedSeconds)
{
    elapsedSeconds_ = elapsedSeconds;
    const ScenePhase nextPhase = DeterminePhase(elapsedSeconds_);
    if (nextPhase != phase_) {
        phase_ = nextPhase;
        LogPhase(phase_);
    }
}

void IconFightScene::Render(HDC hdc, const RECT& clientRect) const
{
    Gdiplus::Graphics graphics(hdc);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    graphics.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);

    const double width = clientRect.right - clientRect.left;
    const double height = clientRect.bottom - clientRect.top;

    const double bannerAlpha =
        elapsedSeconds_ < 3.2 ? (SmoothStep(0.10, 0.55, elapsedSeconds_) * (1.0 - SmoothStep(2.25, 3.20, elapsedSeconds_))) : 0.0;
    if (bannerAlpha > 0.01) {
        Gdiplus::FontFamily fontFamily(L"Microsoft YaHei UI");
        Gdiplus::Font bannerFont(&fontFamily, 30.0f, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
        const unsigned char alpha = static_cast<unsigned char>(std::clamp(bannerAlpha * 230.0, 0.0, 230.0));
        DrawCenteredString(
            graphics,
            L"\u68c0\u6d4b\u5230\u684c\u9762\u56fe\u6807\u60c5\u7eea\u5f02\u5e38...",
            Gdiplus::RectF(ToFloat(width * 0.14), ToFloat(height * 0.15), ToFloat(width * 0.72), 48.0f),
            bannerFont,
            Gdiplus::Color(alpha, 255, 255, 255));
    }

    for (const IconActor& actor : actors_) {
        const ActorPose pose = BuildPose(actor, elapsedSeconds_);
        DrawLimbs(graphics, actor, pose);
    }

    for (const IconActor& actor : actors_) {
        const ActorPose pose = BuildPose(actor, elapsedSeconds_);
        DrawActorBody(graphics, actor, pose);
        DrawActorLabel(graphics, actor, pose, elapsedSeconds_);
    }

    Gdiplus::FontFamily fontFamily(L"Microsoft YaHei UI");
    Gdiplus::Font hintFont(&fontFamily, 13.0f, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
    DrawCenteredString(
        graphics,
        L"Esc",
        Gdiplus::RectF(ToFloat(width - 74.0), ToFloat(height - 42.0), 52.0f, 22.0f),
        hintFont,
        Gdiplus::Color(105, 255, 255, 255));
}

IconFightScene::ScenePhase IconFightScene::DeterminePhase(double elapsedSeconds) const
{
    if (elapsedSeconds < 0.30) {
        return ScenePhase::Calm;
    }
    if (elapsedSeconds < 0.90) {
        return ScenePhase::TextShaking;
    }
    if (elapsedSeconds < 1.75) {
        return ScenePhase::Awakening;
    }
    return ScenePhase::Fighting;
}

void IconFightScene::LogPhase(ScenePhase phase)
{
    LogInfo(std::wstring(L"icon fight scene phase: ") + PhaseName(phase));
}

} // namespace besktop
