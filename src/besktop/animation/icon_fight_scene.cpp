#include "besktop/animation/icon_fight_scene.h"

#include "besktop/animation/gait_ik.h"
#include "besktop/app/runtime_options.h"
#include "besktop/logging/logger.h"

#include <objidl.h>
#include <gdiplus.h>

#include <algorithm>
#include <cmath>
#include <iterator>
#include <string>

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kAwakeningDurationSeconds = 0.48;
constexpr double kLimbGrowthDurationSeconds = 0.72;
constexpr ULONGLONG kAutomaticInteractionToastMilliseconds = 1600;

LONGLONG PerformanceCounterNow()
{
    LARGE_INTEGER value{};
    QueryPerformanceCounter(&value);
    return value.QuadPart;
}

double CounterMilliseconds(LONGLONG start, LONGLONG end)
{
    static const double countsPerMillisecond = [] {
        LARGE_INTEGER frequency{};
        QueryPerformanceFrequency(&frequency);
        return static_cast<double>(frequency.QuadPart) / 1000.0;
    }();
    return countsPerMillisecond > 0.0 ? static_cast<double>(end - start) / countsPerMillisecond : 0.0;
}

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

double DegreesToRadians(double degrees)
{
    return degrees * kPi / 180.0;
}

bool RenderShadowsEnabled()
{
    return besktop::GetRuntimeOptions().renderShadowsEnabled;
}

bool DebugIconPlaneEnabled()
{
    return besktop::GetRuntimeOptions().debugIconPlaneEnabled;
}

double LerpValue(double from, double to, double t);

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

struct DesktopLabelFontSpec {
    std::wstring family = L"Microsoft YaHei UI";
    float pixelSize = 14.0f;
    bool fromSystem = false;
};

const DesktopLabelFontSpec& DesktopIconLabelFontSpec()
{
    static const DesktopLabelFontSpec spec = [] {
        LOGFONTW logFont{};
        if (SystemParametersInfoW(SPI_GETICONTITLELOGFONT, sizeof(logFont), &logFont, 0) &&
            logFont.lfFaceName[0] != L'\0') {
            DesktopLabelFontSpec value;
            value.family = logFont.lfFaceName;
            const float systemPixelSize = static_cast<float>(std::abs(logFont.lfHeight));
            value.pixelSize = std::clamp(systemPixelSize * 1.16f, 14.0f, 22.0f);
            value.fromSystem = true;
            return value;
        }
        return DesktopLabelFontSpec{};
    }();
    return spec;
}

std::wstring DesktopIconLabelFontDescription()
{
    const DesktopLabelFontSpec& spec = DesktopIconLabelFontSpec();
    return spec.family +
        L" " +
        std::to_wstring(static_cast<int>(spec.pixelSize)) +
        L"px" +
        (spec.fromSystem ? L" (SPI_GETICONTITLELOGFONT)" : L" (fallback)");
}

void DrawDesktopIconLabel(
    Gdiplus::Graphics& graphics,
    const std::wstring& text,
    const Gdiplus::RectF& rect,
    double alpha)
{
    if (text.empty() || rect.Width <= 1.0f || rect.Height <= 1.0f || alpha <= 0.01) {
        return;
    }

    const DesktopLabelFontSpec& fontSpec = DesktopIconLabelFontSpec();
    Gdiplus::FontFamily fontFamily(fontSpec.family.c_str());
    Gdiplus::FontFamily fallbackFamily(L"Microsoft YaHei UI");
    const Gdiplus::FontFamily* selectedFamily = fontFamily.IsAvailable() ? &fontFamily : &fallbackFamily;
    Gdiplus::Font font(selectedFamily, fontSpec.pixelSize, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);

    Gdiplus::StringFormat format;
    format.SetAlignment(Gdiplus::StringAlignmentCenter);
    format.SetLineAlignment(Gdiplus::StringAlignmentNear);
    format.SetFormatFlags(Gdiplus::StringFormatFlagsLineLimit);
    format.SetTrimming(Gdiplus::StringTrimmingEllipsisCharacter);

    const BYTE textAlpha = static_cast<BYTE>(std::clamp(alpha * 255.0, 0.0, 255.0));
    const BYTE outlineAlpha = static_cast<BYTE>(std::clamp(alpha * 255.0, 0.0, 255.0));
    Gdiplus::SolidBrush outlineBrush(Gdiplus::Color(outlineAlpha, 0, 0, 0));
    Gdiplus::SolidBrush textBrush(Gdiplus::Color(textAlpha, 255, 255, 255));

    constexpr float kOutlineOffset = 1.15f;
    Gdiplus::RectF labelRect = rect;
    if (text.size() <= 3 && labelRect.Width < 72.0f) {
        const float centerX = labelRect.X + (labelRect.Width * 0.5f);
        labelRect.Width = 72.0f;
        labelRect.X = centerX - (labelRect.Width * 0.5f);
    }
    const Gdiplus::RectF adjustedRect(labelRect.X, labelRect.Y - 2.0f, labelRect.Width, labelRect.Height + 3.0f);
    const Gdiplus::RectF outlineRects[] = {
        Gdiplus::RectF(adjustedRect.X - kOutlineOffset, adjustedRect.Y, adjustedRect.Width, adjustedRect.Height),
        Gdiplus::RectF(adjustedRect.X + kOutlineOffset, adjustedRect.Y, adjustedRect.Width, adjustedRect.Height),
        Gdiplus::RectF(adjustedRect.X, adjustedRect.Y - kOutlineOffset, adjustedRect.Width, adjustedRect.Height),
        Gdiplus::RectF(adjustedRect.X, adjustedRect.Y + kOutlineOffset, adjustedRect.Width, adjustedRect.Height),
        Gdiplus::RectF(adjustedRect.X - kOutlineOffset, adjustedRect.Y - kOutlineOffset, adjustedRect.Width, adjustedRect.Height),
        Gdiplus::RectF(adjustedRect.X + kOutlineOffset, adjustedRect.Y - kOutlineOffset, adjustedRect.Width, adjustedRect.Height),
        Gdiplus::RectF(adjustedRect.X - kOutlineOffset, adjustedRect.Y + kOutlineOffset, adjustedRect.Width, adjustedRect.Height),
        Gdiplus::RectF(adjustedRect.X + kOutlineOffset, adjustedRect.Y + kOutlineOffset, adjustedRect.Width, adjustedRect.Height),
    };
    for (const Gdiplus::RectF& outlineRect : outlineRects) {
        graphics.DrawString(text.c_str(), -1, &font, outlineRect, &format, &outlineBrush);
    }
    graphics.DrawString(text.c_str(), -1, &font, adjustedRect, &format, &textBrush);
}

struct CachedLabelBitmap {
    std::unique_ptr<Gdiplus::Bitmap> bitmap;
    float offsetX = 0.0f;
    float offsetY = 0.0f;
};

CachedLabelBitmap BuildCachedLabelBitmap(const besktop::IconFightScene::IconActor& actor)
{
    CachedLabelBitmap cached;
    const float originalWidth = static_cast<float>(actor.labelBounds.right - actor.labelBounds.left);
    const float originalHeight = static_cast<float>(actor.labelBounds.bottom - actor.labelBounds.top);
    if (actor.label.empty() || originalWidth <= 1.0f || originalHeight <= 1.0f) {
        return cached;
    }

    const float bitmapWidth = actor.label.size() <= 3 ? std::max(72.0f, originalWidth) : originalWidth;
    const float bitmapHeight = originalHeight + 5.0f;
    auto bitmap = std::make_unique<Gdiplus::Bitmap>(
        static_cast<INT>(std::ceil(bitmapWidth)),
        static_cast<INT>(std::ceil(bitmapHeight)),
        PixelFormat32bppARGB);
    if (bitmap->GetLastStatus() != Gdiplus::Ok) {
        return cached;
    }

    Gdiplus::Graphics graphics(bitmap.get());
    if (graphics.GetLastStatus() != Gdiplus::Ok) {
        return cached;
    }
    graphics.Clear(Gdiplus::Color(0, 0, 0, 0));
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    graphics.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);
    const float centeredX = (bitmapWidth - originalWidth) * 0.5f;
    DrawDesktopIconLabel(
        graphics,
        actor.label,
        Gdiplus::RectF(centeredX, 2.0f, originalWidth, originalHeight),
        1.0);
    cached.offsetX = -centeredX;
    cached.offsetY = -2.0f;
    cached.bitmap = std::move(bitmap);
    return cached;
}

using ActorPose = besktop::IconFightScene::ActorPose;

enum class LimbLayer {
    Back,
    Front,
};

ActorPose BuildLegacyPose(const besktop::IconFightScene::IconActor& actor, double elapsedSeconds)
{
    constexpr double kWalkStartSeconds = 1.80;
    constexpr double kWalkDurationSeconds = 2.20;
    constexpr double kTurnDurationSeconds = 0.48;

    ActorPose pose;
    pose.x = actor.baseX;
    pose.y = actor.baseY;
    pose.bodyEffect = SmoothStep(0.62, 1.68, elapsedSeconds);
    pose.limbGrow = SmoothStep(1.05, 1.75, elapsedSeconds);
    pose.labelAlpha = 1.0 - SmoothStep(1.18, 1.86, elapsedSeconds);
    pose.walkPhase = std::fmod(std::max(0.0, elapsedSeconds - kWalkStartSeconds) * 1.75 + actor.role * 0.17, 1.0);
    pose.locomotionWeight = SmoothStep(0.0, 0.55, std::max(0.0, elapsedSeconds - kWalkStartSeconds));
    pose.gait = std::sin((pose.walkPhase * 2.0 * kPi));
    pose.heading = actor.role % 2 == 0 ? ActorPose::Heading::MoveRight : ActorPose::Heading::MoveLeft;
    pose.facing = pose.heading == ActorPose::Heading::MoveLeft ? -1.0 : 1.0;
    pose.rotateX = DegreesToRadians(std::sin((elapsedSeconds * 4.2) + actor.role) * 3.0 * pose.bodyEffect);
    pose.rotateY = DegreesToRadians(std::sin((elapsedSeconds * 2.6) + actor.role) * 3.0 * pose.bodyEffect);
    pose.rotateZ = DegreesToRadians(std::sin((elapsedSeconds * 3.0) + actor.role) * 2.0 * pose.bodyEffect);
    pose.bob = std::sin((elapsedSeconds * 6.0) + actor.role) * 1.6 * pose.bodyEffect;

    if (elapsedSeconds < kWalkStartSeconds) {
        return pose;
    }

    const double walkTime = elapsedSeconds - kWalkStartSeconds;
    const double cycleLength = (kWalkDurationSeconds * 2.0) + (kTurnDurationSeconds * 2.0);
    const double loop = std::fmod(walkTime, cycleLength);
    const double intro = SmoothStep(0.0, 0.55, walkTime);
    const double centerX = actor.battleX;
    const double centerY = actor.battleY;
    const double planeSide = std::max(24.0, std::max(actor.planeWidth, actor.planeHeight));
    const double patrolRadius = std::clamp(planeSide * 0.92, 70.0, 118.0);

    double patrolX = centerX;
    double rotateYDegrees = 0.0;
    double headingSign = 1.0;
    if (loop < kWalkDurationSeconds) {
        const double t = loop / kWalkDurationSeconds;
        headingSign = 1.0;
        pose.facing = 1.0;
        pose.heading = ActorPose::Heading::MoveRight;
        patrolX = centerX + LerpValue(-patrolRadius, patrolRadius, t);
        rotateYDegrees = 5.0 + std::sin(t * kPi * 2.0) * 3.0;
        pose.attackingRight = true;
    } else if (loop < kWalkDurationSeconds + kTurnDurationSeconds) {
        const double t = (loop - kWalkDurationSeconds) / kTurnDurationSeconds;
        headingSign = t < 0.5 ? 1.0 : -1.0;
        pose.facing = headingSign;
        pose.heading = ActorPose::Heading::FacingOut;
        patrolX = centerX + patrolRadius;
        rotateYDegrees = LerpValue(6.0, -6.0, t) + std::sin(t * kPi) * 86.0;
        pose.attackingRight = false;
    } else if (loop < (kWalkDurationSeconds * 2.0) + kTurnDurationSeconds) {
        const double t = (loop - kWalkDurationSeconds - kTurnDurationSeconds) / kWalkDurationSeconds;
        headingSign = -1.0;
        pose.facing = -1.0;
        pose.heading = ActorPose::Heading::MoveLeft;
        patrolX = centerX + LerpValue(patrolRadius, -patrolRadius, t);
        rotateYDegrees = -5.0 + std::sin(t * kPi * 2.0) * 3.0;
        pose.attackingRight = false;
    } else {
        const double t = (loop - (kWalkDurationSeconds * 2.0) - kTurnDurationSeconds) / kTurnDurationSeconds;
        headingSign = t < 0.5 ? -1.0 : 1.0;
        pose.facing = headingSign;
        pose.heading = ActorPose::Heading::FacingOut;
        patrolX = centerX - patrolRadius;
        rotateYDegrees = LerpValue(-6.0, 6.0, t) - std::sin(t * kPi) * 86.0;
        pose.attackingRight = false;
    }

    const besktop::GaitGeometry gaitGeometry = besktop::BuildGaitGeometry(planeSide, planeSide * 0.40, planeSide * 0.41);
    const besktop::GaitLegSample rightStep = besktop::SampleGaitLeg(pose.walkPhase, 0.0, gaitGeometry, 1.0);
    const besktop::GaitLegSample leftStep = besktop::SampleGaitLeg(pose.walkPhase, 0.5, gaitGeometry, 1.0);
    const double doublePlant = std::clamp(rightStep.plantWeight + leftStep.plantWeight - 1.0, 0.0, 1.0);
    const double swingLift = gaitGeometry.stepHeight > 0.0 ?
        std::max(rightStep.footLift, leftStep.footLift) / gaitGeometry.stepHeight :
        0.0;
    const double bobAmplitude = std::clamp(planeSide * 0.035, 2.0, 4.5);
    pose.x = LerpValue(actor.baseX, patrolX, intro);
    pose.y = LerpValue(actor.baseY, centerY, intro);
    pose.bob = ((doublePlant * 0.70) - (swingLift * 0.85)) * bobAmplitude * pose.limbGrow;
    pose.rotateX += DegreesToRadians(std::cos(pose.walkPhase * 2.0 * kPi) * 2.4 * pose.limbGrow);
    pose.rotateY += DegreesToRadians(rotateYDegrees * pose.limbGrow);
    pose.rotateZ += DegreesToRadians(headingSign * std::sin(pose.walkPhase * 2.0 * kPi) * 2.8 * pose.limbGrow);
    return pose;
}

ActorPose BuildPose(const besktop::IconFightScene::IconActor& actor, double elapsedSeconds)
{
    const double actorTime = std::max(0.0, elapsedSeconds - actor.awakeningStartSeconds);
    const bool wandering = actorTime >= kAwakeningDurationSeconds + kLimbGrowthDurationSeconds;

    ActorPose pose;
    pose.x = actor.x;
    pose.y = actor.y;
    pose.bodyEffect = SmoothStep(0.0, kAwakeningDurationSeconds, actorTime);
    pose.limbGrow = SmoothStep(
        kAwakeningDurationSeconds * 0.45,
        kAwakeningDurationSeconds + kLimbGrowthDurationSeconds,
        actorTime);
    pose.labelAlpha = 1.0 - SmoothStep(
        kAwakeningDurationSeconds * 0.55,
        kAwakeningDurationSeconds + kLimbGrowthDurationSeconds,
        actorTime);
    pose.walkPhase = actor.walkPhase;
    pose.locomotionWeight = actor.locomotionWeight;
    pose.gait = std::sin(pose.walkPhase * 2.0 * kPi) * pose.locomotionWeight;
    pose.facing = actor.turnMotion.currentFacing == besktop::TurnFacing::Left ? -1.0 : 1.0;
    pose.heading = actor.turnMotion.turning ?
        ActorPose::Heading::FacingOut :
        (pose.facing < 0.0 ? ActorPose::Heading::MoveLeft : ActorPose::Heading::MoveRight);
    pose.attackingRight = pose.facing > 0.0;
    const double turnStabilityWeight = actor.turnMotion.turning && actor.turnMotion.progress > 0.0 ?
        0.0 : actor.turnPoseWeight;
    pose.rotateX = DegreesToRadians(
        std::sin((actorTime * 4.2) + actor.role) * 3.0 * pose.bodyEffect * turnStabilityWeight);
    const double turnYaw = besktop::SampleTurnYaw(actor.turnMotion);
    pose.rotateY = turnYaw + DegreesToRadians(
        std::cos(turnYaw) * 5.0 * pose.limbGrow * turnStabilityWeight);
    pose.rotateZ = DegreesToRadians(
        std::sin((actorTime * 3.0) + actor.role) * 2.0 * pose.bodyEffect * turnStabilityWeight);

    if (wandering && pose.locomotionWeight > 0.001 && !actor.actionPreviewActor) {
        const double planeSide = std::max(24.0, std::max(actor.planeWidth, actor.planeHeight));
        const besktop::GaitGeometry gaitGeometry = besktop::BuildGaitGeometry(planeSide, planeSide * 0.40, planeSide * 0.41);
        const besktop::GaitLegSample rightStep = besktop::SampleGaitLeg(
            pose.walkPhase, 0.0, gaitGeometry, pose.locomotionWeight);
        const besktop::GaitLegSample leftStep = besktop::SampleGaitLeg(
            pose.walkPhase, 0.5, gaitGeometry, pose.locomotionWeight);
        const double doublePlant = std::clamp(rightStep.plantWeight + leftStep.plantWeight - 1.0, 0.0, 1.0);
        const double swingLift = gaitGeometry.stepHeight > 0.0 ?
            std::max(rightStep.footLift, leftStep.footLift) / gaitGeometry.stepHeight :
            0.0;
        const double bobAmplitude = std::clamp(planeSide * 0.035, 2.0, 4.5);
        pose.bob = ((doublePlant * 0.70) - (swingLift * 0.85)) * bobAmplitude * pose.locomotionWeight;
        pose.rotateX += DegreesToRadians(
            std::cos(pose.walkPhase * 2.0 * kPi) * 2.4 * pose.locomotionWeight);
        pose.rotateZ += DegreesToRadians(
            pose.facing * std::sin(pose.walkPhase * 2.0 * kPi) * 2.8 * pose.locomotionWeight);
    }
    pose.action = actor.actionSample;
    const double planeSide = std::max(24.0, std::max(actor.planeWidth, actor.planeHeight));
    pose.x += pose.facing * pose.action.rootOffsetForward * planeSide;
    pose.x += pose.action.rootOffsetLateral * planeSide;
    pose.y += pose.action.rootOffsetY * planeSide;
    pose.rotateX += pose.action.bodyRotateX;
    pose.rotateY += pose.action.bodyRotateY;
    pose.rotateZ += pose.action.bodyRotateZ;
    pose.x += pose.facing * actor.encounterPose.rootOffsetForward * planeSide;
    pose.y += actor.encounterPose.rootOffsetY * planeSide;
    pose.rotateX += actor.encounterPose.bodyRotateX;
    pose.rotateY += actor.encounterPose.bodyRotateY;
    pose.rotateZ += actor.encounterPose.bodyRotateZ;
    pose.punch = pose.action.punchStrength;
    pose.kick = pose.action.kickStrength;
    pose.dodge = pose.action.dodgeStrength;
    pose.hit = pose.action.hitStrength;
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

void DrawParryImpact(Gdiplus::Graphics& graphics, double x, double y, double strength)
{
    if (strength <= 0.03) {
        return;
    }
    const unsigned char alpha = static_cast<unsigned char>(
        std::clamp(strength * 220.0, 0.0, 220.0));
    Gdiplus::Pen cyan(Gdiplus::Color(alpha, 90, 225, 255), 3.5f);
    cyan.SetStartCap(Gdiplus::LineCapRound);
    cyan.SetEndCap(Gdiplus::LineCapRound);
    Gdiplus::Pen white(Gdiplus::Color(alpha, 245, 255, 255), 2.0f);
    white.SetStartCap(Gdiplus::LineCapRound);
    white.SetEndCap(Gdiplus::LineCapRound);
    const float cx = ToFloat(x);
    const float cy = ToFloat(y);
    const float radius = ToFloat(8.0 + 9.0 * strength);
    graphics.DrawArc(&cyan, cx - radius, cy - radius, radius * 2.0f, radius * 2.0f, 205.0f, 110.0f);
    graphics.DrawLine(&white, cx - radius * 0.25f, cy - radius * 0.55f,
        cx + radius * 0.18f, cy - radius * 1.05f);
    graphics.DrawLine(&white, cx + radius * 0.30f, cy - radius * 0.20f,
        cx + radius * 0.92f, cy - radius * 0.42f);
    graphics.DrawLine(&cyan, cx + radius * 0.18f, cy + radius * 0.18f,
        cx + radius * 0.70f, cy + radius * 0.52f);
}

void DrawBodyShadow(
    Gdiplus::Graphics& graphics,
    const Gdiplus::PointF* shadowPoints,
    size_t pointCount,
    BYTE alpha = 90)
{
    Gdiplus::SolidBrush shadowBrush(Gdiplus::Color(alpha, 0, 0, 0));
    graphics.FillPolygon(&shadowBrush, shadowPoints, static_cast<INT>(pointCount));
}

Gdiplus::PointF LerpPoint(const Gdiplus::PointF& from, const Gdiplus::PointF& to, double t)
{
    return Gdiplus::PointF(
        ToFloat(from.X + (to.X - from.X) * t),
        ToFloat(from.Y + (to.Y - from.Y) * t));
}

Gdiplus::PointF OffsetPoint(const Gdiplus::PointF& point, double x, double y)
{
    return Gdiplus::PointF(ToFloat(point.X + x), ToFloat(point.Y + y));
}

struct Vec2 {
    double x = 0.0;
    double y = 0.0;
};

using Vec3 = besktop::GaitVec3;

struct ProjectedPoint {
    Gdiplus::PointF point{};
    double depth = 0.0;
};

Vec3 RotateActorVector(const Vec3& local, const ActorPose& pose)
{
    const double cosX = std::cos(pose.rotateX);
    const double sinX = std::sin(pose.rotateX);
    const double cosZ = std::cos(pose.rotateZ);
    const double sinZ = std::sin(pose.rotateZ);

    const Vec3 afterY = besktop::RotateAroundVerticalAxis(local, pose.rotateY);
    const double xAfterY = afterY.x;
    const double zAfterY = afterY.z;
    const double yAfterX = local.y * cosX - zAfterY * sinX;
    const double zAfterX = local.y * sinX + zAfterY * cosX;

    const Vec3 bodyRotated{
        xAfterY * cosZ - yAfterX * sinZ,
        xAfterY * sinZ + yAfterX * cosZ,
        zAfterX,
    };
    return besktop::RotateAroundVerticalAxis(bodyRotated, pose.observationOrbitYaw);
}

ProjectedPoint ProjectActorPoint(const Vec3& local, const ActorPose& pose, double planeSide)
{
    const Vec3 rotated = RotateActorVector(local, pose);
    const double focalLength = std::max(520.0, planeSide * 12.0);
    const double denominator = std::max(80.0, focalLength - rotated.z);
    const double perspective = focalLength / denominator;

    return ProjectedPoint{
        Gdiplus::PointF(
            ToFloat(pose.x + rotated.x * perspective),
            ToFloat(pose.y + pose.bob + rotated.y * perspective)),
        rotated.z,
    };
}

bool IsFrontFaceVisible(const ActorPose& pose)
{
    return RotateActorVector(Vec3{0.0, 0.0, 1.0}, pose).z >= 0.0;
}

Vec3 ProjectedUpperBodyOffset(const ActorPose& pose, double planeSide)
{
    // This offset is deliberately projected as a standalone vector and is
    // applied only after the icon and arm chains have been solved. In
    // particular, it never enters ProjectActorPoint or ProjectLegChain.
    // It follows the pelvis yaw and observation camera, but not the torso's
    // own X/Z lean; otherwise a forty-five-degree slip consumes most of the
    // intended screen-space lateral translation.
    return besktop::RotateAroundVerticalAxis(
        Vec3{
            pose.action.upperBodyOffsetForward * planeSide,
            pose.action.upperBodyOffsetY * planeSide,
            pose.action.upperBodyOffsetDepth * planeSide,
        },
        pose.rotateY + pose.observationOrbitYaw);
}

struct BodyProjection {
    Gdiplus::PointF points[4]{};
    Gdiplus::PointF shadowPoints[4]{};
    double centerX = 0.0;
    double centerY = 0.0;
    double projectedWidth = 0.0;
    double projectedHeight = 0.0;
};

Gdiplus::RectF BoundsForPoints(const Gdiplus::PointF* points, size_t count);

BodyProjection BuildBodyProjection(const besktop::IconFightScene::IconActor& actor, const ActorPose& pose)
{
    constexpr double kMinimumProjectedEdge = 2.0;

    const double rawWidth = std::max(8.0, actor.planeWidth);
    const double rawHeight = std::max(8.0, actor.planeHeight);
    const besktop::TurnActorGeometry turnGeometry =
        besktop::BuildTurnActorGeometry(rawWidth, rawHeight);
    const double planeSide = turnGeometry.planeSide;
    const double halfW = rawWidth * 0.5;
    const double halfH = rawHeight * 0.5;
    const double localIconCenterX = -turnGeometry.bodyCenterOffset;

    BodyProjection projection;
    const ProjectedPoint projectedCenter = ProjectActorPoint(
        Vec3{localIconCenterX, 0.0, 0.0}, pose, planeSide);
    projection.centerX = projectedCenter.point.X;
    projection.centerY = projectedCenter.point.Y;
    projection.points[0] = ProjectActorPoint(
        Vec3{localIconCenterX - halfW, -halfH, 0.0}, pose, planeSide).point;
    projection.points[1] = ProjectActorPoint(
        Vec3{localIconCenterX + halfW, -halfH, 0.0}, pose, planeSide).point;
    projection.points[2] = ProjectActorPoint(
        Vec3{localIconCenterX + halfW, halfH, 0.0}, pose, planeSide).point;
    projection.points[3] = ProjectActorPoint(
        Vec3{localIconCenterX - halfW, halfH, 0.0}, pose, planeSide).point;

    Gdiplus::RectF bodyBounds = BoundsForPoints(projection.points, std::size(projection.points));
    if (bodyBounds.Width < kMinimumProjectedEdge) {
        const Vec2 rightAxis{std::cos(pose.rotateZ), std::sin(pose.rotateZ)};
        const double expandBy = (kMinimumProjectedEdge - bodyBounds.Width) * 0.5;
        projection.points[0] = OffsetPoint(projection.points[0], -rightAxis.x * expandBy, -rightAxis.y * expandBy);
        projection.points[3] = OffsetPoint(projection.points[3], -rightAxis.x * expandBy, -rightAxis.y * expandBy);
        projection.points[1] = OffsetPoint(projection.points[1], rightAxis.x * expandBy, rightAxis.y * expandBy);
        projection.points[2] = OffsetPoint(projection.points[2], rightAxis.x * expandBy, rightAxis.y * expandBy);
    }
    if (bodyBounds.Height < kMinimumProjectedEdge) {
        const Vec2 downAxis{-std::sin(pose.rotateZ), std::cos(pose.rotateZ)};
        const double expandBy = (kMinimumProjectedEdge - bodyBounds.Height) * 0.5;
        projection.points[0] = OffsetPoint(projection.points[0], -downAxis.x * expandBy, -downAxis.y * expandBy);
        projection.points[1] = OffsetPoint(projection.points[1], -downAxis.x * expandBy, -downAxis.y * expandBy);
        projection.points[2] = OffsetPoint(projection.points[2], downAxis.x * expandBy, downAxis.y * expandBy);
        projection.points[3] = OffsetPoint(projection.points[3], downAxis.x * expandBy, downAxis.y * expandBy);
    }

    const Vec3 upperBodyOffset = ProjectedUpperBodyOffset(pose, planeSide);
    projection.centerX += upperBodyOffset.x;
    projection.centerY += upperBodyOffset.y;
    for (Gdiplus::PointF& point : projection.points) {
        point = OffsetPoint(point, upperBodyOffset.x, upperBodyOffset.y);
    }

    bodyBounds = BoundsForPoints(projection.points, std::size(projection.points));
    projection.projectedWidth = bodyBounds.Width;
    projection.projectedHeight = bodyBounds.Height;

    const double shadowX = 4.0 * pose.bodyEffect;
    const double shadowY = 6.0 * pose.bodyEffect;
    for (size_t index = 0; index < std::size(projection.points); ++index) {
        projection.shadowPoints[index] = OffsetPoint(projection.points[index], shadowX, shadowY);
    }
    return projection;
}

Gdiplus::RectF BoundsForPoints(const Gdiplus::PointF* points, size_t count)
{
    float left = points[0].X;
    float top = points[0].Y;
    float right = points[0].X;
    float bottom = points[0].Y;
    for (size_t index = 1; index < count; ++index) {
        left = std::min(left, points[index].X);
        top = std::min(top, points[index].Y);
        right = std::max(right, points[index].X);
        bottom = std::max(bottom, points[index].Y);
    }
    return Gdiplus::RectF(left, top, std::max(1.0f, right - left), std::max(1.0f, bottom - top));
}

double LerpValue(double from, double to, double t)
{
    return from + (to - from) * Clamp01(t);
}

besktop::ActionSample BlendActionSamples(
    const besktop::ActionSample& from,
    const besktop::ActionSample& to,
    double weight)
{
    besktop::ActionSample result;
#define BESKTOP_BLEND_ACTION_FIELD(field) result.field = LerpValue(from.field, to.field, weight)
    BESKTOP_BLEND_ACTION_FIELD(bodyRotateX);
    BESKTOP_BLEND_ACTION_FIELD(bodyRotateY);
    BESKTOP_BLEND_ACTION_FIELD(bodyRotateZ);
    BESKTOP_BLEND_ACTION_FIELD(rootOffsetForward);
    BESKTOP_BLEND_ACTION_FIELD(rootOffsetLateral);
    BESKTOP_BLEND_ACTION_FIELD(rootOffsetY);
    BESKTOP_BLEND_ACTION_FIELD(upperBodyOffsetForward);
    BESKTOP_BLEND_ACTION_FIELD(upperBodyOffsetDepth);
    BESKTOP_BLEND_ACTION_FIELD(upperBodyOffsetY);
    BESKTOP_BLEND_ACTION_FIELD(hitStrength);
    BESKTOP_BLEND_ACTION_FIELD(punchStrength);
    BESKTOP_BLEND_ACTION_FIELD(kickStrength);
    BESKTOP_BLEND_ACTION_FIELD(dodgeStrength);
    BESKTOP_BLEND_ACTION_FIELD(whiffRecoveryStrength);
    BESKTOP_BLEND_ACTION_FIELD(leadHandForward);
    BESKTOP_BLEND_ACTION_FIELD(leadHandY);
    BESKTOP_BLEND_ACTION_FIELD(leadHandDepth);
    BESKTOP_BLEND_ACTION_FIELD(rearHandForward);
    BESKTOP_BLEND_ACTION_FIELD(rearHandY);
    BESKTOP_BLEND_ACTION_FIELD(rearHandDepth);
    BESKTOP_BLEND_ACTION_FIELD(leadArmBendForward);
    BESKTOP_BLEND_ACTION_FIELD(rearArmBendForward);
    BESKTOP_BLEND_ACTION_FIELD(leadShoulderForwardOffset);
    BESKTOP_BLEND_ACTION_FIELD(leadShoulderYOffset);
    BESKTOP_BLEND_ACTION_FIELD(leadShoulderDepthOffset);
    BESKTOP_BLEND_ACTION_FIELD(rearShoulderForwardOffset);
    BESKTOP_BLEND_ACTION_FIELD(rearShoulderYOffset);
    BESKTOP_BLEND_ACTION_FIELD(rearShoulderDepthOffset);
    BESKTOP_BLEND_ACTION_FIELD(handTargetWeight);
    BESKTOP_BLEND_ACTION_FIELD(leadFootForwardOffset);
    BESKTOP_BLEND_ACTION_FIELD(leadFootLift);
    BESKTOP_BLEND_ACTION_FIELD(leadFootDepthOffset);
    BESKTOP_BLEND_ACTION_FIELD(rearFootForwardOffset);
    BESKTOP_BLEND_ACTION_FIELD(rearFootLift);
    BESKTOP_BLEND_ACTION_FIELD(rearFootDepthOffset);
    BESKTOP_BLEND_ACTION_FIELD(footTargetWeight);
    BESKTOP_BLEND_ACTION_FIELD(footTargetYawCompensationWeight);
    BESKTOP_BLEND_ACTION_FIELD(footTargetRootCompensationWeight);
    BESKTOP_BLEND_ACTION_FIELD(lowerBodyActionRotationWeight);
    BESKTOP_BLEND_ACTION_FIELD(lowerBodyRotateX);
    BESKTOP_BLEND_ACTION_FIELD(lowerBodyRotateY);
    BESKTOP_BLEND_ACTION_FIELD(lowerBodyRotateZ);
#undef BESKTOP_BLEND_ACTION_FIELD
    result.leadHandTargetEnabled = from.leadHandTargetEnabled || to.leadHandTargetEnabled;
    result.rearHandTargetEnabled = from.rearHandTargetEnabled || to.rearHandTargetEnabled;
    result.leadFootTargetEnabled = from.leadFootTargetEnabled || to.leadFootTargetEnabled;
    result.rearFootTargetEnabled = from.rearFootTargetEnabled || to.rearFootTargetEnabled;
    return result;
}

Vec3 MoveLocalPolar(const Vec3& point, double length, double radians, double depthStep)
{
    return Vec3{
        point.x + std::cos(radians) * length,
        point.y + std::sin(radians) * length,
        point.z + depthStep,
    };
}

struct JointChain {
    Gdiplus::PointF root{};
    Gdiplus::PointF joint{};
    Gdiplus::PointF end{};
    double depth = 0.0;
    bool frontLayer = false;
};

struct LocalJointChain {
    Vec3 root{};
    Vec3 joint{};
    Vec3 end{};
};

struct LimbPose {
    JointChain leftArm{};
    JointChain rightArm{};
    JointChain leftLeg{};
    JointChain rightLeg{};
};

void OffsetJointChain(JointChain& chain, const Vec3& offset)
{
    chain.root = OffsetPoint(chain.root, offset.x, offset.y);
    chain.joint = OffsetPoint(chain.joint, offset.x, offset.y);
    chain.end = OffsetPoint(chain.end, offset.x, offset.y);
    chain.depth += offset.z;
    chain.frontLayer = chain.depth >= 0.0;
}

LocalJointChain BuildLocalChain(
    const Vec3& root,
    double firstLength,
    double secondLength,
    double firstAngle,
    double secondAngle,
    double firstDepthStep,
    double secondDepthStep)
{
    LocalJointChain chain;
    chain.root = root;
    chain.joint = MoveLocalPolar(root, firstLength, firstAngle, firstDepthStep);
    chain.end = MoveLocalPolar(chain.joint, secondLength, secondAngle, secondDepthStep);
    return chain;
}

LocalJointChain BuildTwoBoneIkChain(
    const Vec3& root,
    const Vec3& target,
    double firstLength,
    double secondLength,
    const Vec3& bendHint)
{
    const besktop::TwoBoneIkSolution solved = besktop::SolveTwoBoneIk(
        root, target, firstLength, secondLength, bendHint);
    LocalJointChain chain;
    chain.root = solved.root;
    chain.joint = solved.joint;
    chain.end = solved.end;
    return chain;
}

JointChain ProjectJointChain(const LocalJointChain& local, const ActorPose& pose, double planeSide)
{
    const ProjectedPoint root = ProjectActorPoint(local.root, pose, planeSide);
    const ProjectedPoint joint = ProjectActorPoint(local.joint, pose, planeSide);
    const ProjectedPoint end = ProjectActorPoint(local.end, pose, planeSide);

    JointChain chain;
    chain.root = root.point;
    chain.joint = joint.point;
    chain.end = end.point;
    chain.depth = (root.depth + joint.depth + end.depth) / 3.0;
    chain.frontLayer = chain.depth >= 0.0;
    return chain;
}

JointChain ProjectLegChain(
    const LocalJointChain& local,
    const ActorPose& lowerBodyPose,
    double planeSide)
{
    // Hip, knee and foot belong to one lower-body hierarchy. Projecting the
    // hip with the upper-body action pose while projecting the other joints
    // with the lower-body pose creates a false reversed-knee silhouette when
    // defensive actions isolate torso rotation.
    const ProjectedPoint root = ProjectActorPoint(local.root, lowerBodyPose, planeSide);
    const ProjectedPoint joint = ProjectActorPoint(local.joint, lowerBodyPose, planeSide);
    const ProjectedPoint end = ProjectActorPoint(local.end, lowerBodyPose, planeSide);

    JointChain chain;
    chain.root = root.point;
    chain.joint = joint.point;
    chain.end = end.point;
    chain.depth = (root.depth + joint.depth + end.depth) / 3.0;
    chain.frontLayer = chain.depth >= 0.0;
    return chain;
}

JointChain BuildArmChain(
    const Vec3& shoulder,
    int sideSign,
    double depthSign,
    const ActorPose& pose,
    double planeSide,
    double upperArmLength,
    double forearmLength,
    bool leadArm)
{
    constexpr double headingSign = 1.0;
    const besktop::GaitGeometry gaitGeometry = besktop::BuildGaitGeometry(
        planeSide, planeSide * 0.40, planeSide * 0.41);
    const besktop::GaitLegSample oppositeLeg = besktop::SampleGaitLeg(
        pose.walkPhase,
        sideSign > 0 ? 0.5 : 0.0,
        gaitGeometry,
        pose.locomotionWeight);
    const double strideScale = std::max(1.0, gaitGeometry.stride * 0.45);
    double forward = headingSign * std::clamp(oppositeLeg.footForward / strideScale, -1.0, 1.0);
    forward = -forward;

    const double lift = gaitGeometry.stepHeight > 0.0 ?
        oppositeLeg.footLift / gaitGeometry.stepHeight :
        0.0;
    double upperAngleDegrees = 94.0 - (forward * 25.0) - (lift * 3.0);
    double foreAngleDegrees = 108.0 - (forward * 17.0) + (lift * 4.0);
    upperAngleDegrees = 180.0 - upperAngleDegrees;
    foreAngleDegrees = 180.0 - foreAngleDegrees;

    const double upperAngle = DegreesToRadians(upperAngleDegrees);
    const double foreAngle = DegreesToRadians(foreAngleDegrees);
    LocalJointChain local = BuildLocalChain(
        shoulder,
        upperArmLength,
        forearmLength,
        upperAngle,
        foreAngle,
        depthSign * planeSide * 0.045,
        depthSign * planeSide * 0.040);
    const besktop::ActionSample& action = pose.action;
    const bool targetEnabled = leadArm ? action.leadHandTargetEnabled : action.rearHandTargetEnabled;
    if (targetEnabled && action.handTargetWeight > 0.001) {
        const double forward = leadArm ? action.leadHandForward : action.rearHandForward;
        const double targetY = leadArm ? action.leadHandY : action.rearHandY;
        const double targetDepth = leadArm ? action.leadHandDepth : action.rearHandDepth;
        const double bendForward = leadArm ?
            action.leadArmBendForward : action.rearArmBendForward;
        const Vec3 target{
            shoulder.x + headingSign * forward * planeSide,
            targetY * planeSide,
            depthSign * targetDepth * planeSide,
        };
        const LocalJointChain actionChain = BuildTwoBoneIkChain(
            shoulder,
            target,
            upperArmLength,
            forearmLength,
            Vec3{headingSign * bendForward, 1.0, depthSign * 0.25});
        const double weight = Clamp01(action.handTargetWeight);
        local.joint.x = LerpValue(local.joint.x, actionChain.joint.x, weight);
        local.joint.y = LerpValue(local.joint.y, actionChain.joint.y, weight);
        local.joint.z = LerpValue(local.joint.z, actionChain.joint.z, weight);
        local.end.x = LerpValue(local.end.x, actionChain.end.x, weight);
        local.end.y = LerpValue(local.end.y, actionChain.end.y, weight);
        local.end.z = LerpValue(local.end.z, actionChain.end.z, weight);
    }
    return ProjectJointChain(local, pose, planeSide);
}

JointChain BuildLegChain(
    const Vec3& hip,
    int sideSign,
    double depthSign,
    const ActorPose& pose,
    const ActorPose& lowerBodyPose,
    double planeSide,
    double thighLength,
    double shinLength,
    bool leadLeg)
{
    constexpr double headingSign = 1.0;
    const besktop::GaitGeometry gaitGeometry = besktop::BuildGaitGeometry(
        planeSide, thighLength, shinLength);
    const besktop::GaitLegSample sample = besktop::SampleGaitLeg(
        pose.walkPhase,
        sideSign > 0 ? 0.0 : 0.5,
        gaitGeometry,
        pose.locomotionWeight);
    const double groundY = hip.y + gaitGeometry.legDrop;
    const double neutralSplit =
        static_cast<double>(sideSign) * gaitGeometry.stride * 0.10 * (1.0 - pose.locomotionWeight);

    Vec3 footTarget{
        hip.x + neutralSplit + (headingSign * sample.footForward),
        groundY - sample.footLift,
        hip.z + (depthSign * gaitGeometry.footDepth),
    };
    Vec3 bendHint{headingSign, 0.0, depthSign * 0.18};
    const besktop::ActionSample& action = pose.action;
    const bool targetEnabled = leadLeg ? action.leadFootTargetEnabled : action.rearFootTargetEnabled;
    if (targetEnabled && action.footTargetWeight > 0.001) {
        const double forwardOffset = leadLeg ?
            action.leadFootForwardOffset : action.rearFootForwardOffset;
        const double lift = leadLeg ? action.leadFootLift : action.rearFootLift;
        const double depthOffset = leadLeg ?
            action.leadFootDepthOffset : action.rearFootDepthOffset;
        const Vec3 actionTarget{
            footTarget.x + headingSign * forwardOffset * planeSide,
            footTarget.y - lift * planeSide,
            footTarget.z + depthSign * depthOffset * planeSide,
        };
        const double weight = Clamp01(action.footTargetWeight);
        footTarget.x = LerpValue(footTarget.x, actionTarget.x, weight);
        footTarget.y = LerpValue(footTarget.y, actionTarget.y, weight);
        footTarget.z = LerpValue(footTarget.z, actionTarget.z, weight);
    }

    const double rootCompensation = Clamp01(action.footTargetRootCompensationWeight);
    if (rootCompensation > 0.001) {
        footTarget.x -= action.rootOffsetForward * planeSide * rootCompensation;
        footTarget.y -= action.rootOffsetY * planeSide * rootCompensation;
    }

    const double lowerBodyActionYaw =
        (action.bodyRotateY * action.lowerBodyActionRotationWeight) +
        action.lowerBodyRotateY;
    const double yawCompensation = Clamp01(action.footTargetYawCompensationWeight);
    if (yawCompensation > 0.001 && std::abs(lowerBodyActionYaw) > 0.001) {
        const Vec3 compensatedTarget = besktop::RotateAroundVerticalAxis(
            footTarget, -lowerBodyActionYaw);
        footTarget.x = LerpValue(footTarget.x, compensatedTarget.x, yawCompensation);
        footTarget.y = LerpValue(footTarget.y, compensatedTarget.y, yawCompensation);
        footTarget.z = LerpValue(footTarget.z, compensatedTarget.z, yawCompensation);
        const Vec3 compensatedBendHint = besktop::RotateAroundVerticalAxis(
            bendHint, -lowerBodyActionYaw);
        bendHint.x = LerpValue(bendHint.x, compensatedBendHint.x, yawCompensation);
        bendHint.y = LerpValue(bendHint.y, compensatedBendHint.y, yawCompensation);
        bendHint.z = LerpValue(bendHint.z, compensatedBendHint.z, yawCompensation);
    }

    const LocalJointChain local = BuildTwoBoneIkChain(
        hip,
        footTarget,
        thighLength,
        shinLength,
        bendHint);
    return ProjectLegChain(local, lowerBodyPose, planeSide);
}

Vec3 ApplyActionShoulderOffset(
    const Vec3& neutralShoulder,
    double depthSign,
    const besktop::ActionSample& action,
    bool leadShoulder,
    double planeSide)
{
    const double forward = leadShoulder ?
        action.leadShoulderForwardOffset : action.rearShoulderForwardOffset;
    const double offsetY = leadShoulder ?
        action.leadShoulderYOffset : action.rearShoulderYOffset;
    const double depth = leadShoulder ?
        action.leadShoulderDepthOffset : action.rearShoulderDepthOffset;
    return Vec3{
        neutralShoulder.x + forward * planeSide,
        neutralShoulder.y + offsetY * planeSide,
        neutralShoulder.z + depthSign * depth * planeSide,
    };
}

LimbPose BuildLimbPose(
    const besktop::IconFightScene::IconActor& actor,
    const ActorPose& pose)
{
    const double planeSide = std::max(24.0, std::max(actor.planeWidth, actor.planeHeight));
    const double halfH = std::max(8.0, actor.planeHeight) * 0.5;
    const double shoulderDepth = planeSide * 0.24;
    const double hipDepth = planeSide * 0.22;
    constexpr double rightDepthSign = 1.0;
    constexpr double leftDepthSign = -1.0;
    const double smallHipExtraOut = 0.0;
    const double hipDownOut = std::clamp(planeSide * 0.18, 12.0, 24.0);
    constexpr double shoulderRootX = 0.0;
    const double shoulderRootY = -halfH * 0.20;
    const Vec3 neutralLeftShoulder{shoulderRootX, shoulderRootY, leftDepthSign * shoulderDepth};
    const Vec3 neutralRightShoulder{shoulderRootX, shoulderRootY, rightDepthSign * shoulderDepth};
    const double hipRootY = halfH + hipDownOut;
    const double hipRootX = smallHipExtraOut;
    const Vec3 leftHip{hipRootX, hipRootY, leftDepthSign * hipDepth};
    const Vec3 rightHip{hipRootX, hipRootY, rightDepthSign * hipDepth};
    const double upperArmLength = planeSide * 0.30;
    const double forearmLength = planeSide * 0.32;
    const double thighLength = planeSide * 0.40;
    const double shinLength = planeSide * 0.41;
    ActorPose lowerBodyPose = pose;
    const double kLowerBodyActionRotation = std::clamp(
        pose.action.lowerBodyActionRotationWeight, 0.0, 1.0);
    lowerBodyPose.rotateX -= pose.action.bodyRotateX * (1.0 - kLowerBodyActionRotation);
    lowerBodyPose.rotateY -= pose.action.bodyRotateY * (1.0 - kLowerBodyActionRotation);
    lowerBodyPose.rotateZ -= pose.action.bodyRotateZ * (1.0 - kLowerBodyActionRotation);
    lowerBodyPose.rotateX += pose.action.lowerBodyRotateX;
    lowerBodyPose.rotateY += pose.action.lowerBodyRotateY;
    lowerBodyPose.rotateZ += pose.action.lowerBodyRotateZ;

    // Camera orbit changes only the view. Logical lead/rear limbs continue to
    // follow the actor's committed facing and never swap at a side-on view.
    const bool rightLead = pose.facing > 0.0;
    const Vec3 leftShoulder = ApplyActionShoulderOffset(
        neutralLeftShoulder, leftDepthSign, pose.action, !rightLead, planeSide);
    const Vec3 rightShoulder = ApplyActionShoulderOffset(
        neutralRightShoulder, rightDepthSign, pose.action, rightLead, planeSide);

    LimbPose limbs;
    limbs.leftArm = BuildArmChain(
        leftShoulder, -1, leftDepthSign, pose, planeSide, upperArmLength, forearmLength, !rightLead);
    limbs.rightArm = BuildArmChain(
        rightShoulder, 1, rightDepthSign, pose, planeSide, upperArmLength, forearmLength, rightLead);
    const Vec3 upperBodyOffset = ProjectedUpperBodyOffset(pose, planeSide);
    OffsetJointChain(limbs.leftArm, upperBodyOffset);
    OffsetJointChain(limbs.rightArm, upperBodyOffset);
    limbs.leftLeg = BuildLegChain(
        leftHip, -1, leftDepthSign, pose, lowerBodyPose, planeSide, thighLength, shinLength, !rightLead);
    limbs.rightLeg = BuildLegChain(
        rightHip, 1, rightDepthSign, pose, lowerBodyPose, planeSide, thighLength, shinLength, rightLead);
    return limbs;
}

Gdiplus::PointF ShadowPoint(const Gdiplus::PointF& point)
{
    return OffsetPoint(point, 2.0, 2.5);
}

void DrawJointChain(
    Gdiplus::Graphics& graphics,
    const JointChain& chain,
    Gdiplus::Pen* shadow,
    Gdiplus::Pen& limb)
{
    if (shadow != nullptr) {
        const Gdiplus::PointF shadowPoints[] = {
            ShadowPoint(chain.root),
            ShadowPoint(chain.joint),
            ShadowPoint(chain.end),
        };
        graphics.DrawLines(shadow, shadowPoints, static_cast<INT>(std::size(shadowPoints)));
    }
    const Gdiplus::PointF points[] = {
        chain.root,
        chain.joint,
        chain.end,
    };
    graphics.DrawLines(&limb, points, static_cast<INT>(std::size(points)));
}

void DrawActorLimbs(
    Gdiplus::Graphics& graphics,
    const besktop::IconFightScene::IconActor& actor,
    const ActorPose& pose,
    const BodyProjection& body,
    const LimbPose& limbs,
    LimbLayer layer,
    Gdiplus::Pen* sharedLimb,
    float sharedLimbWidth)
{
    if (pose.limbGrow <= 0.01) {
        return;
    }

    const double alphaCeiling = layer == LimbLayer::Front ? 248.0 : 178.0;
    const double alphaValue = alphaCeiling * pose.limbGrow;
    const unsigned char alpha = static_cast<unsigned char>(std::clamp(alphaValue, 0.0, 255.0));
    const besktop::TurnActorGeometry turnGeometry =
        besktop::BuildTurnActorGeometry(actor.planeWidth, actor.planeHeight);
    const double planeSide = turnGeometry.planeSide;
    const float limbWidth = ToFloat(turnGeometry.limbWidth);
    const bool renderShadows = RenderShadowsEnabled();

    const JointChain* chains[] = {
        &limbs.leftArm,
        &limbs.rightArm,
        &limbs.leftLeg,
        &limbs.rightLeg,
    };

    const auto drawChains = [&](Gdiplus::Pen* shadow, Gdiplus::Pen& limb) {
        for (const JointChain* chain : chains) {
            const bool shouldDraw = layer == LimbLayer::Front ? chain->frontLayer : !chain->frontLayer;
            if (shouldDraw) {
                DrawJointChain(graphics, *chain, shadow, limb);
            }
        }
    };

    const bool canUseSharedLimb = sharedLimb != nullptr &&
        !renderShadows &&
        pose.limbGrow >= 0.999 &&
        std::abs(limbWidth - sharedLimbWidth) < 0.01f;
    if (canUseSharedLimb) {
        drawChains(nullptr, *sharedLimb);
    } else {
        Gdiplus::Pen limb(Gdiplus::Color(alpha, 250, 253, 255), limbWidth);
        limb.SetStartCap(Gdiplus::LineCapRound);
        limb.SetEndCap(Gdiplus::LineCapRound);
        if (renderShadows) {
            Gdiplus::Pen shadow(
                Gdiplus::Color(static_cast<BYTE>(alpha * 0.28), 0, 0, 0),
                limbWidth + 3.8f);
            shadow.SetStartCap(Gdiplus::LineCapRound);
            shadow.SetEndCap(Gdiplus::LineCapRound);
            drawChains(&shadow, limb);
        } else {
            drawChains(nullptr, limb);
        }
    }

    if (layer == LimbLayer::Front) {
        const JointChain& frontArm = limbs.rightArm.frontLayer ? limbs.rightArm : limbs.leftArm;
        const JointChain& frontLeg = limbs.rightLeg.frontLayer ? limbs.rightLeg : limbs.leftLeg;
        const double impactSide = frontArm.end.X >= body.centerX ? 1.0 : -1.0;
        const bool hitImpactAllowed = !actor.combatPreviewActor || actor.combatImpactVisible;
        if (actor.combatPreviewActor && actor.combatBlockedImpact && pose.punch > 0.15) {
            DrawParryImpact(
                graphics,
                frontArm.end.X + ToFloat(impactSide * 10.0),
                frontArm.end.Y,
                pose.punch);
        } else if (hitImpactAllowed && pose.punch > 0.15) {
            DrawImpact(graphics, frontArm.end.X + ToFloat(impactSide * 16.0), frontArm.end.Y, pose.punch);
        }
        if (hitImpactAllowed && pose.kick > 0.15) {
            DrawImpact(graphics, frontLeg.end.X + ToFloat(impactSide * 18.0), frontLeg.end.Y, pose.kick);
        }
    }
}

bool DrawActorIconBody(
    Gdiplus::Graphics& graphics,
    const besktop::IconFightScene::IconActor& actor,
    const ActorPose& pose,
    const Gdiplus::PointF* bodyPoints,
    const Gdiplus::PointF* shadowPoints,
    size_t pointCount)
{
    if (actor.iconImage == nullptr || !actor.iconImage->IsValid()) {
        return false;
    }

    Gdiplus::Bitmap* bitmap = actor.iconImage->bitmap.get();
    if (bitmap == nullptr) {
        return false;
    }

    const BYTE shadowAlpha = RenderShadowsEnabled() ?
        static_cast<BYTE>(std::clamp(pose.bodyEffect * pose.bodyEffect * 58.0, 0.0, 58.0)) :
        0;
    if (shadowAlpha > 0) {
        DrawBodyShadow(graphics, shadowPoints, pointCount, shadowAlpha);
    }

    const bool frontFace = IsFrontFaceVisible(pose);
    const Gdiplus::PointF frontDestination[] = {
        bodyPoints[0],
        bodyPoints[1],
        bodyPoints[3],
    };
    const Gdiplus::PointF backDestination[] = {
        bodyPoints[1],
        bodyPoints[0],
        bodyPoints[2],
    };
    const Gdiplus::PointF* destination = frontFace ? frontDestination : backDestination;

    const Gdiplus::Status drawStatus = graphics.DrawImage(
        bitmap,
        destination,
        3,
        0.0f,
        0.0f,
        static_cast<Gdiplus::REAL>(actor.iconImage->width),
        static_cast<Gdiplus::REAL>(actor.iconImage->height),
        Gdiplus::UnitPixel);
    if (drawStatus != Gdiplus::Ok) {
        return false;
    }

    const BYTE borderAlpha = static_cast<BYTE>(std::clamp(SmoothStep(0.45, 1.0, pose.bodyEffect) * 42.0, 0.0, 42.0));
    if (DebugIconPlaneEnabled() && borderAlpha > 0) {
        Gdiplus::Pen border(Gdiplus::Color(borderAlpha, 255, 255, 255), 1.5f);
        graphics.DrawPolygon(&border, bodyPoints, static_cast<INT>(pointCount));
    }

    if (pose.hit > 0.04) {
        const BYTE flashAlpha = static_cast<BYTE>(std::clamp(pose.hit * 28.0, 0.0, 28.0));
        Gdiplus::SolidBrush flash(Gdiplus::Color(flashAlpha, 255, 255, 255));
        graphics.FillPolygon(&flash, bodyPoints, static_cast<INT>(pointCount));
    }

    return true;
}

void DrawActorBody(
    Gdiplus::Graphics& graphics,
    const besktop::IconFightScene::IconActor& actor,
    const ActorPose& pose,
    const BodyProjection& body)
{
    if (DrawActorIconBody(graphics, actor, pose, body.points, body.shadowPoints, std::size(body.points))) {
        return;
    }

    const BYTE fallbackShadowAlpha = RenderShadowsEnabled() ?
        static_cast<BYTE>(std::clamp(pose.bodyEffect * 90.0, 0.0, 90.0)) :
        0;
    if (fallbackShadowAlpha > 0) {
        DrawBodyShadow(graphics, body.shadowPoints, std::size(body.shadowPoints), fallbackShadowAlpha);
    }

    const Gdiplus::RectF bodyBounds = BoundsForPoints(body.points, std::size(body.points));
    Gdiplus::LinearGradientBrush bodyBrush(
        bodyBounds,
        WithAlpha(255, actor.red, actor.green, actor.blue),
        WithAlpha(255, static_cast<unsigned char>(std::max(20, actor.red - 34)), static_cast<unsigned char>(std::max(20, actor.green - 34)), static_cast<unsigned char>(std::max(20, actor.blue - 34))),
        Gdiplus::LinearGradientModeForwardDiagonal);
    graphics.FillPolygon(&bodyBrush, body.points, static_cast<INT>(std::size(body.points)));

    const BYTE borderAlpha = static_cast<BYTE>(std::clamp((0.48 + 0.52 * pose.bodyEffect) * 210.0, 0.0, 210.0));
    Gdiplus::Pen border(Gdiplus::Color(borderAlpha, 255, 255, 255), 2.0f);
    graphics.DrawPolygon(&border, body.points, static_cast<INT>(std::size(body.points)));

    Gdiplus::SolidBrush shine(Gdiplus::Color(86, 255, 255, 255));
    graphics.FillEllipse(
        &shine,
        bodyBounds.X + bodyBounds.Width * 0.18f,
        bodyBounds.Y + bodyBounds.Height * 0.14f,
        bodyBounds.Width * 0.24f,
        bodyBounds.Height * 0.16f);

    Gdiplus::FontFamily fontFamily(L"Microsoft YaHei UI");
    Gdiplus::Font font(&fontFamily, 18.0f, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
    DrawCenteredString(
        graphics,
        FirstGlyph(actor.label),
        bodyBounds,
        font,
        Gdiplus::Color(245, 255, 255, 255));
}

void DrawActorLabel(
    Gdiplus::Graphics& graphics,
    const besktop::IconFightScene::IconActor& actor,
    const ActorPose& pose,
    const BodyProjection& body,
    const CachedLabelBitmap* cachedLabel,
    double elapsedSeconds)
{
    if (pose.labelAlpha <= 0.02) {
        return;
    }

    const double actorTime = std::max(0.0, elapsedSeconds - actor.awakeningStartSeconds);
    const double shake = SmoothStep(0.02, 0.16, actorTime) *
        (1.0 - SmoothStep(0.34, kAwakeningDurationSeconds, actorTime));
    const double jitterX = std::sin((actorTime * 66.0) + actor.role) * 5.0 * shake;
    const double jitterY = std::cos((actorTime * 73.0) + actor.role) * 3.0 * shake;
    if (cachedLabel != nullptr && cachedLabel->bitmap != nullptr) {
        Gdiplus::ColorMatrix alphaMatrix = {
            1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 0.0f, static_cast<float>(pose.labelAlpha), 0.0f,
            0.0f, 0.0f, 0.0f, 0.0f, 1.0f,
        };
        Gdiplus::ImageAttributes attributes;
        attributes.SetColorMatrix(&alphaMatrix, Gdiplus::ColorMatrixFlagsDefault, Gdiplus::ColorAdjustTypeBitmap);
        const INT width = static_cast<INT>(cachedLabel->bitmap->GetWidth());
        const INT height = static_cast<INT>(cachedLabel->bitmap->GetHeight());
        graphics.DrawImage(
            cachedLabel->bitmap.get(),
            Gdiplus::Rect(
                static_cast<INT>(std::lround(actor.labelBounds.left + jitterX + cachedLabel->offsetX)),
                static_cast<INT>(std::lround(actor.labelBounds.top + jitterY + cachedLabel->offsetY)),
                width,
                height),
            0,
            0,
            width,
            height,
            Gdiplus::UnitPixel,
            &attributes);
        return;
    }

    const Gdiplus::RectF bodyBounds = BoundsForPoints(body.points, std::size(body.points));

    Gdiplus::RectF labelRect(
        ToFloat(actor.labelBounds.left + jitterX),
        ToFloat(actor.labelBounds.top + jitterY),
        ToFloat(actor.labelBounds.right - actor.labelBounds.left),
        ToFloat(actor.labelBounds.bottom - actor.labelBounds.top));
    if (labelRect.Width <= 1.0f || labelRect.Height <= 1.0f) {
        labelRect = Gdiplus::RectF(
            ToFloat(pose.x - 82.0 + jitterX),
            ToFloat(bodyBounds.Y + bodyBounds.Height + 6.0 + jitterY),
            164.0f,
            34.0f);
    }

    DrawDesktopIconLabel(graphics, actor.label, labelRect, pose.labelAlpha);
}

const wchar_t* PhaseName(besktop::IconFightScene::ScenePhase phase)
{
    switch (phase) {
    case besktop::IconFightScene::ScenePhase::Awakening:
        return L"awakening";
    case besktop::IconFightScene::ScenePhase::GrowingLimbs:
        return L"growing-limbs";
    case besktop::IconFightScene::ScenePhase::Wandering:
        return L"wandering";
    case besktop::IconFightScene::ScenePhase::Sleeping:
    default:
        return L"sleeping";
    }
}

} // namespace

namespace besktop {

struct IconFightScene::RenderCache {
    std::vector<BodyProjection> bodies;
    std::vector<LimbPose> limbs;
    std::vector<CachedLabelBitmap> labels;
};

IconFightScene::IconFightScene()
    : renderCache_(std::make_unique<RenderCache>())
{
}

IconFightScene::~IconFightScene() = default;

bool IconFightScene::ToggleAutomaticInteractions()
{
    if (!combatDirectorEnabled_) return false;
    const bool enabled = !activeEncounterPool_.desiredEnabled;
    SetActiveEncounterPoolEnabled(activeEncounterPool_, enabled);
    automaticInteractionToast_ = enabled ?
        L"\u81ea\u52a8\u4e92\u52a8\uff1a\u5df2\u5f00\u542f" :
        L"\u81ea\u52a8\u4e92\u52a8\uff1a\u5df2\u5173\u95ed\uff0c\u4ec5\u81ea\u7531\u6f2b\u6e38";
    automaticInteractionToastStartTick_ = GetTickCount64();
    return true;
}

void IconFightScene::Reset(const DesktopSnapshot& snapshot, const RECT& clientRect)
{
    actors_.clear();
    poseCache_.clear();
    iconImageCache_.Clear();
    monitorBounds_ = snapshot.monitorBounds;
    clientBounds_ = clientRect;
    wanderBounds_ = clientRect;
    usingCapturedWorkArea_ = false;
    elapsedSeconds_ = 0.0;
    previousElapsedSeconds_ = 0.0;
    phase_ = ScenePhase::Sleeping;
    const RuntimeOptions& options = GetRuntimeOptions();
    const RuntimeExperienceMode experienceMode = ResolveRuntimeExperienceMode(options);
    combatPreview_ = experienceMode == RuntimeExperienceMode::FixedCombatPreview ?
        options.combatPreview : CombatScenarioId::None;
    combatDirectorEnabled_ = experienceMode == RuntimeExperienceMode::CombatDirector;
    combatDirectorDiagnosticsEnabled_ = combatDirectorEnabled_ &&
        options.combatDirectorDiagnosticsEnabled;
    combatPairState_ = {};
    awakeningDirectorState_ = {};
    awakeningInputs_.clear();
    awakeningObservations_.clear();
    awakeningReadinessLogged_ = false;
    awakeningCompletionLogged_ = false;
    activeEncounterPool_ = {};
    activeEncounterActorInputs_.clear();
    ecosystemPerceptionInputs_.clear();
    ecosystemRuntimeSnapshots_.clear();
    ecosystemIntentSnapshot_.clear();
    encounterArbiterActors_.clear();
    eventReactionSnapshots_.clear();
    eventReactionInputs_.clear();
    eventReactionStates_.clear();
    eventReactionSteps_.clear();
    eventReactionStats_ = {};
    eventReactionUnsafeRejections_ = 0;
    combatDirectorAvoidanceReplans_ = 0;
    loggedActiveEncounterCount_ = 0;
    loggedActiveReactionCount_ = 0;
    loggedCombatPhase_ = CombatPairPhase::Inactive;
    previewAction_ = experienceMode == RuntimeExperienceMode::ActionPreview ?
        options.actionPreview : ActionId::None;
    actionOrbitCameraEnabled_ = options.actionOrbitCameraEnabled;
    turnPreviewEnabled_ = experienceMode == RuntimeExperienceMode::TurnPreview;
    automaticInteractionToast_.clear();
    automaticInteractionToastStartTick_ = 0;

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
    const double coordinateScale = std::max(0.25, (scaleX + scaleY) * 0.5);
    const bool hasImageListSize = snapshot.iconDisplay.imageListIconSize.cx > 0 &&
        snapshot.iconDisplay.imageListIconSize.cy > 0;
    const double displayPlaneWidth = hasImageListSize ?
        static_cast<double>(snapshot.iconDisplay.imageListIconSize.cx) * coordinateScale :
        48.0 * coordinateScale;
    const double displayPlaneHeight = hasImageListSize ?
        static_cast<double>(snapshot.iconDisplay.imageListIconSize.cy) * coordinateScale :
        48.0 * coordinateScale;
    const std::wstring planeSizeSource = hasImageListSize ?
        snapshot.iconDisplay.source :
        std::wstring(L"scene fallback");
    const auto scaleDesktopRect = [&](const RECT& rect) {
        return RECT{
            static_cast<LONG>((rect.left - snapshot.monitorBounds.left) * scaleX),
            static_cast<LONG>((rect.top - snapshot.monitorBounds.top) * scaleY),
            static_cast<LONG>((rect.right - snapshot.monitorBounds.left) * scaleX),
            static_cast<LONG>((rect.bottom - snapshot.monitorBounds.top) * scaleY),
        };
    };
    if (!snapshot.usedWorkAreaFallback &&
        snapshot.workArea.right > snapshot.workArea.left &&
        snapshot.workArea.bottom > snapshot.workArea.top) {
        RECT mappedWorkArea = scaleDesktopRect(snapshot.workArea);
        RECT clippedWorkArea{};
        if (IntersectRect(&clippedWorkArea, &mappedWorkArea, &clientBounds_) &&
            clippedWorkArea.right > clippedWorkArea.left &&
            clippedWorkArea.bottom > clippedWorkArea.top) {
            wanderBounds_ = clippedWorkArea;
            usingCapturedWorkArea_ = true;
        }
    }

    const size_t availableCount = snapshot.icons.size();
    const unsigned int configuredMaxActors = besktop::GetRuntimeOptions().maxActors;
    const size_t count = configuredMaxActors > 0 ?
        std::min(availableCount, static_cast<size_t>(configuredMaxActors)) :
        availableCount;
    for (size_t index = 0; index < count; ++index) {
        const DesktopIconSnapshot& icon = snapshot.icons[index];
        const bool hasIconBounds = icon.iconBounds.right > icon.iconBounds.left &&
            icon.iconBounds.bottom > icon.iconBounds.top;
        const RECT planeBounds = hasIconBounds ? icon.iconBounds : icon.bounds;

        IconActor actor;
        actor.label = icon.displayName.empty() ? (L"Icon " + std::to_wstring(index + 1)) : icon.displayName;
        actor.randomState = 0x9E3779B9u ^ (static_cast<std::uint32_t>(index + 1) * 0x85EBCA6Bu);
        actor.randomState ^= actor.randomState >> 16;
        actor.behaviorProfile = GenerateActorBehaviorProfile(actor.randomState ^ 0xD1B54A35u);
        InitializeActorRuntimeState(actor.runtimeState, actor.randomState ^ 0x94D049BBu);
        TurnFacing initialFacing = turnPreviewEnabled_ && index == 0 ?
            TurnFacing::Right :
            ((actor.randomState & 1u) == 0u ? TurnFacing::Right : TurnFacing::Left);
        const double capturedIconCenterX =
            (((planeBounds.left + planeBounds.right) * 0.5) - snapshot.monitorBounds.left) * scaleX;
        const TurnActorGeometry turnGeometry =
            BuildTurnActorGeometry(displayPlaneWidth, displayPlaneHeight);
        const double planeSide = turnGeometry.planeSide;
        const double localIconCenterOffset = -turnGeometry.bodyCenterOffset;
        initialFacing = ChooseTurnSafeInitialFacing(
            initialFacing,
            capturedIconCenterX,
            localIconCenterOffset,
            displayPlaneWidth * 0.5,
            static_cast<double>(clientBounds_.left),
            static_cast<double>(clientBounds_.right));
        const double initialFacingSign = initialFacing == TurnFacing::Right ? 1.0 : -1.0;
        actor.baseX = capturedIconCenterX - initialFacingSign * localIconCenterOffset;
        actor.baseY = (((planeBounds.top + planeBounds.bottom) * 0.5) - snapshot.monitorBounds.top) * scaleY;
        actor.x = actor.baseX;
        actor.y = actor.baseY;
        actor.battleX = actor.baseX;
        actor.battleY = actor.baseY;
        actor.labelBounds = scaleDesktopRect(icon.labelBounds);
        actor.usedLabelBoundsFallback = icon.usedLabelBoundsFallback;
        actor.planeWidth = displayPlaneWidth;
        actor.planeHeight = displayPlaneHeight;
        actor.usedPlaneFallback = !hasImageListSize || snapshot.iconDisplay.usedFallback;
        actor.planeSizeSource = planeSizeSource;
        actor.role = static_cast<int>(index);
        actor.walkSpeed = 58.0 + static_cast<double>((actor.randomState >> 8) % 49u);
        InitializeTurnMotion(actor.turnMotion, initialFacing);
        actor.walkPhase = std::fmod(static_cast<double>(actor.role) * 0.17, 1.0);
        actor.red = colors[index % std::size(colors)][0];
        actor.green = colors[index % std::size(colors)][1];
        actor.blue = colors[index % std::size(colors)][2];
        actor.iconImage = iconImageCache_.Load(icon.image, actor.label);
        actor.usedIconImageFallback = actor.iconImage == nullptr;
        actor.combatPreviewActor = index < 2 && combatPreview_ != CombatScenarioId::None;
        actor.turnPreviewActor = index == 0 && turnPreviewEnabled_ && !actor.combatPreviewActor;
        actor.actionPreviewActor = index == 0 && previewAction_ != ActionId::None && !actor.turnPreviewActor;
        actor.actionOrbitCameraEnabled = actor.actionPreviewActor && actionOrbitCameraEnabled_;
        actors_.push_back(actor);
    }

    awakeningInputs_.reserve(actors_.size());
    for (std::size_t index = 0; index < actors_.size(); ++index) {
        const IconActor& actor = actors_[index];
        awakeningInputs_.push_back({
            index,
            actor.baseX,
            actor.baseY,
            std::max(actor.planeWidth, actor.planeHeight),
            actor.randomState ^ 0xA511E9B3u,
        });
    }
    InitializeAwakeningDirector(
        awakeningDirectorState_,
        awakeningInputs_,
        {
            static_cast<double>(wanderBounds_.left),
            static_cast<double>(wanderBounds_.top),
            static_cast<double>(wanderBounds_.right),
            static_cast<double>(wanderBounds_.bottom),
        });
    awakeningObservations_.resize(actors_.size());
    for (std::size_t index = 0; index < actors_.size(); ++index) {
        actors_[index].awakeningStartSeconds =
            GetActorAwakeningStartSeconds(awakeningDirectorState_, index);
    }

    for (IconActor& actor : actors_) {
        ChooseWanderTarget(actor);
    }
    if (combatPreview_ != CombatScenarioId::None && actors_.size() >= 2) {
        ConfigureCombatStations(
            0, 1, combatPreview_,
            (wanderBounds_.left + wanderBounds_.right) * 0.5,
            (wanderBounds_.top + wanderBounds_.bottom) * 0.46);
    }
    InitializeActiveEncounterPool(
        activeEncounterPool_, actors_.size(), combatDirectorEnabled_, 0xA17EC7E1u);
    SetActiveEncounterPoolPreviewSuspended(
        activeEncounterPool_,
        combatPreview_ != CombatScenarioId::None || previewAction_ != ActionId::None || turnPreviewEnabled_);
    activeEncounterActorInputs_.reserve(actors_.size());
    ecosystemPerceptionInputs_.reserve(actors_.size());
    ecosystemRuntimeSnapshots_.reserve(actors_.size());
    ecosystemIntentSnapshot_.reserve(actors_.size());
    encounterArbiterActors_.reserve(actors_.size());
    eventReactionInputs_.reserve(actors_.size());
    eventReactionStates_.resize(actors_.size());
    eventReactionSteps_.resize(actors_.size());
    for (std::size_t index = 0; index < actors_.size(); ++index) {
        InitializeActorEventReactionState(
            eventReactionStates_[index], actors_[index].randomState ^ 0x73A4D29Bu);
    }
    poseCache_.resize(actors_.size());
    renderCache_->bodies.resize(actors_.size());
    renderCache_->limbs.resize(actors_.size());
    renderCache_->labels.clear();
    renderCache_->labels.reserve(actors_.size());
    for (const IconActor& actor : actors_) {
        renderCache_->labels.push_back(BuildCachedLabelBitmap(actor));
    }

    LogInfo(L"icon fight scene reset; actors: " + std::to_wstring(actors_.size()));
    const AwakeningPlanSummary& awakeningSummary = awakeningDirectorState_.summary;
    LogInfo(
        L"awakening waves: total=" + std::to_wstring(awakeningSummary.totalCount) +
        L"; first=" + std::to_wstring(awakeningSummary.firstWaveCount) +
        L" (" + std::to_wstring(awakeningSummary.firstStartMinimum) + L"-" +
        std::to_wstring(awakeningSummary.firstStartMaximum) + L"s); second=" +
        std::to_wstring(awakeningSummary.secondWaveCount) + L" (" +
        std::to_wstring(awakeningSummary.secondStartMinimum) + L"-" +
        std::to_wstring(awakeningSummary.secondStartMaximum) + L"s); fallback=" +
        std::to_wstring(awakeningSummary.fallbackWaveCount) + L" (" +
        std::to_wstring(awakeningSummary.fallbackStartMinimum) + L"-" +
        std::to_wstring(awakeningSummary.fallbackStartMaximum) + L"s)");
    LogInfo(L"icon fight desktop label font: " + DesktopIconLabelFontDescription());
    LogInfo(L"icon fight limb model: distance-driven gait; stance=0.62, locomotion blend; fixed-length 3D two-bone IK; upperArm=0.30 planeSide, forearm=0.32, thigh=0.40, shin=0.41; stride=0.68 planeSide, stepHeight=0.14 planeSide, legDrop=0.994 totalLegLength");
    LogInfo(L"icon fight body model: double-sided icon plane; back face keeps non-mirrored icon UV order");
    LogInfo(std::wstring(L"icon fight render shadows: ") + (RenderShadowsEnabled() ? L"enabled" : L"disabled"));
    LogInfo(std::wstring(L"icon fight debug icon plane: ") + (DebugIconPlaneEnabled() ? L"enabled" : L"disabled"));
    if (previewAction_ != ActionId::None && !turnPreviewEnabled_ && !actors_.empty()) {
        LogInfo(L"action preview enabled: " + std::wstring(ActionIdName(previewAction_)));
        if (actionOrbitCameraEnabled_) {
            LogInfo(L"action orbit camera enabled; duration: 8 animation seconds");
        }
    }
    if (turnPreviewEnabled_ && !actors_.empty()) {
        LogInfo(L"turn preview enabled");
    }
    if (combatPreview_ != CombatScenarioId::None) {
        if (actors_.size() < 2) {
            LogWarning(L"combat preview requires at least two valid actors; preview disabled");
            combatPreview_ = CombatScenarioId::None;
            combatPairState_ = {};
            for (IconActor& actor : actors_) actor.combatPreviewActor = false;
        } else {
            LogInfo(L"combat preview enabled: " + std::wstring(CombatScenarioIdName(combatPreview_)));
        }
    }
    if (combatDirectorEnabled_) {
        const CombatDirectorTuning& tuning = GetCombatDirectorTuning();
        std::array<std::size_t, 5> tendencyCounts{};
        for (const IconActor& actor : actors_) {
            ++tendencyCounts[static_cast<std::size_t>(actor.behaviorProfile.tendency)];
        }
        LogInfo(
            std::wstring(L"combat director ") +
            (combatDirectorDiagnosticsEnabled_ ? L"diagnostic preview" : L"product mode") +
            L" enabled; ActiveEncounterPool has no fixed pair cap; opening=" +
            std::to_wstring(tuning.openingWanderSeconds) +
            L"s; global cooldown=" + std::to_wstring(tuning.globalCooldownMinimumSeconds) +
            L"-" + std::to_wstring(tuning.globalCooldownMaximumSeconds) +
            L"s; actor cooldown=" + std::to_wstring(tuning.actorCooldownSeconds) + L"s");
        LogInfo(
            L"ecosystem tendencies: bold=" + std::to_wstring(tendencyCounts[0]) +
            L"; timid=" + std::to_wstring(tendencyCounts[1]) +
            L"; curious=" + std::to_wstring(tendencyCounts[2]) +
            L"; calm=" + std::to_wstring(tendencyCounts[3]) +
            L"; energetic=" + std::to_wstring(tendencyCounts[4]));
    }
    size_t boundActorCount = 0;
    size_t labelBoundsActorCount = 0;
    for (const IconActor& actor : actors_) {
        if (!actor.usedLabelBoundsFallback &&
            actor.labelBounds.right > actor.labelBounds.left &&
            actor.labelBounds.bottom > actor.labelBounds.top) {
            ++labelBoundsActorCount;
        }
        LogInfo(
            L"icon actor: " + actor.label +
            L" @ " + std::to_wstring(static_cast<int>(actor.baseX)) +
            L"," + std::to_wstring(static_cast<int>(actor.baseY)) +
            L"; plane: " +
            std::to_wstring(static_cast<int>(actor.planeWidth)) +
            L" x " +
            std::to_wstring(static_cast<int>(actor.planeHeight)) +
            L"; source: " +
            actor.planeSizeSource +
            (actor.usedPlaneFallback ? L" (fallback)" : L""));
        LogInfo(
            L"icon actor label bounds: " +
            actor.label +
            L" -> " +
            std::to_wstring(actor.labelBounds.right - actor.labelBounds.left) +
            L" x " +
            std::to_wstring(actor.labelBounds.bottom - actor.labelBounds.top) +
            (actor.usedLabelBoundsFallback ? L" (fallback)" : L" (LVIR_LABEL)"));
        if (actor.iconImage != nullptr) {
            ++boundActorCount;
            LogInfo(
                L"icon actor image bound: " +
                actor.label +
                L" -> " +
                actor.iconImage->sourceIdentifier +
                L" (" +
                std::to_wstring(actor.iconImage->width) +
                L" x " +
                std::to_wstring(actor.iconImage->height) +
                L", " +
                actor.iconImage->extractionMethod +
                L")");
        } else {
            LogInfo(L"icon actor image fallback: " + actor.label);
        }
    }
    LogInfo(
        L"icon images extracted: " +
        std::to_wstring(iconImageCache_.ExtractedCount()) +
        L"; actors bound: " +
        std::to_wstring(boundActorCount) +
        L"; actor fallback: " +
        std::to_wstring(actors_.size() - boundActorCount) +
        L"; extraction failed: " +
        std::to_wstring(iconImageCache_.FailedCount()));
    if (iconImageCache_.FailedCount() > 0) {
        LogWarning(
            L"some icon actor images use fallback bodies; count: " +
            std::to_wstring(iconImageCache_.FailedCount()));
    }
    LogInfo(
        L"icon actor label bounds captured: " +
        std::to_wstring(labelBoundsActorCount) +
        L" / " +
        std::to_wstring(actors_.size()));
}

void IconFightScene::ConfigureCombatStations(
    std::size_t attackerIndex,
    std::size_t defenderIndex,
    CombatScenarioId scenario,
    double centerX,
    double centerY)
{
    const CombatPairPlan& plan = GetCombatPairPlan(scenario);
    const IconActor& attacker = actors_[attackerIndex];
    const IconActor& defender = actors_[defenderIndex];
    const double largestPlaneSide = std::max({
        48.0,
        attacker.planeWidth,
        attacker.planeHeight,
        defender.planeWidth,
        defender.planeHeight,
    });
    const double axisDistance = largestPlaneSide * plan.desiredAxisDistanceScale;
    const double topMargin = largestPlaneSide * 1.1;
    const double bottomMargin = largestPlaneSide * 1.8;
    const double halfDistance = axisDistance * 0.5;
    combatStationLeftX_ = std::clamp(
        centerX - halfDistance,
        static_cast<double>(wanderBounds_.left) + largestPlaneSide * 1.25,
        static_cast<double>(wanderBounds_.right) - largestPlaneSide * 1.25);
    combatStationRightX_ = std::clamp(
        centerX + halfDistance,
        static_cast<double>(wanderBounds_.left) + largestPlaneSide * 1.25,
        static_cast<double>(wanderBounds_.right) - largestPlaneSide * 1.25);
    combatStationY_ = std::clamp(
        centerY,
        static_cast<double>(wanderBounds_.top) + topMargin,
        static_cast<double>(wanderBounds_.bottom) - bottomMargin);
}

void IconFightScene::Update(double elapsedSeconds)
{
    if (automaticInteractionToastStartTick_ != 0 &&
        GetTickCount64() - automaticInteractionToastStartTick_ >=
            kAutomaticInteractionToastMilliseconds) {
        automaticInteractionToast_.clear();
        automaticInteractionToastStartTick_ = 0;
    }
    const double unboundedDeltaSeconds = std::max(0.0, elapsedSeconds - previousElapsedSeconds_);
    const double deltaSeconds = std::clamp(unboundedDeltaSeconds, 0.0, 0.10);
    const double actionDeltaSeconds = std::clamp(unboundedDeltaSeconds, 0.0, 1.0);
    previousElapsedSeconds_ = elapsedSeconds;
    elapsedSeconds_ = elapsedSeconds;
    UpdateAwakeningSchedule();
    if (combatPreview_ != CombatScenarioId::None && actors_.size() >= 2) {
        UpdateCombatPairActors(
            0, 1, combatPreview_, false,
            combatPairState_, combatStationLeftX_, combatStationRightX_, combatStationY_,
            TurnFacing::Right, TurnFacing::Left,
            loggedCombatPhase_, deltaSeconds, actionDeltaSeconds);
    } else if (combatDirectorEnabled_) {
        UpdateCombatDirector(deltaSeconds, actionDeltaSeconds);
    }
    for (IconActor& actor : actors_) {
        const auto updateLocomotionWeight = [&actor, deltaSeconds](double targetWeight) {
            actor.locomotionWeight = BlendTurnLocomotion(
                actor.locomotionWeight, targetWeight, deltaSeconds);
        };
        const double actorTime = elapsedSeconds_ - actor.awakeningStartSeconds;
        if (actor.combatPreviewActor) {
            continue;
        }
        if (actorTime < kAwakeningDurationSeconds + kLimbGrowthDurationSeconds) {
            actor.x = actor.baseX;
            actor.y = actor.baseY;
            actor.locomotionWeight = 0.0;
            continue;
        }
        actor.turnPoseWeight = BlendTurnLocomotion(
            actor.turnPoseWeight, actor.turnMotion.turning ? 0.0 : 1.0, deltaSeconds);
        actor.reservationAvoidanceCooldown = std::max(
            0.0, actor.reservationAvoidanceCooldown - deltaSeconds);

        if (combatDirectorEnabled_ && !activeEncounterPool_.encounters.empty() &&
            !ActiveEncounterPoolOwnsActor(
                activeEncounterPool_, static_cast<std::size_t>(&actor - actors_.data()))) {
            const TurnActorGeometry avoidGeometry =
                BuildTurnActorGeometry(actor.planeWidth, actor.planeHeight);
            const double margin = std::max(28.0, avoidGeometry.maximumHorizontalExtent);
            const CombatAvoidanceDecision avoidance = ComputeActiveEncounterAvoidanceTarget(
                activeEncounterPool_,
                {
                    static_cast<double>(wanderBounds_.left),
                    static_cast<double>(wanderBounds_.top),
                    static_cast<double>(wanderBounds_.right),
                    static_cast<double>(wanderBounds_.bottom),
                },
                {
                    static_cast<std::size_t>(&actor - actors_.data()),
                    actor.x,
                    actor.y,
                    actor.targetX,
                    actor.targetY,
                    margin,
                    actor.reservationAvoidanceCooldown,
                });
            if (avoidance.reselectTarget) {
                actor.targetX = avoidance.targetX;
                actor.targetY = avoidance.targetY;
                actor.waitRemaining = 0.0;
                actor.reservationAvoidanceCooldown =
                    GetCombatDirectorTuning().avoidanceReplanIntervalSeconds;
                ++combatDirectorAvoidanceReplans_;
            }
        }

        if (actor.actionPreviewActor) {
            actor.x = actor.baseX;
            actor.y = actor.baseY;
            updateLocomotionWeight(0.0);
            if (actor.actionOrbitCameraEnabled) {
                actor.actionOrbitElapsedSeconds += actionDeltaSeconds;
            }
            if (actor.actionPlayer.State().actionId == ActionId::None) {
                actor.actionPreviewPauseRemaining = std::max(
                    0.0, actor.actionPreviewPauseRemaining - actionDeltaSeconds);
                if (actor.actionPreviewPauseRemaining <= 0.0) {
                    const double direction = actor.turnMotion.currentFacing == TurnFacing::Left ? -1.0 : 1.0;
                    actor.actionPlayer.Start(previewAction_, direction);
                }
            } else {
                actor.actionPlayer.Update(actionDeltaSeconds);
                actor.actionSample = actor.actionPlayer.Sample();
                actor.actionPlayer.ConsumeEvents();
                if (actor.actionPlayer.IsComplete()) {
                    const ActorActionState completedState = actor.actionPlayer.State();
                    const ActionClip& completedClip = GetActionClip(completedState.actionId);
                    if (completedClip.finalRootDisplacementForward != 0.0) {
                        const double planeSide = std::max(
                            24.0, std::max(actor.planeWidth, actor.planeHeight));
                        const double displacement = completedState.direction *
                            completedClip.finalRootDisplacementForward * planeSide;
                        const double margin = planeSide * 0.75;
                        actor.baseX = std::clamp(
                            actor.baseX + displacement,
                            static_cast<double>(wanderBounds_.left) + margin,
                            static_cast<double>(wanderBounds_.right) - margin);
                        actor.x = actor.baseX;
                    }
                    actor.actionPlayer.Stop();
                    actor.actionSample = {};
                    actor.actionPreviewPauseRemaining = 0.55;
                }
            }
            continue;
        }
        if (actor.turnPreviewActor) {
            actor.x = actor.baseX;
            actor.y = actor.baseY;
            updateLocomotionWeight(0.0);
            if (actor.turnMotion.turning) {
                UpdateTurnMotion(actor.turnMotion, actionDeltaSeconds);
                if (!actor.turnMotion.turning) {
                    actor.turnPreviewPauseRemaining = 0.65;
                }
            } else {
                actor.turnPreviewPauseRemaining = std::max(
                    0.0, actor.turnPreviewPauseRemaining - actionDeltaSeconds);
                if (actor.turnPreviewPauseRemaining <= 0.0) {
                    const TurnFacing nextFacing = actor.turnMotion.currentFacing == TurnFacing::Right ?
                        TurnFacing::Left : TurnFacing::Right;
                    RequestTurn(actor.turnMotion, nextFacing);
                }
            }
            continue;
        }
        const std::size_t actorIndex = static_cast<std::size_t>(&actor - actors_.data());
        if (combatDirectorEnabled_ && actorIndex < eventReactionStates_.size()) {
            const ActorEventReactionState& reaction = eventReactionStates_[actorIndex];
            const auto advanceReactionMovement = [&](double targetX, double targetY, double speedScale) {
                const double dx = targetX - actor.x;
                const double dy = targetY - actor.y;
                const double distance = std::hypot(dx, dy);
                if (distance <= 1.5) {
                    actor.x = targetX;
                    actor.y = targetY;
                    updateLocomotionWeight(0.0);
                    return true;
                }
                if (actor.turnMotion.turning) {
                    updateLocomotionWeight(0.0);
                    if (actor.locomotionWeight <= 0.02) {
                        UpdateTurnMotion(actor.turnMotion, actionDeltaSeconds);
                    }
                    return false;
                }
                if (RequestTurn(actor.turnMotion, FacingFromDirection(dx))) {
                    updateLocomotionWeight(0.0);
                    return false;
                }
                updateLocomotionWeight(1.0);
                const double move = std::min(
                    distance, actor.walkSpeed * speedScale * deltaSeconds);
                actor.x += dx / distance * move;
                actor.y += dy / distance * move;
                const double planeSide = std::max(
                    24.0, std::max(actor.planeWidth, actor.planeHeight));
                const GaitGeometry geometry = BuildGaitGeometry(
                    planeSide, planeSide * 0.40, planeSide * 0.41);
                actor.walkPhase = WrapGaitPhase(
                    actor.walkPhase + move / geometry.cycleTravel);
                return false;
            };
            const auto faceReactionCenter = [&]() {
                const double dx = reaction.eventCenter.x - actor.x;
                updateLocomotionWeight(0.0);
                if (actor.turnMotion.turning) {
                    if (actor.locomotionWeight <= 0.02) {
                        UpdateTurnMotion(actor.turnMotion, actionDeltaSeconds);
                    }
                } else if (std::abs(dx) > 1.0) {
                    RequestTurn(actor.turnMotion, FacingFromDirection(dx));
                }
            };

            if (reaction.kind == ActorEventReactionKind::Observing && reaction.targetValid) {
                if (advanceReactionMovement(reaction.target.x, reaction.target.y, 0.82)) {
                    faceReactionCenter();
                }
                continue;
            }
            if (reaction.kind == ActorEventReactionKind::Avoiding && reaction.targetValid) {
                advanceReactionMovement(
                    reaction.target.x, reaction.target.y,
                    actor.behaviorProfile.tendency == ActorTendency::Timid ? 1.18 : 1.0);
                continue;
            }
            if (reaction.kind == ActorEventReactionKind::Glancing && !reaction.keepMoving) {
                faceReactionCenter();
                continue;
            }
        }
        if (actor.turnMotion.turning) {
            updateLocomotionWeight(0.0);
            if (actor.locomotionWeight <= 0.02) {
                UpdateTurnMotion(actor.turnMotion, actionDeltaSeconds);
            }
            continue;
        }
        if (actor.waitRemaining > 0.0) {
            updateLocomotionWeight(0.0);
            actor.waitRemaining = std::max(0.0, actor.waitRemaining - deltaSeconds);
            if (actor.waitRemaining <= 0.0) {
                ChooseWanderTarget(actor);
            }
            continue;
        }

        const double dx = actor.targetX - actor.x;
        const double dy = actor.targetY - actor.y;
        const double distance = std::sqrt((dx * dx) + (dy * dy));
        const double step = actor.walkSpeed * deltaSeconds;
        if (distance <= std::max(1.0, step)) {
            const double planeSide = std::max(24.0, std::max(actor.planeWidth, actor.planeHeight));
            const GaitGeometry gaitGeometry = BuildGaitGeometry(
                planeSide, planeSide * 0.40, planeSide * 0.41);
            actor.walkPhase = WrapGaitPhase(actor.walkPhase + distance / gaitGeometry.cycleTravel);
            actor.x = actor.targetX;
            actor.y = actor.targetY;
            updateLocomotionWeight(0.0);
            actor.randomState = actor.randomState * 1664525u + 1013904223u;
            actor.waitRemaining = 0.20 + (static_cast<double>(actor.randomState % 1001u) / 1000.0);
        } else {
            const TurnFacing desiredFacing = FacingFromDirection(dx);
            if (RequestTurn(actor.turnMotion, desiredFacing)) {
                updateLocomotionWeight(0.0);
                continue;
            }
            updateLocomotionWeight(1.0);
            actor.x += (dx / distance) * step;
            actor.y += (dy / distance) * step;
            const double planeSide = std::max(24.0, std::max(actor.planeWidth, actor.planeHeight));
            const GaitGeometry gaitGeometry = BuildGaitGeometry(
                planeSide, planeSide * 0.40, planeSide * 0.41);
            actor.walkPhase = WrapGaitPhase(actor.walkPhase + step / gaitGeometry.cycleTravel);
        }
    }
    const ScenePhase nextPhase = DeterminePhase(elapsedSeconds_);
    if (nextPhase != phase_) {
        phase_ = nextPhase;
        LogPhase(phase_);
    }
}

void IconFightScene::UpdateCombatPairActors(
    std::size_t attackerIndex,
    std::size_t defenderIndex,
    CombatScenarioId scenario,
    bool directorInteraction,
    CombatPairState& pairState,
    double attackerStationX,
    double defenderStationX,
    double stationY,
    TurnFacing attackerFacing,
    TurnFacing defenderFacing,
    CombatPairPhase& loggedPhase,
    double deltaSeconds,
    double actionDeltaSeconds)
{
    IconActor& attacker = actors_[attackerIndex];
    IconActor& defender = actors_[defenderIndex];
    const CombatPairPlan& plan = GetCombatPairPlan(scenario);
    const auto isAwake = [this](const IconActor& actor) {
        const double actorTime = elapsedSeconds_ - actor.awakeningStartSeconds;
        return actorTime >= kAwakeningDurationSeconds + kLimbGrowthDurationSeconds;
    };
    const bool bothAwake = isAwake(attacker) && isAwake(defender);
    if (!bothAwake) {
        for (IconActor* actor : {&attacker, &defender}) {
            actor->x = actor->baseX;
            actor->y = actor->baseY;
            actor->locomotionWeight = 0.0;
            actor->actionSample = {};
        }
    }

    const auto distanceToStation = [](const IconActor& actor, double stationX, double stationY) {
        return std::hypot(stationX - actor.x, stationY - actor.y);
    };
    const bool atStations = distanceToStation(attacker, attackerStationX, stationY) <= 1.5 &&
        distanceToStation(defender, defenderStationX, stationY) <= 1.5;
    const bool aligned = atStations &&
        !attacker.turnMotion.turning && !defender.turnMotion.turning &&
        attacker.turnMotion.currentFacing == attackerFacing &&
        defender.turnMotion.currentFacing == defenderFacing;
    CombatPairReadiness readiness;
    readiness.bothAwake = bothAwake;
    readiness.atStations = atStations;
    readiness.aligned = aligned;
    readiness.actionsComplete = pairState.result != CombatResult::None &&
        attacker.actionPlayer.State().actionId == ActionId::None &&
        defender.actionPlayer.State().actionId == ActionId::None &&
        attacker.pendingCombatAction == ActionId::None &&
        defender.pendingCombatAction == ActionId::None;
    readiness.returnedToStations = !directorInteraction && atStations;
    const CombatPairStep pairStep = UpdateCombatPair(
        pairState, plan, readiness, actionDeltaSeconds);

    const auto blendLocomotion = [deltaSeconds](IconActor& actor, double target) {
        actor.locomotionWeight = BlendTurnLocomotion(actor.locomotionWeight, target, deltaSeconds);
    };
    const auto advanceGait = [](IconActor& actor, double distance) {
        const double planeSide = std::max(24.0, std::max(actor.planeWidth, actor.planeHeight));
        const GaitGeometry geometry = BuildGaitGeometry(planeSide, planeSide * 0.40, planeSide * 0.41);
        actor.walkPhase = WrapGaitPhase(actor.walkPhase + distance / geometry.cycleTravel);
    };
    const auto moveTo = [&](IconActor& actor, double targetX, double targetY) {
        const double dx = targetX - actor.x;
        const double dy = targetY - actor.y;
        const double distance = std::hypot(dx, dy);
        if (distance <= 1.5) {
            actor.x = targetX;
            actor.y = targetY;
            blendLocomotion(actor, 0.0);
            return;
        }
        const TurnFacing desiredFacing = FacingFromDirection(dx);
        if (actor.turnMotion.turning) {
            blendLocomotion(actor, 0.0);
            if (actor.locomotionWeight <= 0.02) UpdateTurnMotion(actor.turnMotion, actionDeltaSeconds);
            return;
        }
        if (RequestTurn(actor.turnMotion, desiredFacing)) {
            blendLocomotion(actor, 0.0);
            return;
        }
        blendLocomotion(actor, 1.0);
        const double step = std::min(distance, actor.walkSpeed * deltaSeconds);
        actor.x += dx / distance * step;
        actor.y += dy / distance * step;
        advanceGait(actor, step);
    };

    if (!directorInteraction && (pairState.phase == CombatPairPhase::Approaching ||
        pairState.phase == CombatPairPhase::Returning)) {
        moveTo(attacker, attackerStationX, stationY);
        moveTo(defender, defenderStationX, stationY);
    } else if (!directorInteraction && pairState.phase == CombatPairPhase::Aligning) {
        blendLocomotion(attacker, 0.0);
        blendLocomotion(defender, 0.0);
        if (attacker.locomotionWeight <= 0.02) {
            if (!attacker.turnMotion.turning) RequestTurn(attacker.turnMotion, attackerFacing);
            UpdateTurnMotion(attacker.turnMotion, actionDeltaSeconds);
        }
        if (defender.locomotionWeight <= 0.02) {
            if (!defender.turnMotion.turning) RequestTurn(defender.turnMotion, defenderFacing);
            UpdateTurnMotion(defender.turnMotion, actionDeltaSeconds);
        }
    } else {
        blendLocomotion(attacker, 0.0);
        blendLocomotion(defender, 0.0);
    }

    const auto startAction = [](IconActor& actor, ActionId action) {
        const double direction = actor.turnMotion.currentFacing == TurnFacing::Left ? -1.0 : 1.0;
        actor.actionPlayer.Start(action, direction);
        actor.actionSample = actor.actionPlayer.Sample();
        actor.combatBlendElapsed = 0.0;
        actor.combatBlendDuration = 0.0;
    };
    if (pairStep.startAttackerAction) {
        attacker.combatImpactVisible = false;
        attacker.combatBlockedImpact = false;
        startAction(attacker, plan.attackerAction);
    }
    if (pairStep.startDefenderAction) startAction(defender, plan.defenderAction);

    const auto updateAction = [&](IconActor& actor, bool startedThisStep, double startTime) {
        if (actor.actionPlayer.State().actionId == ActionId::None) return;
        const double advance = startedThisStep ?
            std::max(0.0, pairState.interactionTime - startTime) : actionDeltaSeconds;
        actor.actionPlayer.Update(advance);
        actor.actionSample = actor.actionPlayer.Sample();
        actor.actionPlayer.ConsumeEvents();
        if (actor.combatBlendDuration > 0.0) {
            actor.combatBlendElapsed = std::min(
                actor.combatBlendDuration,
                actor.combatBlendElapsed + advance);
            const double blendWeight = SmoothStep(
                0.0, actor.combatBlendDuration, actor.combatBlendElapsed);
            actor.actionSample = BlendActionSamples(
                actor.combatBlendFrom, actor.actionSample, blendWeight);
            if (actor.combatBlendElapsed >= actor.combatBlendDuration) {
                actor.combatBlendDuration = 0.0;
            }
        }
    };
    if (pairState.phase == CombatPairPhase::Exchanging ||
        pairState.phase == CombatPairPhase::Recovering) {
        updateAction(attacker, pairStep.startAttackerAction, plan.attackerStartTime);
        updateAction(defender, pairStep.startDefenderAction, plan.defenderStartTime);
    }

    if (pairStep.resolveContact) {
        const double attackerDirection = attacker.turnMotion.currentFacing == TurnFacing::Left ? -1.0 : 1.0;
        const double defenderDirection = defender.turnMotion.currentFacing == TurnFacing::Left ? -1.0 : 1.0;
        IconActor contactAttacker = attacker;
        IconActor contactDefender = defender;
        contactAttacker.actionSample = SampleAction(
            GetActionClip(plan.attackerAction),
            plan.expectedContactTime - plan.attackerStartTime,
            attackerDirection);
        if (plan.defenderAction != ActionId::None) {
            contactDefender.actionSample = SampleAction(
                GetActionClip(plan.defenderAction),
                plan.expectedContactTime - plan.defenderStartTime,
                defenderDirection);
        }
        const ActorPose attackerPose = BuildPose(contactAttacker, elapsedSeconds_);
        const ActorPose defenderPose = BuildPose(contactDefender, elapsedSeconds_);
        const LimbPose attackerLimbs = BuildLimbPose(contactAttacker, attackerPose);
        const LimbPose defenderLimbs = BuildLimbPose(contactDefender, defenderPose);
        const bool attackerRightLead = attackerPose.facing > 0.0;
        const JointChain& attackChain = GetActionClip(plan.attackerAction).attackType == ActionAttackType::Kick ?
            (attackerRightLead ? attackerLimbs.rightLeg : attackerLimbs.leftLeg) :
            (attackerRightLead ? attackerLimbs.rightArm : attackerLimbs.leftArm);
        const CombatPoint shoulderCenter{
            (defenderLimbs.leftArm.root.X + defenderLimbs.rightArm.root.X) * 0.5,
            (defenderLimbs.leftArm.root.Y + defenderLimbs.rightArm.root.Y) * 0.5,
        };
        const CombatPoint hipCenter{
            (defenderLimbs.leftLeg.root.X + defenderLimbs.rightLeg.root.X) * 0.5,
            (defenderLimbs.leftLeg.root.Y + defenderLimbs.rightLeg.root.Y) * 0.5,
        };
        const double planeSide = std::max({
            24.0,
            attacker.planeWidth,
            attacker.planeHeight,
            defender.planeWidth,
            defender.planeHeight,
        });
        CombatContactProbe probe;
        probe.attackPoint = {attackChain.end.X, attackChain.end.Y};
        probe.attackRadius = planeSide * 0.13;
        probe.attackType = GetActionClip(plan.attackerAction).attackType == ActionAttackType::Kick ?
            CombatAttackType::Kick : CombatAttackType::Punch;
        probe.hitStrength = plan.contactStrength;
        probe.attackDirection = {attackerDirection, 0.0};
        probe.targetAxisTop = shoulderCenter;
        probe.targetAxisBottom = hipCenter;
        probe.targetRadius = planeSide * 0.35;
        probe.actorAxisDistance = std::abs(defender.x - attacker.x);
        probe.maximumAxisDistance = planeSide * (plan.desiredAxisDistanceScale + 0.18);
        probe.defenseWindow = plan.defenderAction == ActionId::None ?
            ActionDefenseWindowType::None :
            ActionDefenseWindowAt(
                GetActionClip(plan.defenderAction),
                plan.expectedContactTime - plan.defenderStartTime);
        const CombatResult actualResult = ResolveCombatContact(probe);
        attacker.combatImpactVisible = actualResult == CombatResult::HitLight ||
            actualResult == CombatResult::HitHeavy;
        attacker.combatBlockedImpact = actualResult == CombatResult::Blocked;
        ApplyCombatResult(pairState, actualResult);
        if (actualResult == CombatResult::HitLight) {
            startAction(defender, ActionId::LightHitReact);
            pairState.resultActionStarted = true;
        } else if (actualResult == CombatResult::HitHeavy) {
            startAction(defender, ActionId::HeavyStagger);
            pairState.resultActionStarted = true;
        } else if (actualResult == CombatResult::Evaded || actualResult == CombatResult::Whiffed) {
            // Enter the already-overextended portion of WhiffRecovery directly
            // from the missed Contact pose. Waiting for the punch to complete
            // first makes the actor return to guard and visually throw a second
            // empty punch. The short director blend changes only the transition;
            // both frozen single-actor clips remain untouched.
            const ActionSample missedContactPose = attacker.actionSample;
            startAction(attacker, ActionId::WhiffRecovery);
            attacker.actionPlayer.Update(plan.whiffEntryTime);
            attacker.actionPlayer.ConsumeEvents();
            attacker.combatBlendFrom = missedContactPose;
            attacker.combatBlendElapsed = 0.0;
            attacker.combatBlendDuration = plan.transitionBlendDuration;
            attacker.actionSample = missedContactPose;
            pairState.resultActionStarted = true;
        }
        const double attackToAxis = DistancePointToSegment(
            probe.attackPoint, probe.targetAxisTop, probe.targetAxisBottom);
        LogInfo(
            L"combat contact: expected=" + std::wstring(CombatResultName(plan.expectedResult)) +
            L", actual=" + std::wstring(CombatResultName(actualResult)) +
            L", axisDistance=" + std::to_wstring(probe.actorAxisDistance) +
            L", attackPoint=" + std::to_wstring(probe.attackPoint.x) + L"," +
            std::to_wstring(probe.attackPoint.y) +
            L", targetRadius=" + std::to_wstring(probe.targetRadius) +
            L", attackToAxis=" + std::to_wstring(attackToAxis) +
            L", defense=" + std::to_wstring(static_cast<int>(probe.defenseWindow)) +
            L", consumed=yes");
        if (actualResult != plan.expectedResult) {
            LogWarning(L"combat preview result differs from scenario expectation");
        }
    }

    const auto finishAction = [&](IconActor& actor) {
        if (actor.actionPlayer.State().actionId == ActionId::None || !actor.actionPlayer.IsComplete()) return;
        const ActorActionState completed = actor.actionPlayer.State();
        const ActionClip& clip = GetActionClip(completed.actionId);
        if (clip.finalRootDisplacementForward != 0.0) {
            const double planeSide = std::max(24.0, std::max(actor.planeWidth, actor.planeHeight));
            const double margin = planeSide * 1.25;
            actor.x = std::clamp(
                actor.x + completed.direction * clip.finalRootDisplacementForward * planeSide,
                static_cast<double>(wanderBounds_.left) + margin,
                static_cast<double>(wanderBounds_.right) - margin);
            actor.baseX = actor.x;
        }
        actor.actionPlayer.Stop();
        actor.actionSample = {};
        if (actor.pendingCombatAction != ActionId::None) {
            const ActionId pending = actor.pendingCombatAction;
            actor.pendingCombatAction = ActionId::None;
            startAction(actor, pending);
            pairState.resultActionStarted = true;
        }
    };
    finishAction(attacker);
    finishAction(defender);

    if (pairState.phase != loggedPhase) {
        loggedPhase = pairState.phase;
        LogInfo(L"combat pair phase: " + std::wstring(CombatPairPhaseName(loggedPhase)));
    }
}

void IconFightScene::UpdateAwakeningSchedule()
{
    for (std::size_t index = 0; index < actors_.size(); ++index) {
        const IconActor& actor = actors_[index];
        const double actorTime = elapsedSeconds_ - actor.awakeningStartSeconds;
        const bool fullyGrown = actorTime >= kAwakeningDurationSeconds + kLimbGrowthDurationSeconds;
        awakeningObservations_[index] = {
            index,
            actor.x,
            actor.y,
            std::max(actor.planeWidth, actor.planeHeight),
            fullyGrown &&
                !actor.actionPreviewActor &&
                !actor.turnPreviewActor &&
                !actor.combatPreviewActor &&
                actor.actionPlayer.State().actionId == ActionId::None,
        };
    }

    if (UpdateAwakeningProximity(
            awakeningDirectorState_, awakeningObservations_, elapsedSeconds_)) {
        for (std::size_t index = 0; index < actors_.size(); ++index) {
            actors_[index].awakeningStartSeconds =
                GetActorAwakeningStartSeconds(awakeningDirectorState_, index);
        }
    }

    if (!awakeningReadinessLogged_ &&
        IsFirstWaveEcosystemReady(awakeningDirectorState_, awakeningObservations_)) {
        awakeningReadinessLogged_ = true;
        LogInfo(
            L"awakening first-wave ecosystem ready at " +
            std::to_wstring(elapsedSeconds_) + L"s");
    }
    if (!awakeningCompletionLogged_ && !actors_.empty() &&
        elapsedSeconds_ >= LatestAwakeningStartSeconds(awakeningDirectorState_)) {
        awakeningCompletionLogged_ = true;
        LogInfo(
            L"awakening final actor started at " + std::to_wstring(elapsedSeconds_) +
            L"s; proximity accelerations=" +
            std::to_wstring(awakeningDirectorState_.proximityAccelerationCount));
    }
}

#include "besktop/animation/icon_fight_scene_active_pool.inl"

void IconFightScene::ChooseWanderTargetAwayFrom(IconActor& actor, double avoidX, double direction)
{
    ChooseWanderTarget(actor);
    const TurnActorGeometry geometry = BuildTurnActorGeometry(actor.planeWidth, actor.planeHeight);
    const double sideMargin = std::max(28.0, geometry.maximumHorizontalExtent + geometry.visibleMargin);
    const double minimumSeparation = geometry.planeSide * 2.4;
    const double desiredX = avoidX + direction * minimumSeparation;
    if ((actor.targetX - avoidX) * direction < minimumSeparation) {
        actor.targetX = std::clamp(
            desiredX,
            static_cast<double>(wanderBounds_.left) + sideMargin,
            static_cast<double>(wanderBounds_.right) - sideMargin);
    }
    actor.waitRemaining = 0.0;
}

void IconFightScene::ChooseWanderTarget(IconActor& actor)
{
    const TurnActorGeometry turnGeometry =
        BuildTurnActorGeometry(actor.planeWidth, actor.planeHeight);
    const double planeSide = turnGeometry.planeSide;
    const double sideMargin = std::max(
        std::max(28.0, planeSide * 1.25),
        turnGeometry.maximumHorizontalExtent + turnGeometry.visibleMargin);
    const double topMargin = std::max(24.0, planeSide * 0.80);
    const double bottomMargin = usingCapturedWorkArea_ ?
        std::max(40.0, planeSide * 1.65) :
        std::max(72.0, planeSide * 1.85);
    const double minX = wanderBounds_.left + sideMargin;
    const double maxX = wanderBounds_.right - sideMargin;
    const double minY = wanderBounds_.top + topMargin;
    const double maxY = wanderBounds_.bottom - bottomMargin;

    const auto nextUnit = [&actor]() {
        actor.randomState = actor.randomState * 1664525u + 1013904223u;
        return static_cast<double>(actor.randomState & 0x00FFFFFFu) / 16777215.0;
    };
    for (int attempt = 0; attempt < 5; ++attempt) {
        actor.targetX = maxX > minX ? minX + (maxX - minX) * nextUnit() : (wanderBounds_.left + wanderBounds_.right) * 0.5;
        actor.targetY = maxY > minY ? minY + (maxY - minY) * nextUnit() : (wanderBounds_.top + wanderBounds_.bottom) * 0.5;
        if (!IsInsideAnyActiveEncounterReservation(
                activeEncounterPool_, actor.targetX, actor.targetY, planeSide * 0.40)) {
            break;
        }
    }
}

void IconFightScene::Render(HDC hdc, const RECT& clientRect, RenderTimings* timings) const
{
    Gdiplus::Graphics graphics(hdc);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    graphics.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);
    graphics.SetInterpolationMode(Gdiplus::InterpolationModeBilinear);
    graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);

    const double width = clientRect.right - clientRect.left;
    const double height = clientRect.bottom - clientRect.top;

    const double bannerAlpha =
        elapsedSeconds_ < 3.2 ? (SmoothStep(0.45, 0.85, elapsedSeconds_) * (1.0 - SmoothStep(2.25, 3.20, elapsedSeconds_))) : 0.0;
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

    const LONGLONG poseStart = timings != nullptr ? PerformanceCounterNow() : 0;
    for (size_t index = 0; index < actors_.size(); ++index) {
        poseCache_[index] = BuildPose(actors_[index], elapsedSeconds_);
        if (actors_[index].actionOrbitCameraEnabled) {
            poseCache_[index].observationOrbitYaw = SampleObservationOrbitYaw(
                actors_[index].actionOrbitElapsedSeconds);
        }
    }
    const LONGLONG actorPrepStart = timings != nullptr ? PerformanceCounterNow() : 0;
    for (size_t index = 0; index < actors_.size(); ++index) {
        renderCache_->bodies[index] = BuildBodyProjection(actors_[index], poseCache_[index]);
        renderCache_->limbs[index] = BuildLimbPose(actors_[index], poseCache_[index]);
    }
    if (timings != nullptr) {
        const LONGLONG end = PerformanceCounterNow();
        timings->poseMs = CounterMilliseconds(poseStart, actorPrepStart);
        timings->actorPrepMs = CounterMilliseconds(actorPrepStart, end);
    }

    const TurnActorGeometry sharedTurnGeometry = actors_.empty() ?
        BuildTurnActorGeometry(48.0, 48.0) :
        BuildTurnActorGeometry(actors_.front().planeWidth, actors_.front().planeHeight);
    const float sharedLimbWidth = ToFloat(sharedTurnGeometry.limbWidth);
    Gdiplus::Pen sharedBackLimb(Gdiplus::Color(178, 250, 253, 255), sharedLimbWidth);
    sharedBackLimb.SetStartCap(Gdiplus::LineCapRound);
    sharedBackLimb.SetEndCap(Gdiplus::LineCapRound);
    Gdiplus::Pen sharedFrontLimb(Gdiplus::Color(248, 250, 253, 255), sharedLimbWidth);
    sharedFrontLimb.SetStartCap(Gdiplus::LineCapRound);
    sharedFrontLimb.SetEndCap(Gdiplus::LineCapRound);

    for (size_t index = 0; index < actors_.size(); ++index) {
        const IconActor& actor = actors_[index];
        const ActorPose& pose = poseCache_[index];
        const LONGLONG limbStart = timings != nullptr ? PerformanceCounterNow() : 0;
        DrawActorLimbs(
            graphics,
            actor,
            pose,
            renderCache_->bodies[index],
            renderCache_->limbs[index],
            LimbLayer::Back,
            &sharedBackLimb,
            sharedLimbWidth);
        if (timings != nullptr) {
            const LONGLONG end = PerformanceCounterNow();
            timings->limbsMs += CounterMilliseconds(limbStart, end);
        }
    }
    for (size_t index = 0; index < actors_.size(); ++index) {
        const IconActor& actor = actors_[index];
        const ActorPose& pose = poseCache_[index];
        const LONGLONG bodyStart = timings != nullptr ? PerformanceCounterNow() : 0;
        DrawActorBody(graphics, actor, pose, renderCache_->bodies[index]);
        const LONGLONG labelStart = timings != nullptr ? PerformanceCounterNow() : 0;
        DrawActorLabel(
            graphics, actor, pose, renderCache_->bodies[index], &renderCache_->labels[index], elapsedSeconds_);
        if (timings != nullptr) {
            const LONGLONG end = PerformanceCounterNow();
            timings->iconBodyMs += CounterMilliseconds(bodyStart, labelStart);
            timings->labelMs += CounterMilliseconds(labelStart, end);
        }
    }

    for (size_t index = 0; index < actors_.size(); ++index) {
        const IconActor& actor = actors_[index];
        const ActorPose& pose = poseCache_[index];
        const LONGLONG limbStart = timings != nullptr ? PerformanceCounterNow() : 0;
        DrawActorLimbs(
            graphics,
            actor,
            pose,
            renderCache_->bodies[index],
            renderCache_->limbs[index],
            LimbLayer::Front,
            &sharedFrontLimb,
            sharedLimbWidth);
        if (timings != nullptr) {
            const LONGLONG end = PerformanceCounterNow();
            timings->limbsMs += CounterMilliseconds(limbStart, end);
        }
    }

    Gdiplus::FontFamily fontFamily(L"Microsoft YaHei UI");
    if (!automaticInteractionToast_.empty()) {
        const ULONGLONG now = GetTickCount64();
        const ULONGLONG elapsed = now >= automaticInteractionToastStartTick_ ?
            now - automaticInteractionToastStartTick_ : kAutomaticInteractionToastMilliseconds;
        if (elapsed < kAutomaticInteractionToastMilliseconds) {
            const double remaining = static_cast<double>(
                kAutomaticInteractionToastMilliseconds - elapsed);
            const double fade = std::clamp(remaining / 420.0, 0.0, 1.0);
            const double workWidth = std::max(
                0.0, static_cast<double>(wanderBounds_.right - wanderBounds_.left));
            const float toastWidth = ToFloat(std::clamp(workWidth - 40.0, 220.0, 360.0));
            const float toastHeight = 42.0f;
            const float toastX = ToFloat(std::max(
                static_cast<double>(wanderBounds_.left) + 20.0,
                static_cast<double>(wanderBounds_.right) - toastWidth - 20.0));
            const float toastY = ToFloat(static_cast<double>(wanderBounds_.top) + 20.0);
            const unsigned char backgroundAlpha = static_cast<unsigned char>(150.0 * fade);
            const unsigned char textAlpha = static_cast<unsigned char>(240.0 * fade);
            Gdiplus::SolidBrush backgroundBrush(Gdiplus::Color(backgroundAlpha, 18, 24, 34));
            graphics.FillRectangle(&backgroundBrush, toastX, toastY, toastWidth, toastHeight);
            Gdiplus::Font toastFont(&fontFamily, 16.0f, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
            DrawCenteredString(
                graphics,
                automaticInteractionToast_,
                Gdiplus::RectF(toastX, toastY + 8.0f, toastWidth, 24.0f),
                toastFont,
                Gdiplus::Color(textAlpha, 255, 255, 255));
        }
    }
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
    bool anyStarted = false;
    bool anyAwakening = false;
    bool allFullyGrown = !actors_.empty();
    for (const IconActor& actor : actors_) {
        const double actorTime = elapsedSeconds - actor.awakeningStartSeconds;
        anyStarted = anyStarted || actorTime >= 0.0;
        anyAwakening = anyAwakening || (actorTime >= 0.0 && actorTime < kAwakeningDurationSeconds);
        allFullyGrown = allFullyGrown &&
            actorTime >= kAwakeningDurationSeconds + kLimbGrowthDurationSeconds;
    }
    if (!anyStarted) return ScenePhase::Sleeping;
    if (anyAwakening) return ScenePhase::Awakening;
    return allFullyGrown ? ScenePhase::Wandering : ScenePhase::GrowingLimbs;
}

void IconFightScene::LogPhase(ScenePhase phase)
{
    LogInfo(std::wstring(L"icon fight scene phase: ") + PhaseName(phase));
}

} // namespace besktop
