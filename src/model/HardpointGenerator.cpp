#include "model/HardpointGenerator.h"

#include "geom/MeshQuery.h"
#include "model/SuspensionSolver.h"

#include <QCoreApplication>

#include <algorithm>
#include <cmath>
#include <optional>

namespace suspkin {
namespace {

QString tr(const char* text) { return QCoreApplication::translate("HardpointGenerator", text); }

constexpr double kDegToRad = 0.017453292519943295769236907684886;

/// How far outboard of the wheel centre the point on the wheel's axis is put.
/// Only its direction means anything to the solver; this is far enough out to
/// be picked in the viewport and short enough to sit inside a hub.
constexpr double kWheelAxisReach = 100.0;

double rad(double degrees) { return degrees * kDegToRad; }

QString number(double value) { return QString::number(value, 'f', 1); }

QString pointText(const Vec3& p)
{
    return QStringLiteral("(%1, %2, %3)").arg(number(p.x), number(p.y), number(p.z));
}

DesignRole pivotRole(DesignPivot pivot)
{
    switch (pivot) {
    case DesignPivot::LowerFront: return DesignRole::LowerFront;
    case DesignPivot::LowerRear: return DesignRole::LowerRear;
    case DesignPivot::UpperFront: return DesignRole::UpperFront;
    case DesignPivot::UpperRear: return DesignRole::UpperRear;
    }
    return DesignRole::LowerFront;
}

/// Where the line through @p origin along @p direction meets the plane through
/// @p onPlane with @p normal. A dot product -- which is all the Python tool's
/// sympy solve was doing, once per point, and most of why it was slow.
bool linePlane(const Vec3& origin, const Vec3& direction, const Vec3& onPlane, const Vec3& normal,
               Vec3* out)
{
    const double denominator = dot(normal, direction);
    if (std::abs(denominator) < 1e-12 * normal.length() * direction.length()) return false;
    *out = origin + direction * (dot(normal, onPlane - origin) / denominator);
    return true;
}

/// How far along its axis the rocker's second axis point is put. Only the
/// direction means anything to the solver.
constexpr double kRockerAxisReach = 50.0;

/// Wheel travel either side of design over which the rocker's rate is taken.
/// Small enough to be the slope at design, large enough to be well above the
/// solver's tolerance.
constexpr double kRateStep = 0.5;

/// The rocker's axis: along the car, which is where a pushrod running across
/// the car wants it.
const Vec3 kRockerAxis(1.0, 0.0, 0.0);

/// @p direction, in the rocker's plane, turned about the rocker's axis by
/// @p degrees -- positive turning inboard on side @p s. That is right-handed
/// about +x on the left and left-handed on the right, which is what makes one
/// number mean the same rocker on either side.
Vec3 turnInboard(const Vec3& direction, double degrees, double s)
{
    return rotateAbout(direction, Axis{ Vec3(), kRockerAxis }, s * rad(degrees));
}

/// A pickup on a wishbone: @p inboard along it from its ball joint toward the
/// middle of its chassis pivots, then @p height off its plane, upward.
Vec3 pickupOnArm(const GeneratedCorner& corner, bool upper, double inboard, double height)
{
    const Vec3 outer = corner.at(upper ? DesignRole::UpperOuter : DesignRole::LowerOuter);
    const Vec3 pivots = (corner.at(upper ? DesignRole::UpperFront : DesignRole::LowerFront)
                         + corner.at(upper ? DesignRole::UpperRear : DesignRole::LowerRear))
                        * 0.5;
    Vec3 normal = (upper ? corner.upperNormal : corner.lowerNormal).normalized();
    if (normal.z < 0.0) normal = normal * -1.0;
    return outer + (pivots - outer).normalized() * inboard + normal * height;
}

/// How far the rocker turns per millimetre of bump at design, in radians: the
/// solver's own answer, from the corner as it has been generated so far. Every
/// role is named by its own number, so nothing here depends on a template.
std::optional<double> rockerRate(const GeneratedCorner& corner, PushrodMount mount,
                                 QString* error)
{
    MechanismTemplate names;
    for (const DesignRole role : kDesignRoles)
        names.*mechanismRole(role) = QString::number(static_cast<int>(role));
    names.pushrodMount = mount;

    const std::optional<CornerSolver> solver =
        CornerSolver::bind(names, bindGeneratedCorner(corner, names), error);
    if (!solver) return std::nullopt;
    const CornerPose bump = solver->poseAtWheelTravel(kRateStep);
    const CornerPose droop = solver->poseAtWheelTravel(-kRateStep);
    if (!bump.valid || !droop.valid) {
        *error = bump.valid ? droop.error : bump.error;
        return std::nullopt;
    }
    return (bump.rockerAngle - droop.rockerAngle) / (2.0 * kRateStep);
}

/// Where the pushrod meets a rocker arm of @p arm about @p pivot square: the
/// tangent from the pickup to the arm's circle, in the rocker's plane. Of the
/// two, the upper. Empty when the pickup is inside that circle.
std::optional<Vec3> squareArm(const Vec3& pivot, const Vec3& pickup, double arm)
{
    const Vec3 toPickup(0.0, pickup.y - pivot.y, pickup.z - pivot.z);
    const double reach = toPickup.length();
    if (!(arm > 0.0) || reach <= arm) return std::nullopt;
    const double swing = std::acos(arm / reach);
    const Axis axis{ Vec3(), kRockerAxis };
    const Vec3 one = rotateAbout(toPickup / reach, axis, swing);
    const Vec3 other = rotateAbout(toPickup / reach, axis, -swing);
    return one.z >= other.z ? one : other;
}

/// Step 13: the pushrod, the rocker and the damper. The damper's arm is sized
/// so that the installation ratio comes out at its target at design, and the
/// damper is laid square to that arm on the side that bump closes it.
void placeRocker(const AxleDesign& a, PushrodMount mount, GeneratedCorner* out)
{
    const double s = out->side;
    const Vec3 pushrodOuter = pickupOnArm(*out, mount == PushrodMount::UpperArm,
                                          a.pushrodPickupInboard, a.pushrodPickupHeight);
    const Vec3 pivot(pushrodOuter.x + a.rockerPivotOffsetX, s * a.rockerPivotY, a.rockerPivotZ);
    const std::optional<Vec3> pushrodArm = squareArm(pivot, pushrodOuter, a.rockerPushrodArm);
    if (!pushrodArm) {
        out->warnings << tr("The rocker was not generated: its pushrod arm has to be longer than "
                            "nothing and shorter than the distance to the pushrod pickup.");
        return;
    }
    if (!(a.installationRatio > 0.0) || !(a.damperLength > 0.0)) {
        out->warnings << tr("The rocker was not generated: the installation ratio and the "
                            "damper's length both have to be more than nothing.");
        return;
    }

    out->place(DesignRole::PushrodOuter, pushrodOuter);
    out->place(DesignRole::PushrodInner, pivot + *pushrodArm * a.rockerPushrodArm);
    out->place(DesignRole::RockerPivot, pivot);
    out->place(DesignRole::RockerAxis, pivot + kRockerAxis * kRockerAxisReach);

    QString error;
    const std::optional<double> rate = rockerRate(*out, mount, &error);
    if (!rate || std::abs(*rate) < 1e-9) {
        out->warnings << (rate ? tr("The rocker does not turn in bump, so the damper was not "
                                    "generated.")
                               : tr("The damper was not generated: %1").arg(error));
        return;
    }
    out->rockerRate = *rate;

    // The damper eye moves along axis x arm at the rocker's rate. Laid along
    // that motion, pointing the way that closes it in bump, the damper's
    // length changes by exactly the arm times the rate -- which is what the
    // installation ratio is, so the arm is the ratio over the rate.
    const Vec3 damperArm = turnInboard(*pushrodArm, a.rockerDamperAngle, s);
    out->damperArm = a.installationRatio / std::abs(*rate);
    const Vec3 eye = pivot + damperArm * out->damperArm;
    const Vec3 along = cross(kRockerAxis, damperArm).normalized() * (*rate > 0.0 ? 1.0 : -1.0);
    const Vec3 mountPoint = eye + along * a.damperLength;
    out->place(DesignRole::DamperOutboard, eye);
    out->place(DesignRole::DamperInboard, mountPoint);
    if (s * mountPoint.y < 0.0)
        out->warnings << tr("The damper's chassis mount is past the car's centreline.");
}

/// Step 14: the anti-roll bar, by the four points it is defined by. The drop
/// link is square to what carries it, and the bar's arm runs forward from the
/// bar, square to both the drop link and the bar's axis across the car.
void placeAntiRollBar(const AxleDesign& a, const DesignMounts& mounts, GeneratedCorner* out)
{
    const double s = out->side;
    if (!(a.dropLinkLength > 0.0) || a.antiRollArmLength == 0.0
        || !(a.antiRollBearingInset > 0.0)) {
        out->warnings << tr("The anti-roll bar was not generated: the drop link, the bar's arm and "
                            "the bearing's inset all have to be more than nothing.");
        return;
    }

    Vec3 pickup;
    Vec3 link;
    if (mounts.dropLink == DropLinkMount::Rocker) {
        if (!out->has(DesignRole::RockerPivot)) {
            out->warnings << tr("The anti-roll bar hangs off the rocker, which is not being "
                                "generated, so the bar was not generated either.");
            return;
        }
        const Vec3 pivot = out->at(DesignRole::RockerPivot);
        const Vec3 pushrodArm = (out->at(DesignRole::PushrodInner) - pivot).normalized();
        const Vec3 arm = turnInboard(pushrodArm, a.antiRollRockerAngle, s);
        pickup = pivot + arm * a.antiRollRockerArm;
        // Square to its rocker arm, hanging down from it.
        link = cross(kRockerAxis, arm).normalized();
        if (link.z > 0.0) link = link * -1.0;
    } else {
        pickup = pickupOnArm(*out, mounts.dropLink == DropLinkMount::UpperArm,
                             a.antiRollPickupInboard, 0.0);
        // Straight up from the wheel: square to the bar's arm, which is what
        // lets the link turn it rather than push along it.
        link = Vec3(0.0, 0.0, 1.0);
    }

    const Vec3 armEnd = pickup + link * a.dropLinkLength;
    const Vec3 armRoot = armEnd - Vec3(a.antiRollArmLength, 0.0, 0.0);
    const Vec3 bearing = armRoot - Vec3(0.0, s * a.antiRollBearingInset, 0.0);
    out->place(DesignRole::AntiRollDropLinkOuter, pickup);
    out->place(DesignRole::AntiRollArmEnd, armEnd);
    out->place(DesignRole::AntiRollArmRoot, armRoot);
    out->place(DesignRole::AntiRollBearing, bearing);
    if (s * bearing.y < 0.0)
        out->warnings << tr("The anti-roll bar's bearing is past the car's centreline.");
}

} // namespace

QString designRoleLabel(DesignRole role)
{
    switch (role) {
    case DesignRole::LowerFront: return tr("lower front pivot");
    case DesignRole::LowerRear: return tr("lower rear pivot");
    case DesignRole::LowerOuter: return tr("lower ball joint");
    case DesignRole::UpperFront: return tr("upper front pivot");
    case DesignRole::UpperRear: return tr("upper rear pivot");
    case DesignRole::UpperOuter: return tr("upper ball joint");
    case DesignRole::TieRodInboard: return tr("inner tie rod end");
    case DesignRole::TieRodOutboard: return tr("outer tie rod end");
    case DesignRole::WheelCenter: return tr("wheel centre");
    case DesignRole::WheelAxis: return tr("point on the wheel axis");
    case DesignRole::ContactPatch: return tr("contact patch");
    case DesignRole::PushrodOuter: return tr("pushrod pickup");
    case DesignRole::PushrodInner: return tr("pushrod end on the rocker");
    case DesignRole::RockerPivot: return tr("rocker pivot");
    case DesignRole::RockerAxis: return tr("point on the rocker axis");
    case DesignRole::DamperOutboard: return tr("damper end on the rocker");
    case DesignRole::DamperInboard: return tr("damper chassis mount");
    case DesignRole::AntiRollDropLinkOuter: return tr("drop link pickup");
    case DesignRole::AntiRollArmEnd: return tr("anti-roll bar arm end");
    case DesignRole::AntiRollArmRoot: return tr("anti-roll bar arm root");
    case DesignRole::AntiRollBearing: return tr("anti-roll bar bearing");
    }
    return QString();
}

QString MechanismTemplate::*mechanismRole(DesignRole role)
{
    switch (role) {
    case DesignRole::LowerFront: return &MechanismTemplate::lowerFront;
    case DesignRole::LowerRear: return &MechanismTemplate::lowerRear;
    case DesignRole::LowerOuter: return &MechanismTemplate::lowerOuter;
    case DesignRole::UpperFront: return &MechanismTemplate::upperFront;
    case DesignRole::UpperRear: return &MechanismTemplate::upperRear;
    case DesignRole::UpperOuter: return &MechanismTemplate::upperOuter;
    case DesignRole::TieRodInboard: return &MechanismTemplate::tieRodInboard;
    case DesignRole::TieRodOutboard: return &MechanismTemplate::tieRodOutboard;
    case DesignRole::WheelCenter: return &MechanismTemplate::wheelCenter;
    case DesignRole::WheelAxis: return &MechanismTemplate::wheelAxis;
    case DesignRole::ContactPatch: return &MechanismTemplate::contactPatch;
    case DesignRole::PushrodOuter: return &MechanismTemplate::pushrodOuter;
    case DesignRole::PushrodInner: return &MechanismTemplate::pushrodInner;
    case DesignRole::RockerPivot: return &MechanismTemplate::rockerPivot;
    case DesignRole::RockerAxis: return &MechanismTemplate::rockerAxis;
    case DesignRole::DamperOutboard: return &MechanismTemplate::damperOutboard;
    case DesignRole::DamperInboard: return &MechanismTemplate::damperInboard;
    case DesignRole::AntiRollDropLinkOuter: return &MechanismTemplate::antiRollDropLinkOuter;
    case DesignRole::AntiRollArmEnd: return &MechanismTemplate::antiRollArmEnd;
    case DesignRole::AntiRollArmRoot: return &MechanismTemplate::antiRollArmRoot;
    case DesignRole::AntiRollBearing: return &MechanismTemplate::antiRollBearing;
    }
    return &MechanismTemplate::lowerFront;
}

QString mechanismName(const MechanismTemplate& mechanism, DesignRole role)
{
    return mechanism.*mechanismRole(role);
}

GeneratedCorner generateCorner(const DesignParameters& p, AxlePosition axle,
                               const MeshQuery* chassis, const DesignMounts& mounts)
{
    GeneratedCorner out;
    out.axle = axle;
    const AxleDesign& a = p.axle(axle);
    const bool front = axle == AxlePosition::Front;
    const double s = p.side == DesignSide::Left ? 1.0 : -1.0;
    out.side = s;

    const auto fail = [&out](const QString& message) {
        out.ok = false;
        out.error = message;
        return out;
    };

    // Targets that cannot describe a suspension at all. Anything merely odd
    // goes through and is warned about further down.
    if (!(p.wheelbase > 0.0)) return fail(tr("The wheelbase has to be longer than nothing."));
    if (!(p.frontWeight > 0.0 && p.frontWeight < 100.0))
        return fail(tr("The front axle has to carry some of the car, and not all of it."));
    if (!(p.frontBrakeBias >= 0.0 && p.frontBrakeBias <= 100.0))
        return fail(tr("The brake bias is a percentage, from 0 to 100."));
    if (!(p.loadedRadius > 0.0)) return fail(tr("The wheel centre has to be above the ground."));
    if (!(a.track > 0.0)) return fail(tr("The track has to be wider than nothing."));
    if (!(std::abs(a.camber) < 45.0 && std::abs(a.toe) < 45.0))
        return fail(tr("Camber and toe are a few degrees, and these are not."));
    if (!(std::abs(a.caster) < 60.0 && std::abs(a.kingpinInclination) < 60.0))
        return fail(tr("A steering axis leaning that far has nothing to steer with."));
    if (!(a.frontViewSwingArm > 0.0 && a.sideViewSwingArm > 0.0))
        return fail(tr("A swing arm has a length; give both of them one."));
    if (!(a.upperJointHeight + a.lowerJointDrop > 1.0))
        return fail(tr("The upper ball joint has to be above the lower one."));
    for (const double angle : { a.upperForwardAngle, a.upperRearwardAngle, a.lowerForwardAngle,
                                a.lowerRearwardAngle }) {
        if (!(angle > -80.0 && angle < 80.0))
            return fail(tr("A wishbone leg swept %1 degrees never reaches the chassis.")
                            .arg(number(angle)));
    }

    // 1. The wheel centre: along the wheelbase from the weight distribution,
    //    the loaded radius up.
    const double w = p.frontWeight / 100.0;
    Vec3 wc(front ? p.cogX + p.wheelbase * (1.0 - w) : p.cogX - p.wheelbase * w,
            s * a.track / 2.0, p.loadedRadius);

    // 2. Which way the wheel points: static toe and camber in one direction,
    //    outboard. This is what the table's wheel axis point states, and the
    //    only way a hardpoint table can state toe at all.
    const double gamma = rad(a.camber);
    const double tau = rad(a.toe);
    const Vec3 n(std::sin(tau) * std::cos(gamma), s * std::cos(tau) * std::cos(gamma),
                 -std::sin(gamma));
    out.spinAxis = n;

    // 3. The contact patch, by the solver's own functions: the drop from the
    //    centre down the wheel's own plane onto the ground. Negative camber
    //    walks it outboard -- which is why it is computed, not dropped straight
    //    down. The track is measured between the patches, so the wheel is moved
    //    across until its patch is where the track says.
    bool grounded = false;
    const double radius = tireRadiusToGround(wc, n, 0.0, &grounded);
    if (!grounded) return fail(tr("The wheel's plane never reaches the ground at this camber."));
    Vec3 cp = contactPatchFor(wc, n, radius);
    const double shift = s * a.track / 2.0 - cp.y;
    wc.y += shift;
    cp.y += shift;
    out.tyreRadius = radius;
    out.place(DesignRole::WheelCenter, wc);
    out.place(DesignRole::ContactPatch, cp);
    out.place(DesignRole::WheelAxis, wc + n * kWheelAxisReach);

    // 4. The steering axis: through the ground at the patch moved forward by
    //    the trail and inboard by the scrub, leaning top-rearward by the caster
    //    and top-inboard by the inclination -- as tangents, because a caster
    //    angle is what the axis reads in side view, which is exactly how the
    //    solver measures it back.
    const Vec3 pierce(cp.x + a.mechanicalTrail, cp.y - s * a.scrubRadius, cp.z);
    const Vec3 axis =
        Vec3(-std::tan(rad(a.caster)), -s * std::tan(rad(a.kingpinInclination)), 1.0).normalized();
    out.steeringPierce = pierce;
    out.steeringDirection = axis;
    const auto onAxis = [&](double z) { return pierce + axis * ((z - pierce.z) / axis.z); };

    // 5. The ball joints, on that axis, at the heights the rim leaves room for.
    const Vec3 upperOuter = onAxis(wc.z + a.upperJointHeight);
    const Vec3 lowerOuter = onAxis(wc.z - a.lowerJointDrop);
    out.place(DesignRole::UpperOuter, upperOuter);
    out.place(DesignRole::LowerOuter, lowerOuter);
    if (lowerOuter.z <= 0.0)
        out.warnings << tr("The lower ball joint is at or below the ground.");
    for (const auto& [joint, label] : { std::pair{ upperOuter, tr("upper") },
                                        std::pair{ lowerOuter, tr("lower") } }) {
        // How far from the wheel's own axis: further out than the rim, and the
        // joint is not inside the wheel.
        const double fromAxis = cross(joint - wc, n).length();
        if (p.rimRadius > 0.0 && fromAxis > p.rimRadius) {
            out.warnings << tr("The %1 ball joint is %2 mm from the wheel's axis, outside a "
                               "%3 mm rim.")
                                .arg(label, number(fromAxis), number(p.rimRadius));
        }
    }

    // 6. The front-view instant centre, from the roll centre it has to give:
    //    a swing arm's length inboard of the patch, at the height that puts
    //    the line from the patch through it across the centreline at the roll
    //    centre. Off the patch's real y, not off half the track.
    const double halfTrack = std::abs(cp.y);
    out.rollCentre = Vec3(wc.x, cp.y - s * a.frontViewSwingArm,
                          a.rollCentreHeight * a.frontViewSwingArm / halfTrack);

    // 7. The side-view instant centre, from the anti-dive (or anti-lift) and
    //    the share of the braking this axle does -- over the whole wheelbase --
    //    then walked out from the contact patch, toward the other axle, by the
    //    side-view swing arm.
    const double share = front ? p.frontBrakeBias / 100.0 : 1.0 - p.frontBrakeBias / 100.0;
    double theta = 0.0;
    if (a.antiPercent != 0.0) {
        if (share > 1e-9) {
            theta = std::atan((a.antiPercent / 100.0) * p.cogHeight / (p.wheelbase * share));
        } else {
            out.warnings << tr("This axle does none of the braking, so anti-%1 means nothing "
                               "here; the side-view swing arm is level.")
                                .arg(front ? tr("dive") : tr("lift"));
        }
    }
    const double toward = front ? -1.0 : 1.0;
    out.pitchCentre = Vec3(cp.x + toward * a.sideViewSwingArm * std::cos(theta), cp.y,
                           cp.z + a.sideViewSwingArm * std::sin(theta));

    // 8. Each wishbone's plane holds its ball joint and both instant centres:
    //    that is what fixes how the arm moves the joint.
    const auto planeOf = [&](const Vec3& outer) {
        return cross(out.rollCentre - outer, out.pitchCentre - outer);
    };
    out.upperNormal = planeOf(upperOuter);
    out.lowerNormal = planeOf(lowerOuter);
    for (const Vec3* normal : { &out.upperNormal, &out.lowerNormal }) {
        if (normal->length() < 1e-9)
            return fail(tr("A ball joint lies on the line through the two instant centres, so its "
                           "wishbone has no plane."));
        if (std::abs(normal->z) < 1e-6 * normal->length())
            return fail(tr("A wishbone's plane stands on edge, so its legs cannot be laid in it."));
    }

    // 9. Each leg leaves its ball joint at its own planform angle, in its arm's
    //    plane, and ends on the pivot line -- or against the chassis, a
    //    clearance off it, when there is one to put it against.
    const bool againstChassis = p.useChassis && chassis && !chassis->isEmpty();
    const auto legOf = [&](const Vec3& normal, double angle, bool forward) {
        const double dx = (forward ? 1.0 : -1.0) * std::sin(rad(angle));
        const double dy = -s * std::cos(rad(angle));
        return Vec3(dx, dy, -(normal.x * dx + normal.y * dy) / normal.z);
    };
    const auto pivotOn = [&](const Vec3& outer, const Vec3& leg, double pivotY, DesignRole role,
                             QString* error) -> Vec3 {
        const double span = s * outer.y - pivotY;
        if (span <= 0.0) {
            *error = tr("The %1 is set %2 mm from the centreline, which is outboard of its ball "
                        "joint.")
                         .arg(designRoleLabel(role), number(pivotY));
            return outer;
        }
        const Vec3 onLine = outer + leg * (span / std::abs(leg.y));
        if (!againstChassis) return onLine;

        const QString which = designRoleLabel(role);
        const Vec3 unit = leg.normalized();
        // A joint already inside the clearance means the ray starts in the
        // geometry -- which is what an imported upright or wheel looks like.
        if (chassis->distanceTo(outer) < p.chassisClearance) {
            out.warnings << tr("The %1 ball joint is inside the imported geometry, so the %2 was "
                               "put at its distance from the centreline instead. Is the upright "
                               "part of that model?")
                                .arg(role == DesignRole::UpperFront || role == DesignRole::UpperRear
                                         ? tr("upper")
                                         : tr("lower"),
                                     which);
            return onLine;
        }
        const std::optional<RayHit> hit = chassis->castRay(outer, unit);
        if (!hit) {
            // Said rather than covered up: the Python tool invented y = 200
            // here, which is why its output could not be trusted unattended.
            out.warnings << tr("The %1's leg never meets the imported geometry, so it was put at "
                               "its distance from the centreline instead.")
                                .arg(which);
            return onLine;
        }
        // Back off along the leg toward the joint until the pivot is the
        // clearance off the surface. The distance grows from nothing at the
        // hit, so bisection finds it.
        double lo = 0.0;
        double hi = hit->distance;
        for (int i = 0; i < 60 && hi - lo > 1e-6; ++i) {
            const double mid = 0.5 * (lo + hi);
            if (chassis->distanceTo(hit->point - unit * mid) < p.chassisClearance)
                lo = mid;
            else
                hi = mid;
        }
        ++out.pivotsOnChassis;
        return hit->point - unit * hi;
    };

    struct Leg {
        DesignRole role;
        Vec3 outer;
        Vec3 direction;
    };
    const Leg legs[4] = {
        { DesignRole::LowerFront, lowerOuter, legOf(out.lowerNormal, a.lowerForwardAngle, true) },
        { DesignRole::LowerRear, lowerOuter, legOf(out.lowerNormal, a.lowerRearwardAngle, false) },
        { DesignRole::UpperFront, upperOuter, legOf(out.upperNormal, a.upperForwardAngle, true) },
        { DesignRole::UpperRear, upperOuter, legOf(out.upperNormal, a.upperRearwardAngle, false) },
    };
    for (const Leg& leg : legs) {
        const bool upper = leg.role == DesignRole::UpperFront || leg.role == DesignRole::UpperRear;
        QString error;
        out.place(leg.role, pivotOn(leg.outer, leg.direction, upper ? a.upperPivotY : a.lowerPivotY,
                                    leg.role, &error));
        if (!error.isEmpty()) return fail(error);
    }

    // 10. The outer tie rod end: the steering arm's length behind (or ahead
    //     of) the wheel centre, on the lower packaging circle, across at the
    //     steering axis and then the Ackermann offset inboard of it.
    double drop = 0.0;
    if (std::abs(a.steeringArm) <= a.lowerJointDrop) {
        // An arm exactly as long as the circle is wide is the point level with
        // the wheel centre, which is on the circle and nothing to warn about.
        drop = std::sqrt(std::max(0.0, a.lowerJointDrop * a.lowerJointDrop
                                           - a.steeringArm * a.steeringArm));
    } else {
        out.warnings << tr("The outer tie rod end is set further behind the wheel centre than "
                           "the lower ball joint's %1 mm depth, so it was put at wheel-centre "
                           "height.")
                            .arg(number(a.lowerJointDrop));
    }
    Vec3 tieOuter = onAxis(wc.z - drop);
    tieOuter.x = wc.x - a.steeringArm;
    tieOuter.y -= s * a.ackermann;
    out.place(DesignRole::TieRodOutboard, tieOuter);

    // 11. The inner tie rod end: three of the four chassis pivots make a plane,
    //     and it goes where the line from the outer end toward the front-view
    //     instant centre pierces it. Then forward or back by the offset, which
    //     leaves its front view -- and so the bump steer -- where it was.
    const DesignRole advised = pivotRole(a.advisedPivot);
    std::vector<Vec3> plane;
    for (const Leg& leg : legs)
        if (leg.role != advised) plane.push_back(out.at(leg.role));
    const Vec3 planeNormal = cross(plane[1] - plane[0], plane[2] - plane[0]);
    if (planeNormal.length() < 1e-9)
        return fail(tr("Three of the chassis pivots are in a line, so they make no plane for the "
                       "tie rod."));
    Vec3 tieInner;
    if (!linePlane(tieOuter, out.rollCentre - tieOuter, plane[0], planeNormal, &tieInner))
        return fail(tr("The tie rod's line runs parallel to the pivots' plane and never meets it."));
    tieInner.x = tieOuter.x + a.tieRodInboardOffsetX;
    out.place(DesignRole::TieRodInboard, tieInner);
    if (s * (tieInner.y - tieOuter.y) >= 0.0)
        out.warnings << tr("The inner tie rod end came out outboard of the outer one.");

    // 12. Where the fourth pivot would have to be to share that plane: along
    //     its own leg, so the arm keeps its planform. Advice only.
    for (const Leg& leg : legs) {
        if (leg.role != advised) continue;
        Vec3 onPlane;
        if (linePlane(leg.outer, leg.direction, plane[0], planeNormal, &onPlane)) {
            out.advice.valid = true;
            out.advice.pivot = leg.role;
            out.advice.placed = out.at(leg.role);
            out.advice.advised = onPlane;
            out.advice.distance = distance(onPlane, out.at(leg.role));
        }
    }

    out.ok = true;

    // 13. The pushrod, the rocker and the damper, and 14. the anti-roll bar.
    //     Packaging more than geometry, so a group that cannot be placed is
    //     said and left out rather than failing a corner that is otherwise
    //     whole.
    if (a.generateRocker) placeRocker(a, mounts.pushrod, &out);
    if (a.generateAntiRollBar) placeAntiRollBar(a, mounts, &out);
    return out;
}

HardpointTable bindGeneratedCorner(const GeneratedCorner& corner, const MechanismTemplate& mechanism)
{
    HardpointTable table;
    if (!corner.ok) return table;
    for (const DesignRole role : kDesignRoles) {
        if (!corner.has(role)) continue; // a group that was not generated
        const QString name = mechanismName(mechanism, role);
        if (name.isEmpty()) continue; // the template has no name for it
        // Two roles under one name would be one point in two places.
        if (table.indexOf(name) >= 0) continue;
        Hardpoint point;
        point.name = name;
        const Vec3& position = corner.at(role);
        point.coord[0] = position.x;
        point.coord[1] = position.y;
        point.coord[2] = position.z;
        table.points.push_back(point);
    }
    return table;
}

int DesignPlan::count(DesignChange::Kind kind) const
{
    return static_cast<int>(std::count_if(changes.begin(), changes.end(),
                                          [kind](const DesignChange& c) { return c.kind == kind; }));
}

int DesignPlan::handEditedCount() const
{
    return static_cast<int>(std::count_if(changes.begin(), changes.end(),
                                          [](const DesignChange& c) { return c.handEdited; }));
}

DesignPlan planDesign(const DesignParameters& parameters, const LinkageTemplate& templ,
                      const MirrorSpec& mirror, const HardpointTable& current,
                      const HardpointTable& baseline, const MeshQuery* chassis)
{
    DesignPlan plan;
    plan.table = current;
    plan.steering = templ.corners;

    if (templ.mechanism.isEmpty()) {
        plan.error = tr("This project's linkage template does not say which hardpoint plays which "
                        "role, so there is nothing to give the generated points names from.");
        return plan;
    }

    // What each corner's steering means today: a template that says nothing
    // anywhere leaves every axle steered.
    const bool declared = templ.steeringDeclared();
    const auto rackOf = [&](const CornerSpec& corner) {
        return declared ? corner.steeringRack : templ.mechanism.tieRodInboard;
    };

    if (parameters.front.generate && parameters.rear.generate
        && parameters.front.corner == parameters.rear.corner) {
        plan.error = tr("Both axles are set to generate the corner \"%1\"; one of them would "
                        "overwrite the other.")
                         .arg(parameters.front.corner);
        return plan;
    }

    std::vector<int> generatedRows;
    for (const AxlePosition position : { AxlePosition::Front, AxlePosition::Rear }) {
        const AxleDesign& axle = parameters.axle(position);
        if (!axle.generate) continue;
        const QString label = axlePositionLabel(position);

        const auto corner = std::find_if(plan.steering.begin(), plan.steering.end(),
                                         [&axle](const CornerSpec& c) { return c.token == axle.corner; });
        if (corner == plan.steering.end()) {
            plan.error = tr("%1: the linkage template has no corner called \"%2\".")
                             .arg(label, axle.corner);
            return plan;
        }

        const DesignMounts mounts{ templ.mechanism.pushrodMount, templ.mechanism.antiRollMount };
        GeneratedCorner generated = generateCorner(parameters, position, chassis, mounts);
        if (!generated.ok) {
            plan.error = QStringLiteral("%1: %2").arg(label, generated.error);
            return plan;
        }
        for (const QString& warning : generated.warnings)
            plan.warnings << QStringLiteral("%1: %2").arg(label, warning);

        // Named against the table, so a point it still holds under a former
        // name is moved under that name rather than joined by a second one.
        const MechanismTemplate names =
            instantiateMechanism(templ.mechanism, axle.corner, false, mirror, current);
        const HardpointTable points = bindGeneratedCorner(generated, names);

        // Into the table: over a point of the same name, or on the end.
        for (const Hardpoint& point : points.points) {
            const int existing = plan.table.indexOf(point.name);
            if (existing >= 0) {
                Hardpoint& target = plan.table.points[static_cast<std::size_t>(existing)];
                for (int k = 0; k < 3; ++k) target.coord[k] = point.coord[k];
                // Generated here, so it is nobody's mirror any more.
                target.mirrorOf.clear();
                generatedRows.push_back(existing);
            } else {
                plan.table.points.push_back(point);
                generatedRows.push_back(static_cast<int>(plan.table.points.size()) - 1);
            }
        }

        if (generated.advice.valid && generated.advice.distance > 0.05) {
            const QString pivot = mechanismName(names, generated.advice.pivot);
            plan.advice << tr("%1: moving %2 to %3, %4 mm from where it was put, puts all four "
                              "chassis pivots and the inner tie rod end in one plane -- which is "
                              "what takes the bump steer out.")
                               .arg(label,
                                    pivot.isEmpty() ? designRoleLabel(generated.advice.pivot) : pivot,
                                    pointText(generated.advice.advised),
                                    number(generated.advice.distance));
        }

        // Which axle has a rack is the corner's business, and the generator
        // has just said. A corner that already means that is left as the file
        // has it, so an answer the template already gives is not written twice.
        const QString rack = axle.steered ? templ.mechanism.tieRodInboard : QString();
        if (rackOf(*corner) != rack) {
            corner->steeringRack = rack;
            corner->steeringStated = true;
            plan.steeringChanged = true;
        }
        plan.corners.push_back(std::move(generated));
    }

    if (plan.corners.empty()) {
        plan.error = tr("No axle is set to be generated.");
        return plan;
    }

    // A template that said nothing, and now says something, would take every
    // corner it does not mention literally -- and those would stop steering.
    // They are given, explicitly, the answer they have had all along.
    if (plan.steeringChanged && !declared) {
        for (std::size_t i = 0; i < plan.steering.size(); ++i) {
            CornerSpec& corner = plan.steering[i];
            if (corner.steeringStated) continue; // the one just answered
            corner.steeringRack = templ.mechanism.tieRodInboard;
            corner.steeringStated = true;
        }
    }

    // The far side through the project's own mirror rule, as the parts and the
    // mechanism get theirs. Existing mirrors are brought up to date: the whole
    // point of regenerating is that both sides move.
    MirrorSpec both = mirror;
    both.updateExisting = true;
    both.skipMirrored = true;
    const MirrorOutcome mirrored = mirrorHardpoints(plan.table, generatedRows, both);
    plan.table = mirrored.table;
    for (const QString& note : mirrored.notes) plan.warnings << note;

    // What that does, point by point, against what the user has now.
    QStringList touched;
    for (const int row : generatedRows) {
        const QString& name = plan.table.points[static_cast<std::size_t>(row)].name;
        if (!touched.contains(name)) touched << name;
    }
    for (const int row : generatedRows) {
        const QString far =
            mirroredName(plan.table.points[static_cast<std::size_t>(row)].name, both);
        if (!far.isEmpty() && plan.table.indexOf(far) >= 0 && !touched.contains(far)) touched << far;
    }

    for (const QString& name : touched) {
        const Hardpoint* after = plan.table.find(name);
        const Hardpoint* before = current.find(name);
        DesignChange change;
        change.name = name;
        change.mirrored = after && after->isMirrored();
        if (!before) {
            change.kind = DesignChange::Kind::Added;
        } else {
            const Vec3 was(before->coord[0], before->coord[1], before->coord[2]);
            const Vec3 now(after->coord[0], after->coord[1], after->coord[2]);
            change.distance = distance(was, now);
            change.kind = change.distance > 0.0 ? DesignChange::Kind::Moved
                                                : DesignChange::Kind::Unchanged;

            // Hand-edited is a point whose current value is the user's rather
            // than the workbook's or the mirror rule's: that is what would be
            // lost. It differs from what the workbook holds -- or the workbook
            // never held it -- and it is not simply the mirror of its source.
            if (change.kind == DesignChange::Kind::Moved) {
                const Hardpoint* original = baseline.find(name);
                const bool differs = !original || original->coord[0] != before->coord[0]
                                     || original->coord[1] != before->coord[1]
                                     || original->coord[2] != before->coord[2];
                bool isItsSourcesMirror = false;
                if (before->isMirrored()) {
                    const Hardpoint* source = current.find(before->mirrorOf);
                    const int axis = both.axis == MirrorAxis::X ? 0 : (both.axis == MirrorAxis::Y ? 1 : 2);
                    isItsSourcesMirror = source != nullptr;
                    for (int k = 0; isItsSourcesMirror && k < 3; ++k) {
                        const double expected = k == axis ? -source->coord[k] : source->coord[k];
                        isItsSourcesMirror = std::abs(expected - before->coord[k]) < 1e-9;
                    }
                }
                change.handEdited = differs && !isItsSourcesMirror;
            }
        }
        plan.changes.push_back(change);
    }
    return plan;
}

} // namespace suspkin
