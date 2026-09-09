#include "model/SuspensionSolver.h"

#include <QCoreApplication>

#include <algorithm>
#include <cmath>

namespace suspkin {
namespace {

QString tr(const char* text) { return QCoreApplication::translate("SuspensionSolver", text); }

constexpr double kRadToDeg = 57.295779513082320876798154814105;

/// How close to the asked-for height counts as arrived. A micron: far below
/// anything a suspension is measured to, and far above where the doubles stop.
constexpr double kTargetTolerance = 1e-7;

/// Bigger than any single Newton step should ever need on a suspension, and
/// small enough that overshooting cannot fling the solve past a link's limit
/// into a region where it does not assemble.
constexpr double kMaxStep = 0.35;

/// Where the road is. A hardpoint workbook is measured in vehicle coordinates
/// with z up from the ground, so this is not a guess so much as the frame's own
/// definition -- and a corner whose workbook says otherwise names a contact
/// patch, which is taken over it.
constexpr double kGroundZ = 0.0;

double pickWheelHeight(const CornerPose& pose) { return pose.wheelCenter.z; }
double pickContactHeight(const CornerPose& pose) { return pose.contactPatch.z; }

} // namespace

Vec3 wheelPlaneDown(const Vec3& spinAxis)
{
    const Vec3 axis = spinAxis.normalized();
    const Vec3 down(0.0, 0.0, -1.0);
    const Vec3 inPlane = down - axis * dot(down, axis);
    // A wheel whose axis stands vertical has no down in its own plane. It is not
    // a suspension anybody is drawing, but it is a division by zero if ignored.
    if (inPlane.lengthSquared() < 1e-12) return Vec3();
    return inPlane.normalized();
}

Vec3 contactPatchFor(const Vec3& wheelCenter, const Vec3& spinAxis, double tireRadius)
{
    const Vec3 down = wheelPlaneDown(spinAxis);
    if (down.lengthSquared() < 0.5) return wheelCenter;
    return wheelCenter + down * tireRadius;
}

double tireRadiusToGround(const Vec3& wheelCenter, const Vec3& spinAxis, double groundZ, bool* ok)
{
    if (ok) *ok = false;
    const Vec3 down = wheelPlaneDown(spinAxis);
    if (down.z > -1e-9) return 0.0; // the wheel plane runs away from the ground
    const double radius = (groundZ - wheelCenter.z) / down.z;
    if (radius <= 0.0) return 0.0; // a wheel centre at or below the ground
    if (ok) *ok = true;
    return radius;
}

const Vec3* CornerPose::find(const QString& name) const
{
    for (const PosedPoint& point : points)
        if (point.name == name) return &point.position;
    return nullptr;
}

std::optional<CornerSolver> CornerSolver::bind(const MechanismTemplate& mechanism,
                                               const HardpointTable& table, QString* error)
{
    const MechanismCoverage coverage = coverMechanism(mechanism, table);
    if (coverage.absent) {
        if (error) *error = tr("None of this corner's hardpoints are in the table.");
        return std::nullopt;
    }
    if (!coverage.missingRequired.isEmpty()) {
        if (error)
            *error = tr("Cannot solve this corner: the table has no %1.")
                         .arg(coverage.missingRequired.join(QStringLiteral(", ")));
        return std::nullopt;
    }

    CornerSolver solver;
    solver.m_mechanism = mechanism;

    const auto at = [&table](const QString& name) {
        const Hardpoint* point = table.find(name);
        return point ? Vec3(point->coord[0], point->coord[1], point->coord[2]) : Vec3();
    };
    const auto has = [&table](const QString& name) {
        return !name.isEmpty() && table.indexOf(name) >= 0;
    };

    solver.m_lowerFront = at(mechanism.lowerFront);
    solver.m_lowerRear = at(mechanism.lowerRear);
    solver.m_lowerOuter = at(mechanism.lowerOuter);
    solver.m_upperFront = at(mechanism.upperFront);
    solver.m_upperRear = at(mechanism.upperRear);
    solver.m_upperOuter = at(mechanism.upperOuter);
    solver.m_tieRodInboard = at(mechanism.tieRodInboard);
    solver.m_tieRodOutboard = at(mechanism.tieRodOutboard);
    solver.m_wheelCenter = at(mechanism.wheelCenter);

    // A rack drives this corner only if the template says one does, and only
    // through the point this solve knows how to move. Anything else is caught
    // where there is somewhere to report it -- AxleSolver::build().
    solver.m_steered = !mechanism.steeringRack.isEmpty()
                       && mechanism.steeringRack == mechanism.tieRodInboard;

    solver.m_hasWheelAxis = has(mechanism.wheelAxis);
    if (solver.m_hasWheelAxis) solver.m_wheelAxisPoint = at(mechanism.wheelAxis);

    solver.m_hasContactPatch = has(mechanism.contactPatch);
    if (solver.m_hasContactPatch) solver.m_contactPatch = at(mechanism.contactPatch);

    solver.m_hasRocker = mechanism.hasRocker() && has(mechanism.pushrodOuter)
                         && has(mechanism.pushrodInner) && has(mechanism.rockerPivot)
                         && has(mechanism.rockerAxis);
    if (solver.m_hasRocker) {
        solver.m_pushrodOuter = at(mechanism.pushrodOuter);
        solver.m_pushrodInner = at(mechanism.pushrodInner);
        solver.m_rockerPivot = at(mechanism.rockerPivot);
        solver.m_rockerAxisPoint = at(mechanism.rockerAxis);
    }

    // The damper hangs off the rocker, so without one there is nothing to
    // measure its length against.
    solver.m_hasDamper = solver.m_hasRocker && has(mechanism.damperInboard)
                         && has(mechanism.damperOutboard);
    if (solver.m_hasDamper) {
        solver.m_damperInboard = at(mechanism.damperInboard);
        solver.m_damperOutboard = at(mechanism.damperOutboard);
    }

    solver.m_hasAntiRoll = solver.m_hasRocker && has(mechanism.antiRollRocker)
                           && has(mechanism.antiRollArmOuter) && has(mechanism.antiRollArmPivot);
    if (solver.m_hasAntiRoll) {
        solver.m_antiRollRocker = at(mechanism.antiRollRocker);
        solver.m_antiRollArmOuter = at(mechanism.antiRollArmOuter);
        solver.m_antiRollArmPivot = at(mechanism.antiRollArmPivot);
    }

    for (const QString& name : mechanism.carried)
        if (has(name)) solver.m_carried.push_back(PosedPoint{ name, at(name) });

    // +y is left in ISO 8855, and that is the whole of what decides which way
    // camber and toe are signed.
    solver.m_left = solver.m_wheelCenter.y >= 0.0;

    solver.m_lowerAxis = axisThrough(solver.m_lowerFront, solver.m_lowerRear);
    solver.m_upperAxis = axisThrough(solver.m_upperFront, solver.m_upperRear);
    if (!solver.m_lowerAxis.isValid() || !solver.m_upperAxis.isValid()) {
        if (error)
            *error = tr("A wishbone's two chassis pivots are at the same place, so it has no "
                        "axis to turn about.");
        return std::nullopt;
    }
    solver.m_upperCircle = circleAbout(solver.m_upperOuter, solver.m_upperAxis);
    if (!solver.m_upperCircle.isValid()) {
        if (error) *error = tr("The upper ball joint sits on its own pivot axis.");
        return std::nullopt;
    }

    solver.m_uprightLowerUpper = distance(solver.m_lowerOuter, solver.m_upperOuter);
    solver.m_uprightLowerTie = distance(solver.m_lowerOuter, solver.m_tieRodOutboard);
    solver.m_uprightUpperTie = distance(solver.m_upperOuter, solver.m_tieRodOutboard);
    solver.m_tieRodLength = distance(solver.m_tieRodInboard, solver.m_tieRodOutboard);

    if (solver.m_hasRocker) {
        solver.m_rockerAxis = axisThrough(solver.m_rockerPivot, solver.m_rockerAxisPoint);
        solver.m_pushrodInnerCircle = circleAbout(solver.m_pushrodInner, solver.m_rockerAxis);
        solver.m_pushrodLength = distance(solver.m_pushrodOuter, solver.m_pushrodInner);
        if (!solver.m_pushrodInnerCircle.isValid()) solver.m_hasRocker = false;
    }
    if (!solver.m_hasRocker) {
        solver.m_hasDamper = false;
        solver.m_hasAntiRoll = false;
    }

    if (solver.m_hasAntiRoll) {
        // A U-bar runs across the car, so without the far side's arm root to
        // give the real axis, y is the right guess and not much of one.
        solver.m_antiRollAxis = axisThrough(solver.m_antiRollArmPivot,
                                            solver.m_antiRollArmPivot + Vec3(0, 1, 0));
        solver.m_antiRollCircle = circleAbout(solver.m_antiRollArmOuter, solver.m_antiRollAxis);
        solver.m_dropLinkLength = distance(solver.m_antiRollRocker, solver.m_antiRollArmOuter);
        if (!solver.m_antiRollCircle.isValid()) solver.m_hasAntiRoll = false;
    }

    // Which way the wheel points, as a unit vector outboard along its own axis of
    // rotation.
    //
    // A second point on that axis says it outright, camber and toe together, and
    // is the only way a hardpoint table can state static toe at all. Without one
    // it is inferred the old way, from the contact patch sitting under the wheel
    // centre: that carries the workbook's static camber and assumes zero toe,
    // which is why toe is also reported as a change from this position.
    Vec3 spin;
    if (solver.m_hasWheelAxis) {
        spin = (solver.m_wheelAxisPoint - solver.m_wheelCenter).normalized();
        // Either end of the axle may have been measured; outboard is the end the
        // measures are read against.
        if (spin.y * solver.side() < 0.0) spin = spin * -1.0;
    } else if (solver.m_hasContactPatch) {
        const Vec3 up = solver.m_wheelCenter - solver.m_contactPatch;
        spin = cross(up, Vec3(1, 0, 0)).normalized() * solver.side();
    }
    if (spin.lengthSquared() < 0.5) spin = Vec3(0, solver.side(), 0);
    solver.m_designSpinAxis = spin;

    // The tyre radius, which is what turns a wheel centre and an axis into a
    // contact patch: the drop from the centre down the wheel's own plane onto
    // the road. What a patch in the workbook supplies is the height of that
    // road -- a table measured from a chassis datum rather than from the ground
    // has no other way of saying where the ground is -- and not the patch's own
    // position, which under a cambered wheel is not where the tyre touches.
    //
    // With no axis point named the two come to the same thing: the drop is then
    // straight down the line the patch itself defined, so the patch is
    // reproduced exactly and nothing about an older project moves.
    const double groundZ = solver.m_hasContactPatch ? solver.m_contactPatch.z : kGroundZ;
    bool grounded = false;
    solver.m_tireRadius = tireRadiusToGround(solver.m_wheelCenter, spin, groundZ, &grounded);
    solver.m_hasGround = grounded;

    // Computed from here on, at design as well as through the travel, so that
    // the rise a roll sweep drives is measured against the same thing it moves.
    if (grounded)
        solver.m_contactPatch = contactPatchFor(solver.m_wheelCenter, spin, solver.m_tireRadius);
    else if (!solver.m_hasContactPatch)
        solver.m_contactPatch = solver.m_wheelCenter - Vec3(0, 0, 1);

    CornerPose design;
    if (!solver.solveAt(0.0, 0.0, nullptr, &design)) {
        if (error)
            *error = design.error.isEmpty()
                         ? tr("This corner does not assemble at the coordinates given.")
                         : design.error;
        return std::nullopt;
    }
    // Measured against itself, so every change is zero here by construction.
    design.camberChange = 0.0;
    design.toeChange = 0.0;
    design.damperTravel = 0.0;
    solver.m_design = design;

    return solver;
}

void CornerSolver::setAntiRollAxis(const Vec3& direction)
{
    if (!m_hasAntiRoll) return;
    const Axis axis = axisThrough(m_antiRollArmPivot, m_antiRollArmPivot + direction);
    if (!axis.isValid()) return;
    const Circle circle = circleAbout(m_antiRollArmOuter, axis);
    if (!circle.isValid()) return;
    m_antiRollAxis = axis;
    m_antiRollCircle = circle;
    // The design pose's bar angle is zero either way, so nothing else has to be
    // rebuilt: the axis only ever enters through this circle.
}

bool CornerSolver::solveAt(double angle, double rackTravel, const CornerPose* previous,
                           CornerPose* out) const
{
    CornerPose pose;
    pose.armAngle = angle;
    // An axle with no rack ignores rack travel rather than pretending to steer.
    // The pose reports what actually happened, not what was asked for.
    const double rack = m_steered ? rackTravel : 0.0;
    pose.rackTravel = rack;

    Vec3 candidates[2];
    bool ok = false;

    // 1. The lower ball joint simply turns with its wishbone. This is the one
    //    free variable; everything below follows from it.
    pose.lowerOuter = rotateAbout(m_lowerOuter, m_lowerAxis, angle);

    // 2. The upper ball joint is on its own circle and a fixed distance from the
    //    lower one, because the upright between them is rigid.
    int count = intersectCircleSphere(m_upperCircle, pose.lowerOuter, m_uprightLowerUpper,
                                      candidates);
    if (count == 0) {
        pose.error = tr("The upper wishbone cannot reach the upright here.");
        *out = pose;
        return false;
    }
    pose.upperOuter = nearestTo(previous ? previous->upperOuter : m_upperOuter, candidates, count,
                                &ok);

    // 3. The outer tie rod end: rigid with the two ball joints, and a tie rod's
    //    length from the rack.
    pose.tieRodInboard = m_tieRodInboard + Vec3(0.0, rack, 0.0);
    count = trilaterate(pose.lowerOuter, m_uprightLowerTie, pose.upperOuter, m_uprightUpperTie,
                        pose.tieRodInboard, m_tieRodLength, candidates);
    if (count == 0) {
        pose.error = tr("The tie rod cannot reach the upright here.");
        *out = pose;
        return false;
    }
    pose.tieRodOutboard = nearestTo(previous ? previous->tieRodOutboard : m_tieRodOutboard,
                                    candidates, count, &ok);

    // 4. Three joints of one rigid body place the whole of it, wheel included.
    const Vec3 from[3] = { m_lowerOuter, m_upperOuter, m_tieRodOutboard };
    const Vec3 to[3] = { pose.lowerOuter, pose.upperOuter, pose.tieRodOutboard };
    const Rigid upright = rigidFromTriangle(from, to, &ok);
    if (!ok) {
        pose.error = tr("The upright's three joints are in a straight line, so its orientation "
                        "is not determined.");
        *out = pose;
        return false;
    }
    pose.wheelCenter = upright.map(m_wheelCenter);
    pose.spinAxis = upright.rotate(m_designSpinAxis).normalized();
    pose.uprightMotion = upright;
    pose.wheelCenterName = m_mechanism.wheelCenter;

    // The wheel is rigid with the upright; the contact patch is not. It is the
    // bottom of a tyre that stays on the road, so it is recomputed from the
    // wheel's new attitude rather than carried round with the upright -- which
    // is what makes it walk outboard as the wheel gains camber instead of
    // lifting off the ground with it.
    pose.contactPatch = m_hasGround
                            ? contactPatchFor(pose.wheelCenter, pose.spinAxis, m_tireRadius)
                            : upright.map(m_contactPatch);

    // 5. How far the upper wishbone turned, which is what carries the pushrod.
    pose.upperArmAngle = m_upperCircle.angleOf(pose.upperOuter);

    if (m_hasRocker) {
        switch (m_mechanism.pushrodMount) {
        case PushrodMount::UpperArm:
            pose.pushrodOuter = rotateAbout(m_pushrodOuter, m_upperAxis, pose.upperArmAngle);
            break;
        case PushrodMount::LowerArm:
            pose.pushrodOuter = rotateAbout(m_pushrodOuter, m_lowerAxis, angle);
            break;
        case PushrodMount::Upright:
            pose.pushrodOuter = upright.map(m_pushrodOuter);
            break;
        }

        // 6. The rocker turns until its pushrod pickup is a pushrod away.
        count = intersectCircleSphere(m_pushrodInnerCircle, pose.pushrodOuter, m_pushrodLength,
                                      candidates);
        if (count == 0) {
            pose.error = tr("The pushrod cannot reach the rocker here.");
            *out = pose;
            return false;
        }
        pose.pushrodInner = nearestTo(previous ? previous->pushrodInner : m_pushrodInner,
                                      candidates, count, &ok);
        pose.rockerAngle = m_pushrodInnerCircle.angleOf(pose.pushrodInner);

        if (m_hasDamper)
            pose.damperOutboard = rotateAbout(m_damperOutboard, m_rockerAxis, pose.rockerAngle);

        if (m_hasAntiRoll) {
            pose.antiRollRocker = rotateAbout(m_antiRollRocker, m_rockerAxis, pose.rockerAngle);
            count = intersectCircleSphere(m_antiRollCircle, pose.antiRollRocker, m_dropLinkLength,
                                          candidates);
            if (count == 0) {
                pose.error = tr("The anti-roll drop link cannot reach the bar here.");
                *out = pose;
                return false;
            }
            pose.antiRollArmOuter = nearestTo(
                previous ? previous->antiRollArmOuter : m_antiRollArmOuter, candidates, count, &ok);
            pose.antiRollArmAngle = m_antiRollCircle.angleOf(pose.antiRollArmOuter);
        }
    }

    const auto add = [&pose](const QString& name, const Vec3& position) {
        if (!name.isEmpty()) pose.points.push_back(PosedPoint{ name, position });
    };
    add(m_mechanism.lowerOuter, pose.lowerOuter);
    add(m_mechanism.upperOuter, pose.upperOuter);
    add(m_mechanism.tieRodInboard, pose.tieRodInboard);
    add(m_mechanism.tieRodOutboard, pose.tieRodOutboard);
    add(m_mechanism.wheelCenter, pose.wheelCenter);
    if (m_hasWheelAxis) add(m_mechanism.wheelAxis, upright.map(m_wheelAxisPoint));
    if (m_hasContactPatch) add(m_mechanism.contactPatch, pose.contactPatch);
    for (const PosedPoint& carried : m_carried) add(carried.name, upright.map(carried.position));
    if (m_hasRocker) {
        add(m_mechanism.pushrodOuter, pose.pushrodOuter);
        add(m_mechanism.pushrodInner, pose.pushrodInner);
    }
    if (m_hasDamper) add(m_mechanism.damperOutboard, pose.damperOutboard);
    if (m_hasAntiRoll) {
        add(m_mechanism.antiRollRocker, pose.antiRollRocker);
        add(m_mechanism.antiRollArmOuter, pose.antiRollArmOuter);
    }

    pose.hasDamper = m_hasDamper;
    pose.hasAntiRoll = m_hasAntiRoll;
    pose.valid = true;
    measure(&pose);
    *out = pose;
    return true;
}

void CornerSolver::measure(CornerPose* pose) const
{
    const double s = side();

    // Camber and toe are the spin axis in two different views. The axis points
    // outboard, so the same two formulas do both sides.
    const Vec3 n = pose->spinAxis;
    pose->camber = -std::asin(std::clamp(n.z, -1.0, 1.0)) * kRadToDeg;
    pose->toe = std::atan2(n.x, s * n.y) * kRadToDeg;
    pose->camberChange = pose->camber - m_design.camber;
    pose->toeChange = pose->toe - m_design.toe;

    // The steering axis runs from the lower ball joint up to the upper one.
    const Vec3 steering = pose->upperOuter - pose->lowerOuter;
    pose->caster = std::atan2(-steering.x, steering.z) * kRadToDeg;
    pose->kingpinInclination = std::atan2(-s * steering.y, steering.z) * kRadToDeg;

    pose->wheelTravel = pose->wheelCenter.z - m_wheelCenter.z;
    pose->contactPatchRise = pose->contactPatch.z - m_contactPatch.z;

    // Track and wheelbase are read at the ground when there is a contact patch
    // to read them at -- named or computed -- and at the wheel centre when the
    // wheel is lying so flat that there is none.
    const Vec3& ground = m_hasGround ? pose->contactPatch : pose->wheelCenter;
    const Vec3& groundDesign = m_hasGround ? m_contactPatch : m_wheelCenter;
    pose->halfTrackChange = s * (ground.y - groundDesign.y);
    pose->wheelbaseChange = ground.x - groundDesign.x;

    bool ok = false;
    const Vec3 pierce = linePlaneCrossing(pose->lowerOuter, pose->upperOuter, 2, ground.z, &ok);
    if (ok) {
        pose->scrubRadius = s * (ground.y - pierce.y);
        pose->mechanicalTrail = pierce.x - ground.x;
    }

    if (m_hasDamper) {
        pose->damperLength = distance(pose->damperOutboard, m_damperInboard);
        pose->damperTravel = pose->damperLength - m_design.damperLength;
    }

    // The front-view instant centre. Each wishbone's line in that view runs from
    // its outer ball joint to where its pivot axis pierces the transverse plane
    // through the wheel centre -- which is what makes a swept-back arm behave
    // differently from a square one.
    bool lowerOk = false;
    bool upperOk = false;
    Vec3 lowerPivot =
        linePlaneCrossing(m_lowerFront, m_lowerRear, 0, pose->wheelCenter.x, &lowerOk);
    if (!lowerOk) lowerPivot = (m_lowerFront + m_lowerRear) * 0.5;
    Vec3 upperPivot =
        linePlaneCrossing(m_upperFront, m_upperRear, 0, pose->wheelCenter.x, &upperOk);
    if (!upperOk) upperPivot = (m_upperFront + m_upperRear) * 0.5;

    bool centerOk = false;
    Vec3 center =
        intersectLines2D(lowerPivot, pose->lowerOuter, upperPivot, pose->upperOuter, 0, &centerOk);
    center.x = pose->wheelCenter.x;
    pose->instantCenter = center;
    pose->instantCenterValid = centerOk;
}

CornerPose CornerSolver::driveTo(double value, double rackTravel, const CornerPose* previous,
                                 double (*pick)(const CornerPose&)) const
{
    CornerPose pose;
    double angle = previous && previous->valid ? previous->armAngle : 0.0;
    if (!solveAt(angle, rackTravel, previous, &pose)) return pose;

    double residual = pick(pose) - value;
    constexpr double kProbe = 1e-5;

    for (int iteration = 0; iteration < 40 && std::abs(residual) > kTargetTolerance; ++iteration) {
        // The slope of height against arm angle, by a probe to whichever side
        // assembles. Near a travel limit only one of them does.
        CornerPose probe;
        double slope = 0.0;
        if (solveAt(angle + kProbe, rackTravel, &pose, &probe)) {
            slope = (pick(probe) - pick(pose)) / kProbe;
        } else if (solveAt(angle - kProbe, rackTravel, &pose, &probe)) {
            slope = (pick(pose) - pick(probe)) / kProbe;
        } else {
            pose.valid = false;
            pose.error = tr("The mechanism jams before it reaches that position.");
            return pose;
        }
        if (std::abs(slope) < 1e-12) break;

        double step = std::clamp(-residual / slope, -kMaxStep, kMaxStep);

        // Back off until it assembles again. A step that overshoots the end of
        // the travel is the normal way to find where that end is.
        CornerPose next;
        bool moved = false;
        for (int backoff = 0; backoff < 30; ++backoff) {
            if (solveAt(angle + step, rackTravel, &pose, &next)) {
                moved = true;
                break;
            }
            step *= 0.5;
        }
        if (!moved) {
            pose.valid = false;
            pose.error = tr("The mechanism jams before it reaches that position.");
            return pose;
        }

        angle += step;
        pose = next;
        residual = pick(pose) - value;
    }

    if (std::abs(residual) > 1e-4) {
        pose.valid = false;
        pose.error = tr("That position is outside this corner's travel.");
    }
    return pose;
}

CornerPose CornerSolver::poseAtArmAngle(double angle, double rackTravel,
                                        const CornerPose* previous) const
{
    CornerPose pose;
    solveAt(angle, rackTravel, previous, &pose);
    return pose;
}

CornerPose CornerSolver::poseAtWheelTravel(double travel, double rackTravel,
                                           const CornerPose* previous) const
{
    return driveTo(m_wheelCenter.z + travel, rackTravel, previous, &pickWheelHeight);
}

CornerPose CornerSolver::poseAtContactPatchRise(double rise, double rackTravel,
                                                const CornerPose* previous) const
{
    return driveTo(m_contactPatch.z + rise, rackTravel, previous, &pickContactHeight);
}

} // namespace suspkin
