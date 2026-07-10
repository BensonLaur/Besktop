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
constexpr size_t kMaxDesktopActors = 9;

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

bool IsTruthyEnvironmentFlag(const wchar_t* name)
{
    wchar_t value[16]{};
    const DWORD length = GetEnvironmentVariableW(name, value, static_cast<DWORD>(std::size(value)));
    if (length == 0 || length >= std::size(value)) {
        return false;
    }

    return value[0] == L'1' ||
        value[0] == L't' ||
        value[0] == L'T' ||
        value[0] == L'y' ||
        value[0] == L'Y' ||
        value[0] == L'o' ||
        value[0] == L'O';
}

bool RenderShadowsEnabled()
{
    static const bool enabled = IsTruthyEnvironmentFlag(L"BESKTOP_RENDER_SHADOWS");
    return enabled;
}

bool DebugIconPlaneEnabled()
{
    static const bool enabled = IsTruthyEnvironmentFlag(L"BESKTOP_DEBUG_ICON_PLANE");
    return enabled;
}

double LerpValue(double from, double to, double t);
double Wrap01(double value);

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

struct ActorPose {
    enum class Heading {
        MoveRight,
        MoveLeft,
        FacingOut,
    };

    double x = 0.0;
    double y = 0.0;
    double bob = 0.0;
    double bodyEffect = 0.0;
    double rotateX = 0.0;
    double rotateY = 0.0;
    double rotateZ = 0.0;
    double facing = 1.0;
    double punch = 0.0;
    double kick = 0.0;
    double dodge = 0.0;
    double hit = 0.0;
    double limbGrow = 0.0;
    double labelAlpha = 1.0;
    double gait = 0.0;
    double walkPhase = 0.0;
    Heading heading = Heading::MoveRight;
    bool attackingRight = true;
};

enum class LimbLayer {
    Back,
    Front,
};

struct WalkLegSample {
    double phase = 0.0;
    bool stance = true;
    double footOffset = 0.0;
    double footLift = 0.0;
    double plantWeight = 1.0;
};

WalkLegSample SampleWalkLeg(double walkPhase, double phaseOffset, double stride, double stepHeight)
{
    constexpr double kStanceRatio = 0.60;
    WalkLegSample sample;
    sample.phase = Wrap01(walkPhase + phaseOffset);
    sample.stance = sample.phase < kStanceRatio;

    if (sample.stance) {
        const double t = sample.phase / kStanceRatio;
        sample.footOffset = LerpValue(stride * 0.45, -stride * 0.45, t);
        sample.footLift = 0.0;
        sample.plantWeight = 1.0;
        return sample;
    }

    const double swingT = (sample.phase - kStanceRatio) / (1.0 - kStanceRatio);
    sample.footOffset = LerpValue(-stride * 0.45, stride * 0.45, SmoothStep(0.0, 1.0, swingT));
    sample.footLift = std::sin(swingT * kPi) * stepHeight;
    sample.plantWeight = 0.0;
    return sample;
}

ActorPose BuildPose(const besktop::IconFightScene::IconActor& actor, double elapsedSeconds)
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

    const WalkLegSample rightStep = SampleWalkLeg(pose.walkPhase, 0.0, 1.0, 1.0);
    const WalkLegSample leftStep = SampleWalkLeg(pose.walkPhase, 0.5, 1.0, 1.0);
    const double doublePlant = std::clamp(rightStep.plantWeight + leftStep.plantWeight - 1.0, 0.0, 1.0);
    const double swingLift = std::max(rightStep.footLift, leftStep.footLift);
    const double bobAmplitude = std::clamp(planeSide * 0.035, 2.0, 4.5);
    pose.x = LerpValue(actor.baseX, patrolX, intro);
    pose.y = LerpValue(actor.baseY, centerY, intro);
    pose.bob = ((doublePlant * 0.70) - (swingLift * 0.85)) * bobAmplitude * pose.limbGrow;
    pose.rotateX += DegreesToRadians(std::cos(pose.walkPhase * 2.0 * kPi) * 2.4 * pose.limbGrow);
    pose.rotateY += DegreesToRadians(rotateYDegrees * pose.limbGrow);
    pose.rotateZ += DegreesToRadians(headingSign * std::sin(pose.walkPhase * 2.0 * kPi) * 2.8 * pose.limbGrow);
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

struct Vec3 {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

struct ProjectedPoint {
    Gdiplus::PointF point{};
    double depth = 0.0;
};

Vec3 RotateActorVector(const Vec3& local, const ActorPose& pose)
{
    const double cosX = std::cos(pose.rotateX);
    const double sinX = std::sin(pose.rotateX);
    const double cosY = std::cos(pose.rotateY);
    const double sinY = std::sin(pose.rotateY);
    const double cosZ = std::cos(pose.rotateZ);
    const double sinZ = std::sin(pose.rotateZ);

    const double xAfterY = local.x * cosY + local.z * sinY;
    const double zAfterY = -local.x * sinY + local.z * cosY;
    const double yAfterX = local.y * cosX - zAfterY * sinX;
    const double zAfterX = local.y * sinX + zAfterY * cosX;

    return Vec3{
        xAfterY * cosZ - yAfterX * sinZ,
        xAfterY * sinZ + yAfterX * cosZ,
        zAfterX,
    };
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
    const double planeSide = std::max(rawWidth, rawHeight);
    const double halfW = rawWidth * 0.5;
    const double halfH = rawHeight * 0.5;

    BodyProjection projection;
    projection.centerX = pose.x;
    projection.centerY = pose.y + pose.bob;
    projection.points[0] = ProjectActorPoint(Vec3{-halfW, -halfH, 0.0}, pose, planeSide).point;
    projection.points[1] = ProjectActorPoint(Vec3{halfW, -halfH, 0.0}, pose, planeSide).point;
    projection.points[2] = ProjectActorPoint(Vec3{halfW, halfH, 0.0}, pose, planeSide).point;
    projection.points[3] = ProjectActorPoint(Vec3{-halfW, halfH, 0.0}, pose, planeSide).point;

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

bool IsRightSideFront(ActorPose::Heading heading)
{
    switch (heading) {
    case ActorPose::Heading::MoveLeft:
        return false;
    case ActorPose::Heading::MoveRight:
    case ActorPose::Heading::FacingOut:
    default:
        return true;
    }
}

double HeadingSign(const ActorPose& pose)
{
    if (pose.heading == ActorPose::Heading::MoveLeft) {
        return -1.0;
    }
    if (pose.heading == ActorPose::Heading::MoveRight) {
        return 1.0;
    }
    return pose.facing < 0.0 ? -1.0 : 1.0;
}

double Wrap01(double value)
{
    double wrapped = std::fmod(value, 1.0);
    if (wrapped < 0.0) {
        wrapped += 1.0;
    }
    return wrapped;
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
    double bendSign,
    double kneeDepth)
{
    const double dx = target.x - root.x;
    const double dy = target.y - root.y;
    double distance = std::sqrt((dx * dx) + (dy * dy));
    double ux = 0.0;
    double uy = 1.0;
    if (distance > 0.001) {
        ux = dx / distance;
        uy = dy / distance;
    } else {
        distance = 0.001;
    }

    const double minReach = std::max(1.0, std::abs(firstLength - secondLength) + 0.5);
    const double maxReach = std::max(minReach + 0.5, firstLength + secondLength - 0.5);
    const double clampedDistance = std::clamp(distance, minReach, maxReach);
    const double base = std::clamp(
        ((firstLength * firstLength) - (secondLength * secondLength) + (clampedDistance * clampedDistance)) /
            (2.0 * clampedDistance),
        0.0,
        firstLength);
    const double height = std::sqrt(std::max(0.0, (firstLength * firstLength) - (base * base)));
    const double px = -uy;
    const double py = ux;

    LocalJointChain chain;
    chain.root = root;
    chain.end = Vec3{
        root.x + (ux * clampedDistance),
        root.y + (uy * clampedDistance),
        target.z,
    };
    chain.joint = Vec3{
        root.x + (ux * base) + (px * bendSign * height),
        root.y + (uy * base) + (py * bendSign * height),
        ((root.z + chain.end.z) * 0.5) + kneeDepth,
    };
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

JointChain BuildArmChain(
    const Vec3& shoulder,
    int sideSign,
    double depthSign,
    const ActorPose& pose,
    double planeSide,
    double upperArmLength,
    double forearmLength)
{
    const double headingSign = HeadingSign(pose);
    const double stride = std::clamp(planeSide * 0.42, 28.0, 50.0);
    const double stepHeight = std::clamp(planeSide * 0.18, 10.0, 21.0);
    const WalkLegSample oppositeLeg = SampleWalkLeg(pose.walkPhase, sideSign > 0 ? 0.5 : 0.0, stride, stepHeight);
    const double strideScale = std::max(1.0, stride * 0.45);
    double forward = headingSign * std::clamp(oppositeLeg.footOffset / strideScale, -1.0, 1.0);
    if (pose.heading == ActorPose::Heading::FacingOut) {
        forward = sideSign * 0.46 + (oppositeLeg.footOffset / strideScale) * 0.14;
    }
    if (pose.heading == ActorPose::Heading::MoveRight) {
        forward = -forward;
    }

    const double lift = stepHeight > 0.0 ? oppositeLeg.footLift / stepHeight : 0.0;
    double upperAngleDegrees = 94.0 - (forward * 25.0) - (lift * 3.0);
    double foreAngleDegrees = 108.0 - (forward * 17.0) + (lift * 4.0);
    if (pose.heading == ActorPose::Heading::MoveRight) {
        upperAngleDegrees = 180.0 - upperAngleDegrees;
        foreAngleDegrees = 180.0 - foreAngleDegrees;
    }

    const double upperAngle = DegreesToRadians(upperAngleDegrees);
    const double foreAngle = DegreesToRadians(foreAngleDegrees);
    const LocalJointChain local = BuildLocalChain(
        shoulder,
        upperArmLength,
        forearmLength,
        upperAngle,
        foreAngle,
        depthSign * planeSide * 0.045,
        depthSign * planeSide * 0.040);
    return ProjectJointChain(local, pose, planeSide);
}

JointChain BuildLegChain(
    const Vec3& hip,
    int sideSign,
    double depthSign,
    const ActorPose& pose,
    double planeSide,
    double thighLength,
    double shinLength)
{
    const double headingSign = HeadingSign(pose);
    const double stride = std::clamp(planeSide * 0.34, 22.0, 42.0);
    const double stepHeight = std::clamp(planeSide * 0.19, 12.0, 23.0);
    const double legDrop = (thighLength + shinLength) * 0.78;
    const WalkLegSample sample = SampleWalkLeg(pose.walkPhase, sideSign > 0 ? 0.0 : 0.5, stride, stepHeight);
    const double groundY = hip.y + legDrop;
    const double footInward = std::clamp(planeSide * 0.10, 6.0, 14.0);
    const double footStrideScale = 0.55;
    const double footBaseX = hip.x - (headingSign * footInward);

    Vec3 footTarget{
        footBaseX + (headingSign * sample.footOffset * footStrideScale),
        groundY - sample.footLift,
        hip.z + (depthSign * planeSide * 0.050),
    };
    double bendSign = -headingSign * 0.65;
    if (pose.heading == ActorPose::Heading::FacingOut) {
        footTarget.x = hip.x + (sideSign * stride * 0.14) + (headingSign * sample.footOffset * 0.10);
        footTarget.y = groundY - (sample.footLift * 0.55);
        footTarget.z = hip.z + (depthSign * planeSide * 0.13);
        bendSign = static_cast<double>(sideSign) * 0.65;
    }

    const LocalJointChain local = BuildTwoBoneIkChain(
        hip,
        footTarget,
        thighLength,
        shinLength,
        bendSign,
        depthSign * planeSide * 0.035);
    return ProjectJointChain(local, pose, planeSide);
}

LimbPose BuildLimbPose(
    const besktop::IconFightScene::IconActor& actor,
    const ActorPose& pose)
{
    const double planeSide = std::max(24.0, std::max(actor.planeWidth, actor.planeHeight));
    const double halfW = std::max(8.0, actor.planeWidth) * 0.5;
    const double halfH = std::max(8.0, actor.planeHeight) * 0.5;
    const double shoulderDepth = planeSide * 0.24;
    const double hipDepth = planeSide * 0.22;
    const bool rightFront = IsRightSideFront(pose.heading);
    const double rightDepthSign = rightFront ? 1.0 : -1.0;
    const double leftDepthSign = -rightDepthSign;
    const double headingSign = HeadingSign(pose);
    const double bodySideInset = std::clamp(planeSide * 0.035, 2.0, 5.0);
    const double smallHipExtraOut = 0.0;
    const double hipDownOut = std::clamp(planeSide * 0.18, 12.0, 24.0);
    const double sideAttachX = headingSign * (halfW - bodySideInset);
    const double shoulderRootX = sideAttachX;
    const double shoulderRootY = -halfH * 0.20;
    const Vec3 leftShoulder{shoulderRootX, shoulderRootY, leftDepthSign * shoulderDepth};
    const Vec3 rightShoulder{shoulderRootX, shoulderRootY, rightDepthSign * shoulderDepth};
    const double hipRootY = halfH + hipDownOut;
    const double hipRootX = sideAttachX + (headingSign * smallHipExtraOut);
    const Vec3 leftHip{hipRootX, hipRootY, leftDepthSign * hipDepth};
    const Vec3 rightHip{hipRootX, hipRootY, rightDepthSign * hipDepth};

    const double upperArmLength = planeSide * 0.30;
    const double forearmLength = planeSide * 0.32;
    const double thighLength = planeSide * 0.40;
    const double shinLength = planeSide * 0.41;

    LimbPose limbs;
    limbs.leftArm = BuildArmChain(leftShoulder, -1, leftDepthSign, pose, planeSide, upperArmLength, forearmLength);
    limbs.rightArm = BuildArmChain(rightShoulder, 1, rightDepthSign, pose, planeSide, upperArmLength, forearmLength);
    limbs.leftLeg = BuildLegChain(leftHip, -1, leftDepthSign, pose, planeSide, thighLength, shinLength);
    limbs.rightLeg = BuildLegChain(rightHip, 1, rightDepthSign, pose, planeSide, thighLength, shinLength);
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
        const Gdiplus::PointF shadowRoot = ShadowPoint(chain.root);
        const Gdiplus::PointF shadowJoint = ShadowPoint(chain.joint);
        const Gdiplus::PointF shadowEnd = ShadowPoint(chain.end);
        graphics.DrawLine(shadow, shadowRoot, shadowJoint);
        graphics.DrawLine(shadow, shadowJoint, shadowEnd);
    }
    graphics.DrawLine(&limb, chain.root, chain.joint);
    graphics.DrawLine(&limb, chain.joint, chain.end);
}

void DrawActorLimbs(
    Gdiplus::Graphics& graphics,
    const besktop::IconFightScene::IconActor& actor,
    const ActorPose& pose,
    LimbLayer layer)
{
    if (pose.limbGrow <= 0.01) {
        return;
    }

    const BodyProjection body = BuildBodyProjection(actor, pose);
    const LimbPose limbs = BuildLimbPose(actor, pose);
    const double alphaCeiling = layer == LimbLayer::Front ? 248.0 : 178.0;
    const double alphaValue = alphaCeiling * pose.limbGrow;
    const unsigned char alpha = static_cast<unsigned char>(std::clamp(alphaValue, 0.0, 255.0));
    const double planeSide = std::max(24.0, std::max(actor.planeWidth, actor.planeHeight));
    const float limbWidth = ToFloat(std::clamp(planeSide * 0.072, 5.0, 8.5));
    const bool renderShadows = RenderShadowsEnabled();

    Gdiplus::Pen limb(Gdiplus::Color(alpha, 250, 253, 255), limbWidth);
    limb.SetStartCap(Gdiplus::LineCapRound);
    limb.SetEndCap(Gdiplus::LineCapRound);

    const JointChain* chains[] = {
        &limbs.leftArm,
        &limbs.rightArm,
        &limbs.leftLeg,
        &limbs.rightLeg,
    };

    const auto drawChains = [&](Gdiplus::Pen* shadow) {
        for (const JointChain* chain : chains) {
        const bool shouldDraw = layer == LimbLayer::Front ? chain->frontLayer : !chain->frontLayer;
        if (shouldDraw) {
                DrawJointChain(graphics, *chain, shadow, limb);
            }
        }
    };

    if (renderShadows) {
        Gdiplus::Pen shadow(Gdiplus::Color(static_cast<BYTE>(alpha * 0.28), 0, 0, 0), limbWidth + 3.8f);
        shadow.SetStartCap(Gdiplus::LineCapRound);
        shadow.SetEndCap(Gdiplus::LineCapRound);
        drawChains(&shadow);
    } else {
        drawChains(nullptr);
    }

    if (layer == LimbLayer::Front) {
        const JointChain& frontArm = limbs.rightArm.frontLayer ? limbs.rightArm : limbs.leftArm;
        const JointChain& frontLeg = limbs.rightLeg.frontLayer ? limbs.rightLeg : limbs.leftLeg;
        const double impactSide = frontArm.end.X >= body.centerX ? 1.0 : -1.0;
        if (pose.punch > 0.15) {
            DrawImpact(graphics, frontArm.end.X + ToFloat(impactSide * 16.0), frontArm.end.Y, pose.punch);
        }
        if (pose.kick > 0.15) {
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

    const Gdiplus::InterpolationMode previousInterpolation = graphics.GetInterpolationMode();
    const Gdiplus::PixelOffsetMode previousPixelOffset = graphics.GetPixelOffsetMode();
    graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
    graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
    const Gdiplus::Status drawStatus = graphics.DrawImage(
        bitmap,
        destination,
        3,
        0.0f,
        0.0f,
        static_cast<Gdiplus::REAL>(actor.iconImage->width),
        static_cast<Gdiplus::REAL>(actor.iconImage->height),
        Gdiplus::UnitPixel);
    graphics.SetInterpolationMode(previousInterpolation);
    graphics.SetPixelOffsetMode(previousPixelOffset);

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

void DrawActorBody(Gdiplus::Graphics& graphics, const besktop::IconFightScene::IconActor& actor, const ActorPose& pose)
{
    const BodyProjection body = BuildBodyProjection(actor, pose);

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
    double elapsedSeconds)
{
    if (pose.labelAlpha <= 0.02) {
        return;
    }

    const double shake = SmoothStep(0.30, 0.82, elapsedSeconds) * (1.0 - SmoothStep(1.08, 1.48, elapsedSeconds));
    const double jitterX = std::sin((elapsedSeconds * 66.0) + actor.role) * 5.0 * shake;
    const double jitterY = std::cos((elapsedSeconds * 73.0) + actor.role) * 3.0 * shake;
    const BodyProjection body = BuildBodyProjection(actor, pose);
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
    case besktop::IconFightScene::ScenePhase::TextShaking:
        return L"text-shaking";
    case besktop::IconFightScene::ScenePhase::Awakening:
        return L"awakening";
    case besktop::IconFightScene::ScenePhase::Fighting:
        return L"walking-preview";
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
    iconImageCache_.Clear();
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
    const double previewMarginX = std::max(160.0, displayPlaneWidth * 2.4);
    const double previewMarginY = std::max(150.0, displayPlaneHeight * 2.4);
    const auto clampPreviewCenter = [](double value, double margin, double extent) {
        if (extent <= margin * 2.0) {
            return extent * 0.5;
        }
        return std::clamp(value, margin, extent - margin);
    };
    const auto scaleDesktopRect = [&](const RECT& rect) {
        return RECT{
            static_cast<LONG>((rect.left - snapshot.monitorBounds.left) * scaleX),
            static_cast<LONG>((rect.top - snapshot.monitorBounds.top) * scaleY),
            static_cast<LONG>((rect.right - snapshot.monitorBounds.left) * scaleX),
            static_cast<LONG>((rect.bottom - snapshot.monitorBounds.top) * scaleY),
        };
    };

    const size_t count = std::min(snapshot.icons.size(), kMaxDesktopActors);
    for (size_t index = 0; index < count; ++index) {
        const DesktopIconSnapshot& icon = snapshot.icons[index];
        const bool hasIconBounds = icon.iconBounds.right > icon.iconBounds.left &&
            icon.iconBounds.bottom > icon.iconBounds.top;
        const RECT planeBounds = hasIconBounds ? icon.iconBounds : icon.bounds;

        IconActor actor;
        actor.label = icon.displayName.empty() ? (L"Icon " + std::to_wstring(index + 1)) : icon.displayName;
        actor.baseX = (((planeBounds.left + planeBounds.right) * 0.5) - snapshot.monitorBounds.left) * scaleX;
        actor.baseY = (((planeBounds.top + planeBounds.bottom) * 0.5) - snapshot.monitorBounds.top) * scaleY;
        actor.battleX = clampPreviewCenter(actor.baseX, previewMarginX, clientWidth);
        actor.battleY = clampPreviewCenter(actor.baseY, previewMarginY, clientHeight);
        actor.labelBounds = scaleDesktopRect(icon.labelBounds);
        actor.usedLabelBoundsFallback = icon.usedLabelBoundsFallback;
        actor.planeWidth = displayPlaneWidth;
        actor.planeHeight = displayPlaneHeight;
        actor.usedPlaneFallback = !hasImageListSize || snapshot.iconDisplay.usedFallback;
        actor.planeSizeSource = planeSizeSource;
        actor.role = static_cast<int>(index);
        actor.red = colors[index % std::size(colors)][0];
        actor.green = colors[index % std::size(colors)][1];
        actor.blue = colors[index % std::size(colors)][2];
        actor.iconImage = iconImageCache_.Load(icon.image, actor.label);
        actor.usedIconImageFallback = actor.iconImage == nullptr;
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
        actor.labelBounds = RECT{
            static_cast<LONG>(actor.baseX - 82.0),
            static_cast<LONG>(actor.baseY + 30.0),
            static_cast<LONG>(actor.baseX + 82.0),
            static_cast<LONG>(actor.baseY + 66.0),
        };
        actor.usedLabelBoundsFallback = true;
        actor.planeWidth = 48.0;
        actor.planeHeight = 48.0;
        actor.usedPlaneFallback = true;
        actor.planeSizeSource = L"demo fallback";
        actor.role = index;
        actor.red = colors[index % std::size(colors)][0];
        actor.green = colors[index % std::size(colors)][1];
        actor.blue = colors[index % std::size(colors)][2];
        actors_.push_back(actor);
    }

    LogInfo(L"icon fight scene reset; actors: " + std::to_wstring(actors_.size()));
    LogInfo(L"icon fight desktop label font: " + DesktopIconLabelFontDescription());
    LogInfo(L"icon fight limb model: local 3D foot plant locomotion; stance=0.60, shared side attach shoulder/hip roots; two-bone leg IK; upperArm=0.30 planeSide, forearm=0.32, thigh=0.40, shin=0.41; bodySideInset=0.035, hipExtraOut=0.0, hipDownOut=0.18");
    LogInfo(L"icon fight body model: double-sided icon plane; back face keeps non-mirrored icon UV order");
    LogInfo(std::wstring(L"icon fight render shadows: ") + (RenderShadowsEnabled() ? L"enabled" : L"disabled"));
    LogInfo(std::wstring(L"icon fight debug icon plane: ") + (DebugIconPlaneEnabled() ? L"enabled" : L"disabled"));
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
                actor.iconImage->sourcePath +
                L" (" +
                std::to_wstring(actor.iconImage->width) +
                L" x " +
                std::to_wstring(actor.iconImage->height) +
                L", " +
                actor.iconImage->extractionMethod +
                L")");
        } else {
            LogWarning(L"icon actor image fallback: " + actor.label);
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
    LogInfo(
        L"icon actor label bounds captured: " +
        std::to_wstring(labelBoundsActorCount) +
        L" / " +
        std::to_wstring(actors_.size()));
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

    for (const IconActor& actor : actors_) {
        const ActorPose pose = BuildPose(actor, elapsedSeconds_);
        DrawActorLimbs(graphics, actor, pose, LimbLayer::Back);
    }

    for (const IconActor& actor : actors_) {
        const ActorPose pose = BuildPose(actor, elapsedSeconds_);
        DrawActorBody(graphics, actor, pose);
        DrawActorLabel(graphics, actor, pose, elapsedSeconds_);
    }

    for (const IconActor& actor : actors_) {
        const ActorPose pose = BuildPose(actor, elapsedSeconds_);
        DrawActorLimbs(graphics, actor, pose, LimbLayer::Front);
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
    if (elapsedSeconds < 1.80) {
        return ScenePhase::Awakening;
    }
    return ScenePhase::Fighting;
}

void IconFightScene::LogPhase(ScenePhase phase)
{
    LogInfo(std::wstring(L"icon fight scene phase: ") + PhaseName(phase));
}

} // namespace besktop
