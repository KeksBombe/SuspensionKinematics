#pragma once

#include "geom/Vec3.h"
#include "model/GeomSolve.h"
#include "model/Mechanism.h"

#include <QString>
#include <QStringList>

#include <optional>
#include <vector>

namespace suspkin {

/// A hardpoint the solve moved, by name. A pose is laid over a table by name
/// rather than by index so that neither side has to know the other's ordering.
struct PosedPoint {
    QString name;
    Vec3 position;
};

/// The unit direction that runs straight down the wheel's own plane -- vertical
/// with the wheel upright, leaning with it once there is camber. It is @c down
/// with whatever of it lies along the spin axis taken out, which is what makes
/// it stay in the plane of the tyre.
///
/// Zero when @p spinAxis is vertical, where a wheel lying flat has no lowest
/// point on its rim to speak of.
Vec3 wheelPlaneDown(const Vec3& spinAxis);

/// Where the tyre touches: @p tireRadius from @p wheelCenter, straight down the
/// wheel's own plane. The patch is not carried rigidly by the upright -- it
/// walks around the tyre as the wheel leans, which is exactly what a real
/// contact patch does and what makes scrub radius change with camber.
Vec3 contactPatchFor(const Vec3& wheelCenter, const Vec3& spinAxis, double tireRadius);

/// The tyre radius a wheel centre implies: how far it is above @p groundZ,
/// measured down the wheel plane rather than straight down. This is how a corner
/// with no contact patch in its workbook gets one -- the ground and the wheel's
/// own axis are between them enough to say where it is.
///
/// @p ok is cleared when the wheel plane never reaches the ground, which is a
/// wheel lying on its side or a centre already below it.
double tireRadiusToGround(const Vec3& wheelCenter, const Vec3& spinAxis, double groundZ, bool* ok);

/// What one corner looks like at one point in its travel.
///
/// Angles are degrees, lengths millimetres -- the units the user reads and the
/// units the workbook is in. Only @ref armAngle and its siblings are radians,
/// because they are the solve's own variables and nobody reads them.
struct CornerPose {
    bool valid = false;
    QString error; ///< why not, when it is not

    /// The mechanism's own state. The lower wishbone's rotation is its single
    /// degree of freedom; everything else here follows from it.
    double armAngle = 0.0;         ///< radians from design
    double upperArmAngle = 0.0;    ///< radians; what carries the pushrod
    double rockerAngle = 0.0;      ///< radians
    double antiRollArmAngle = 0.0; ///< radians of bar arm rotation
    double rackTravel = 0.0;       ///< mm the inboard tie rod end was moved, +y

    /// Every point that moved, for laying over the table.
    std::vector<PosedPoint> points;

    /// The joints themselves, named, because the measures are read off them and
    /// because a sweep continues from them.
    Vec3 lowerOuter;
    Vec3 upperOuter;
    Vec3 tieRodInboard;
    Vec3 tieRodOutboard;
    Vec3 wheelCenter;
    Vec3 contactPatch;
    Vec3 pushrodOuter;
    Vec3 pushrodInner;
    Vec3 damperOutboard;
    Vec3 antiRollRocker;
    Vec3 antiRollArmOuter;

    /// The wheel's spin axis, pointing outboard. Camber and toe are this vector
    /// in two different views.
    Vec3 spinAxis;

    /// The rigid motion the upright made from its design position.
    ///
    /// Everything outboard rides it, the wheel *model* included -- which is why
    /// it is published here rather than staying inside the solve: a mesh cannot
    /// be laid over a table by name the way a hardpoint can, so it is turned by
    /// this instead.
    Rigid uprightMotion;
    /// Which hardpoint the wheel centre is, so that a wheel model pinned to that
    /// name can be found and turned by @ref uprightMotion without the caller
    /// having to know the mechanism this pose came out of.
    QString wheelCenterName;

    double wheelTravel = 0.0;      ///< mm the wheel centre rose from design
    double contactPatchRise = 0.0; ///< mm the contact patch rose from design

    /// Degrees. Negative camber leans the top of the wheel in; positive toe
    /// points the front of the wheel at the car's centreline.
    double camber = 0.0;
    double camberChange = 0.0;
    double toe = 0.0;
    double toeChange = 0.0;
    /// Degrees, off the steering axis. Positive caster leans its top rearward;
    /// positive inclination leans its top inboard.
    double caster = 0.0;
    double kingpinInclination = 0.0;

    /// Millimetres, at the ground plane through the contact patch. Positive
    /// scrub puts the tyre outboard of the steering axis; positive trail puts
    /// the contact patch behind it.
    double scrubRadius = 0.0;
    double mechanicalTrail = 0.0;
    double halfTrackChange = 0.0;
    double wheelbaseChange = 0.0;

    /// The spring end of it.
    bool hasDamper = false;
    double damperLength = 0.0;
    double damperTravel = 0.0; ///< mm shorter (negative) or longer than design
    bool hasAntiRoll = false;

    /// The front-view instant centre: where the two wishbones' projected lines
    /// cross. Invalid when they are parallel, which is a real answer about a
    /// suspension rather than a failure -- it puts the centre at infinity.
    Vec3 instantCenter;
    bool instantCenterValid = false;

    const Vec3* find(const QString& name) const;
};

/// One corner's mechanism, bound to the coordinates a table gave it.
///
/// Binding resolves every name once. After that a pose costs a handful of
/// closed-form intersections and no lookups, which is what makes a hundred-step
/// sweep instant and a live slider possible.
class CornerSolver {
public:
    /// Resolve @p mechanism against @p table. Returns nothing, and sets
    /// @p error, when the table does not hold enough of it to solve.
    static std::optional<CornerSolver> bind(const MechanismTemplate& mechanism,
                                            const HardpointTable& table, QString* error);

    const MechanismTemplate& mechanism() const { return m_mechanism; }

    /// Whether a steering rack drives this corner. False for an axle whose toe
    /// link inboard end is simply bolted to the chassis: it then ignores rack
    /// travel entirely rather than being dragged sideways by a rack it has not
    /// got.
    bool isSteered() const { return m_steered; }

    /// Which side of the car this is, from the sign of the wheel centre's y.
    /// It is what camber and toe signs are read against: +y is left in ISO 8855.
    bool isLeft() const { return m_left; }
    /// +1 on the left, -1 on the right. Multiplying a lateral number by this
    /// turns it from "toward +y" into "outboard", which is what the measures
    /// actually mean.
    double side() const { return m_left ? 1.0 : -1.0; }

    /// The direction of the anti-roll bar's own axis, which runs through this
    /// corner's arm pivot. Without it the bar is taken to run along y, which is
    /// what a transverse U-bar does anyway.
    ///
    /// It is a direction rather than the far side's pivot on purpose. Both arms
    /// are on one straight bar, so both have to measure their rotation about the
    /// same direction: give the two of them opposite ones and a pure bump, where
    /// the bar does nothing, reads as the arms twisting against each other.
    void setAntiRollAxis(const Vec3& direction);

    /// The corner as the workbook holds it: the zero every change is measured
    /// from.
    const CornerPose& designPose() const { return m_design; }

    /// Pose at a given lower-wishbone angle. @p previous picks the assembly
    /// branch: a circle meets a sphere twice, and which of the two is this
    /// suspension is decided by where it was a moment ago. Passing nothing
    /// continues from the design position, which is right for any single pose
    /// inside the normal travel.
    CornerPose poseAtArmAngle(double angle, double rackTravel = 0.0,
                              const CornerPose* previous = nullptr) const;

    /// Pose with the wheel centre @p travel millimetres above where the workbook
    /// put it. Bump is positive.
    CornerPose poseAtWheelTravel(double travel, double rackTravel = 0.0,
                                 const CornerPose* previous = nullptr) const;

    /// Pose with the contact patch @p rise millimetres above where it started,
    /// which is how a roll sweep asks for a corner: the ground moves, not the
    /// wheel.
    CornerPose poseAtContactPatchRise(double rise, double rackTravel = 0.0,
                                      const CornerPose* previous = nullptr) const;

private:
    CornerSolver() = default;

    /// The single-degree-of-freedom solve, from an angle to everything else.
    bool solveAt(double angle, double rackTravel, const CornerPose* previous,
                 CornerPose* out) const;
    /// Fill in camber, toe, caster and the rest from a solved geometry.
    void measure(CornerPose* pose) const;
    /// Drive @p target(pose) to @p value by moving the arm angle. @p pick is
    /// what is being aimed at -- the wheel centre's height, or the contact
    /// patch's.
    CornerPose driveTo(double value, double rackTravel, const CornerPose* previous,
                       double (*pick)(const CornerPose&)) const;

    MechanismTemplate m_mechanism;
    bool m_left = true;
    /// The mechanism names a rack point, and it is the point this solve can
    /// actually move: the inboard tie rod end.
    bool m_steered = false;

    // Design coordinates, resolved once.
    Vec3 m_lowerFront, m_lowerRear, m_lowerOuter;
    Vec3 m_upperFront, m_upperRear, m_upperOuter;
    Vec3 m_tieRodInboard, m_tieRodOutboard;
    Vec3 m_wheelCenter, m_contactPatch;
    Vec3 m_pushrodOuter, m_pushrodInner;
    Vec3 m_rockerPivot, m_rockerAxisPoint;
    Vec3 m_damperInboard, m_damperOutboard;
    Vec3 m_antiRollRocker, m_antiRollArmOuter, m_antiRollArmPivot;
    std::vector<PosedPoint> m_carried; ///< design positions of the carried points

    Axis m_lowerAxis, m_upperAxis, m_rockerAxis, m_antiRollAxis;
    Circle m_upperCircle, m_pushrodInnerCircle, m_antiRollCircle;

    // Link lengths, which are the constraints.
    double m_uprightLowerUpper = 0.0;
    double m_uprightLowerTie = 0.0;
    double m_uprightUpperTie = 0.0;
    double m_tieRodLength = 0.0;
    double m_pushrodLength = 0.0;
    double m_dropLinkLength = 0.0;

    /// The table names a point on the wheel's axis, so the wheel's orientation
    /// is measured rather than inferred from a patch under it.
    bool m_hasWheelAxis = false;
    Vec3 m_wheelAxisPoint;
    /// The table names a contact patch of its own. It is still drawn and still
    /// moved; what it supplies to the solve is the tyre radius.
    bool m_hasContactPatch = false;
    /// A contact patch can be had at all -- named or computed. Only a wheel
    /// lying flat has none.
    bool m_hasGround = false;
    double m_tireRadius = 0.0;
    bool m_hasRocker = false;
    bool m_hasDamper = false;
    bool m_hasAntiRoll = false;

    /// The design wheel spin axis, outboard. Taken from the wheel axis point when
    /// the table has one -- which is the only way a table can state static toe --
    /// and otherwise from the contact patch sitting under the wheel centre, which
    /// carries the workbook's static camber but knows nothing about toe. That is
    /// why toe is reported as a change as well as an absolute.
    Vec3 m_designSpinAxis;
    CornerPose m_design;
};

} // namespace suspkin
