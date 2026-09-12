#include "model/GeomSolve.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace suspkin {
namespace {

/// Below this a direction is not a direction. Hardpoints are millimetres in the
/// hundreds, so anything this small is two points the template named twice.
constexpr double kTiny = 1e-12;

/// How far past a tangency a root is still taken as one. A sweep driven to the
/// end of a link's travel lands on cos = 1 + a few ulps, and refusing to solve
/// there would show up as the mechanism jamming a hair short of where it does.
constexpr double kTangentSlack = 1e-9;

/// An orthonormal frame on a triangle: the first edge, the triangle's normal,
/// and the third to match. Returns false when the three are collinear, where
/// there is no frame and so no orientation to speak of.
bool triangleFrame(const Vec3 p[3], Vec3 out[3])
{
    const Vec3 e1 = (p[1] - p[0]).normalized();
    if (e1.lengthSquared() < kTiny) return false;
    const Vec3 e3 = cross(e1, p[2] - p[0]).normalized();
    if (e3.lengthSquared() < kTiny) return false;
    out[0] = e1;
    out[1] = cross(e3, e1);
    out[2] = e3;
    return true;
}

} // namespace

Axis axisThrough(const Vec3& a, const Vec3& b)
{
    Axis axis;
    axis.origin = a;
    axis.direction = (b - a).normalized();
    return axis;
}

Vec3 rotateAbout(const Vec3& p, const Axis& axis, double angle)
{
    if (!axis.isValid()) return p;
    const Vec3 offset = p - axis.origin;
    const double c = std::cos(angle);
    const double s = std::sin(angle);
    // Rodrigues, about a line rather than about the origin.
    const Vec3 turned = offset * c + cross(axis.direction, offset) * s
                        + axis.direction * (dot(axis.direction, offset) * (1.0 - c));
    return axis.origin + turned;
}

double Circle::angleOf(const Vec3& p) const
{
    const Vec3 offset = p - center;
    return std::atan2(dot(offset, v), dot(offset, u));
}

Circle circleAbout(const Vec3& seed, const Axis& axis)
{
    Circle circle;
    if (!axis.isValid()) return circle;
    circle.center = axis.project(seed);
    circle.normal = axis.direction;
    const Vec3 arm = seed - circle.center;
    circle.radius = arm.length();
    if (circle.radius < kTiny) {
        // The seed sits on the axis: it does not move, and there is no in-plane
        // direction to measure an angle from. Left invalid on purpose.
        circle.radius = 0.0;
        return circle;
    }
    circle.u = arm / circle.radius;
    circle.v = cross(circle.normal, circle.u);
    return circle;
}

int intersectCircleSphere(const Circle& circle, const Vec3& center, double radius, Vec3 out[2])
{
    if (!circle.isValid() || radius < 0.0) return 0;

    // On the circle, |P - S|^2 expands to a constant plus a single sinusoid in
    // the angle, so the whole thing collapses to one cosine equation.
    const Vec3 d = circle.center - center;
    const double a = dot(d, circle.u);
    const double b = dot(d, circle.v);
    const double m = std::sqrt(a * a + b * b);
    if (m < kTiny) return 0; // concentric in plane: every angle or none

    const double k = (radius * radius - d.lengthSquared() - circle.radius * circle.radius)
                     / (2.0 * circle.radius);
    double ratio = k / m;
    if (ratio > 1.0 + kTangentSlack || ratio < -1.0 - kTangentSlack) return 0;
    ratio = std::clamp(ratio, -1.0, 1.0);

    const double phase = std::atan2(b, a);
    const double spread = std::acos(ratio);
    out[0] = circle.at(phase + spread);
    // Tangency is a spread of zero *or* of pi -- the sphere touching the circle
    // from outside, or reaching just far enough to touch the far side of it.
    // Both are one root, and reporting two coincident ones would let the branch
    // picker choose between a point and itself.
    if (spread < kTangentSlack || (std::numbers::pi - spread) < kTangentSlack) return 1;
    out[1] = circle.at(phase - spread);
    return 2;
}

int trilaterate(const Vec3& a, double ra, const Vec3& b, double rb, const Vec3& c, double rc,
                Vec3 out[2])
{
    const Vec3 ab = b - a;
    const double d = ab.length();
    if (d < kTiny) return 0;
    const Vec3 ex = ab / d;

    const Vec3 ac = c - a;
    const double i = dot(ex, ac);
    const Vec3 rest = ac - ex * i;
    const double restLength = rest.length();
    if (restLength < kTiny) return 0; // the three anchors are collinear
    const Vec3 ey = rest / restLength;
    const Vec3 ez = cross(ex, ey);
    const double j = dot(ey, ac);

    const double x = (ra * ra - rb * rb + d * d) / (2.0 * d);
    const double y = (ra * ra - rc * rc + i * i + j * j) / (2.0 * j) - (i / j) * x;
    double zz = ra * ra - x * x - y * y;
    if (zz < 0.0) {
        // The same slack as a tangency, and for the same reason.
        if (zz < -kTangentSlack * ra * ra - kTiny) return 0;
        zz = 0.0;
    }
    const double z = std::sqrt(zz);

    const Vec3 base = a + ex * x + ey * y;
    out[0] = base + ez * z;
    if (z < kTangentSlack) return 1;
    out[1] = base - ez * z;
    return 2;
}

Rigid rigidFromTriangle(const Vec3 from[3], const Vec3 to[3], bool* ok)
{
    Rigid rigid;
    if (ok) *ok = false;

    Vec3 f[3];
    Vec3 t[3];
    if (!triangleFrame(from, f) || !triangleFrame(to, t)) return rigid;

    // R = F_to * F_from^T, written a row at a time so nothing has to store a
    // matrix type nobody else here needs.
    for (int row = 0; row < 3; ++row)
        rigid.r[row] = f[0] * t[0][row] + f[1] * t[1][row] + f[2] * t[2][row];
    rigid.t = to[0] - Vec3(dot(rigid.r[0], from[0]), dot(rigid.r[1], from[0]),
                           dot(rigid.r[2], from[0]));

    if (ok) *ok = true;
    return rigid;
}

Vec3 nearestTo(const Vec3& reference, const Vec3* candidates, int count, bool* ok)
{
    if (count <= 0) {
        if (ok) *ok = false;
        return reference;
    }
    int best = 0;
    double bestDistance = distanceSquared(reference, candidates[0]);
    for (int i = 1; i < count; ++i) {
        const double d = distanceSquared(reference, candidates[i]);
        if (d < bestDistance) {
            bestDistance = d;
            best = i;
        }
    }
    if (ok) *ok = true;
    return candidates[best];
}

Vec3 linePlaneCrossing(const Vec3& a, const Vec3& b, int axis, double value, bool* ok)
{
    const Vec3 d = b - a;
    if (std::abs(d[axis]) < kTiny) {
        if (ok) *ok = false;
        return a;
    }
    if (ok) *ok = true;
    return a + d * ((value - a[axis]) / d[axis]);
}

Vec3 intersectLines2D(const Vec3& a1, const Vec3& a2, const Vec3& b1, const Vec3& b2, int drop,
                      bool* ok)
{
    const int i0 = (drop + 1) % 3;
    const int i1 = (drop + 2) % 3;
    const Vec3 r = a2 - a1;
    const Vec3 s = b2 - b1;
    const double denominator = r[i0] * s[i1] - r[i1] * s[i0];
    if (std::abs(denominator) < kTiny) {
        if (ok) *ok = false;
        return a1;
    }
    const double t = ((b1[i0] - a1[i0]) * s[i1] - (b1[i1] - a1[i1]) * s[i0]) / denominator;
    Vec3 hit = a1 + r * t;
    hit[drop] = a1[drop]; // the view we flattened into has no opinion about this
    if (ok) *ok = true;
    return hit;
}

Vec3 planesCrossing(const Vec3& normalA, const Vec3& pointA, const Vec3& normalB,
                    const Vec3& pointB, int axis, double value, bool* ok)
{
    // With the one coordinate fixed, each plane is a line in the other two, and
    // the answer is where those two lines cross: a two-by-two system.
    const int i0 = (axis + 1) % 3;
    const int i1 = (axis + 2) % 3;
    const double a1 = normalA[i0];
    const double b1 = normalA[i1];
    const double c1 = dot(normalA, pointA) - normalA[axis] * value;
    const double a2 = normalB[i0];
    const double b2 = normalB[i1];
    const double c2 = dot(normalB, pointB) - normalB[axis] * value;
    const double determinant = a1 * b2 - a2 * b1;
    // Relative to the normals themselves, so the test means the same thing
    // whether they came out of a cross product of millimetres or of unit vectors.
    const double scale = normalA.length() * normalB.length();
    if (!(scale > 0.0) || std::abs(determinant) < kTiny * scale) {
        if (ok) *ok = false;
        return pointA;
    }
    Vec3 hit;
    hit[axis] = value;
    hit[i0] = (c1 * b2 - c2 * b1) / determinant;
    hit[i1] = (a1 * c2 - a2 * c1) / determinant;
    if (ok) *ok = true;
    return hit;
}

} // namespace suspkin
