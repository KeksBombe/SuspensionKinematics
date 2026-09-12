#pragma once

#include "geom/Vec3.h"

namespace suspkin {

/// A rotation and a translation, in that order: p -> r*p + t.
///
/// Written out rather than borrowed from QMatrix4x4 because that one is float,
/// and this is the transform the upright's whole orientation is read off --
/// camber and toe are its columns in disguise.
struct Rigid {
    /// Rows of the rotation. r[i] dotted with p gives component i of the result.
    Vec3 r[3] = { Vec3(1, 0, 0), Vec3(0, 1, 0), Vec3(0, 0, 1) };
    Vec3 t;

    Vec3 map(const Vec3& p) const
    {
        return Vec3(dot(r[0], p) + t.x, dot(r[1], p) + t.y, dot(r[2], p) + t.z);
    }
    /// A direction is not translated, which is the whole difference between
    /// where the wheel is and which way it points.
    Vec3 rotate(const Vec3& d) const { return Vec3(dot(r[0], d), dot(r[1], d), dot(r[2], d)); }
};

/// The line a hinge turns about: a point on it and a unit direction.
struct Axis {
    Vec3 origin;
    Vec3 direction; ///< unit, or zero when the two points given were the same

    bool isValid() const { return direction.lengthSquared() > 0.0; }
    /// The foot of the perpendicular from @p p, which is the centre of the
    /// circle @p p traces about this axis.
    Vec3 project(const Vec3& p) const { return origin + direction * dot(p - origin, direction); }
    double distanceTo(const Vec3& p) const { return distance(p, project(p)); }
};

/// The axis through @p a and @p b. Degenerate -- direction zero -- when they are
/// the same point, which is how a template that names one pivot twice reports
/// itself rather than by dividing by zero.
Axis axisThrough(const Vec3& a, const Vec3& b);

/// @p p turned about @p axis by @p angle radians, right-handed about the axis
/// direction.
Vec3 rotateAbout(const Vec3& p, const Axis& axis, double angle);

/// The circle a point sweeps out about an axis, as a centre, a radius and an
/// in-plane frame. @ref at reproduces the original point at angle zero, so an
/// angle is always measured from the design position.
struct Circle {
    Vec3 center;
    Vec3 normal; ///< unit
    Vec3 u;      ///< unit, in plane; points at the seed point
    Vec3 v;      ///< unit, in plane; normal x u
    double radius = 0.0;

    bool isValid() const { return radius > 0.0 && normal.lengthSquared() > 0.0; }
    Vec3 at(double angle) const { return center + u * (radius * std::cos(angle))
                                                + v * (radius * std::sin(angle)); }
    /// The angle of @p p about this circle, whether or not it lies on it.
    double angleOf(const Vec3& p) const;
};

/// The circle @p seed traces about @p axis, with the angle origin at @p seed.
Circle circleAbout(const Vec3& seed, const Axis& axis);

/// Where a circle meets a sphere. Returns how many of @p out were filled: 0 when
/// they miss, 1 at a tangency, 2 otherwise.
///
/// This is the workhorse. A wishbone's outer joint is a point on a circle; the
/// link hanging off it is a sphere; almost every step of a suspension solve is
/// one of these.
int intersectCircleSphere(const Circle& circle, const Vec3& center, double radius, Vec3 out[2]);

/// The point at the three given distances from three known points -- the outer
/// tie rod end, given the two ball joints it is rigid with and the rack end it
/// hangs off. Returns 0, 1 or 2 solutions as above.
int trilaterate(const Vec3& a, double ra, const Vec3& b, double rb, const Vec3& c, double rc,
                Vec3 out[2]);

/// The rigid motion taking three points onto three others.
///
/// The triples are assumed congruent, which they are here: they are three joints
/// of one body, solved to the distances they started at. Three points and a
/// frame each, so no least squares and no SVD -- the answer is exact.
/// @p ok is cleared when either triple is collinear, where no frame exists.
Rigid rigidFromTriangle(const Vec3 from[3], const Vec3 to[3], bool* ok);

/// Whichever of @p count candidates is closest to @p reference. Continuation:
/// a circle meets a sphere twice, and which of the two is the suspension and
/// which is its reflection through the arm is decided by where it was a
/// moment ago, never by a rule about signs.
Vec3 nearestTo(const Vec3& reference, const Vec3* candidates, int count, bool* ok);

/// Where the line through @p a and @p b crosses the plane @p axis = @p value,
/// with @p axis 0, 1 or 2 for x, y or z. @p ok is cleared when the line runs
/// parallel to the plane.
Vec3 linePlaneCrossing(const Vec3& a, const Vec3& b, int axis, double value, bool* ok);

/// Where two lines cross when both are flattened into a plane -- the front view
/// for an instant centre, the side view for the other one. @p drop is the axis
/// thrown away (0 for the front view, 1 for the side view). The answer keeps the
/// dropped coordinate of @p a1.
///
/// @p ok is cleared when the two are parallel, which is a real answer about a
/// suspension: parallel arms put the instant centre at infinity.
Vec3 intersectLines2D(const Vec3& a1, const Vec3& a2, const Vec3& b1, const Vec3& b2, int drop,
                      bool* ok);

/// Where the line two planes share crosses the plane @p axis = @p value. Each
/// plane is given by a normal and a point on it; the normals need not be unit
/// length.
///
/// @p ok is cleared when the planes are parallel, or when the line they share
/// runs parallel to the plane it is to cross -- both of which put the answer
/// at infinity.
Vec3 planesCrossing(const Vec3& normalA, const Vec3& pointA, const Vec3& normalB,
                    const Vec3& pointB, int axis, double value, bool* ok);

} // namespace suspkin
