#pragma once

#include <algorithm>
#include <cmath>

namespace besktop::anim3d
{

constexpr float Pi = 3.14159265358979323846f;
struct Vec3
{
    float x = 0, y = 0, z = 0;
    Vec3 operator+(Vec3 b) const
    {
        return {x + b.x, y + b.y, z + b.z};
    }
    Vec3 operator-(Vec3 b) const
    {
        return {x - b.x, y - b.y, z - b.z};
    }
    Vec3 operator-() const
    {
        return {-x, -y, -z};
    }
    Vec3 operator*(float k) const
    {
        return {x * k, y * k, z * k};
    }
    Vec3 operator/(float k) const
    {
        return *this * (1 / k);
    }
};
inline float Dot(Vec3 a, Vec3 b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}
inline Vec3 Cross(Vec3 a, Vec3 b)
{
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline float Length(Vec3 a)
{
    return std::sqrt(Dot(a, a));
}
inline Vec3 Normal(Vec3 a, Vec3 fallback = {0, 1, 0})
{
    const float n = Length(a);
    return n > 1e-7f ? a / n : fallback;
}
inline Vec3 Lerp(Vec3 a, Vec3 b, float t)
{
    return a * (1 - t) + b * t;
}
inline float Smooth(float t)
{
    t = std::clamp(t, 0.f, 1.f);
    return t * t * (3 - 2 * t);
}
inline bool Finite(Vec3 a)
{
    return std::isfinite(a.x) && std::isfinite(a.y) && std::isfinite(a.z);
}
struct Quat
{
    float x = 0, y = 0, z = 0, w = 1;
    Quat Inverse() const
    {
        return {-x, -y, -z, w};
    }
    Quat operator*(Quat b) const
    {
        return {w * b.x + x * b.w + y * b.z - z * b.y, w * b.y - x * b.z + y * b.w + z * b.x,
                w * b.z + x * b.y - y * b.x + z * b.w, w * b.w - x * b.x - y * b.y - z * b.z};
    }
    Vec3 Rotate(Vec3 v) const
    {
        const Vec3 q{x, y, z};
        const Vec3 t = Cross(q, v) * 2;
        return v + t * w + Cross(q, t);
    }
};
inline Quat Normalize(Quat q)
{
    const float n = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    return n > 1e-8f ? Quat{q.x / n, q.y / n, q.z / n, q.w / n} : Quat{};
}
inline Quat AxisAngle(Vec3 axis, float angle)
{
    axis = Normal(axis);
    const float s = std::sin(angle * .5f);
    return {axis.x * s, axis.y * s, axis.z * s, std::cos(angle * .5f)};
}
inline Quat Slerp(Quat a, Quat b, float t)
{
    float d = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    if (d < 0)
    {
        b = {-b.x, -b.y, -b.z, -b.w};
        d = -d;
    }
    d = std::clamp(d, -1.f, 1.f);
    float u = 1 - t, v = t;
    if (d < .9995f)
    {
        const float theta = std::acos(d), s = std::sin(theta);
        u = std::sin((1 - t) * theta) / s;
        v = std::sin(t * theta) / s;
    }
    return Normalize({a.x * u + b.x * v, a.y * u + b.y * v, a.z * u + b.z * v, a.w * u + b.w * v});
}
inline Quat FromTo(Vec3 from, Vec3 to)
{
    from = Normal(from);
    to = Normal(to);
    const float dot = Dot(from, to);
    if (dot < -.9999f)
        return AxisAngle(Normal(Cross(from, std::abs(from.x) < .8f ? Vec3{1, 0, 0} : Vec3{0, 1, 0})), Pi);
    const Vec3 axis = Cross(from, to);
    return Normalize({axis.x, axis.y, axis.z, 1 + dot});
}
struct Transform
{
    Vec3 position;
    Quat rotation;
    Transform operator*(const Transform &b) const
    {
        return {position + rotation.Rotate(b.position), Normalize(rotation * b.rotation)};
    }
    Transform Inverse() const
    {
        const Quat q = rotation.Inverse();
        return {q.Rotate(-position), q};
    }
};
} // namespace besktop::anim3d
