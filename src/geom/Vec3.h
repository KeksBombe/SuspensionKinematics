#pragma once

#include <QVector3D>

#include <cmath>

namespace suspkin {

/// A point or a direction in the vehicle frame, in millimetres, in double
/// precision.
///
/// The renderer works in floats and QVector3D is the type it wants, but the
/// solver does not. A circle meeting a sphere near a tangency loses most of a
/// float's mantissa exactly where the answer matters, and these are two-metre
/// numbers that have to come back to the micron over a hundred sweep steps.
/// Hardpoint coordinates are doubles for the same reason -- see Hardpoint::coord
/// -- so this is the type they stay in right up to the draw call.
struct Vec3 {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;

    constexpr Vec3() = default;
    constexpr Vec3(double ax, double ay, double az) : x(ax), y(ay), z(az) {}

    static Vec3 fromVector(const QVector3D& v)
    {
        return Vec3(static_cast<double>(v.x()), static_cast<double>(v.y()),
                    static_cast<double>(v.z()));
    }

    /// Renderer-side position. Millimetre magnitudes are far inside float
    /// precision, so nothing visible is lost here.
    QVector3D toVector() const
    {
        return QVector3D(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z));
    }

    double operator[](int i) const { return i == 0 ? x : (i == 1 ? y : z); }
    double& operator[](int i) { return i == 0 ? x : (i == 1 ? y : z); }

    Vec3 operator+(const Vec3& o) const { return Vec3(x + o.x, y + o.y, z + o.z); }
    Vec3 operator-(const Vec3& o) const { return Vec3(x - o.x, y - o.y, z - o.z); }
    Vec3 operator-() const { return Vec3(-x, -y, -z); }
    Vec3 operator*(double s) const { return Vec3(x * s, y * s, z * s); }
    Vec3 operator/(double s) const { return Vec3(x / s, y / s, z / s); }

    Vec3& operator+=(const Vec3& o) { x += o.x; y += o.y; z += o.z; return *this; }
    Vec3& operator-=(const Vec3& o) { x -= o.x; y -= o.y; z -= o.z; return *this; }
    Vec3& operator*=(double s) { x *= s; y *= s; z *= s; return *this; }

    double lengthSquared() const { return x * x + y * y + z * z; }
    double length() const { return std::sqrt(lengthSquared()); }

    /// The zero vector normalises to itself rather than to NaN, so a degenerate
    /// input produces a degenerate answer the caller can test for instead of a
    /// silent poisoning of everything downstream.
    Vec3 normalized() const
    {
        const double len = length();
        return len > 0.0 ? Vec3(x / len, y / len, z / len) : Vec3();
    }
};

inline Vec3 operator*(double s, const Vec3& v) { return v * s; }

inline double dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

inline Vec3 cross(const Vec3& a, const Vec3& b)
{
    return Vec3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
}

inline double distance(const Vec3& a, const Vec3& b) { return (a - b).length(); }
inline double distanceSquared(const Vec3& a, const Vec3& b) { return (a - b).lengthSquared(); }

} // namespace suspkin
