#pragma once

#include <QJsonObject>
#include <QString>

namespace suspkin {

/// Which end of the car an axle's targets describe. It decides where the axle
/// sits along the wheelbase and which way anti-dive leans the side-view swing
/// arm, so it is not just a label.
enum class AxlePosition { Front, Rear };

/// Which of the four chassis pivots is left off the plane the tie rod's inboard
/// end is put on. Four points are never naturally coplanar, so three make the
/// plane and the generator says where the fourth would have to be.
enum class DesignPivot { LowerFront, LowerRear, UpperFront, UpperRear };

/// Which side of the car the corner is generated on -- which side the template's
/// own point names are on. The far side then comes from the mirror rule.
enum class DesignSide { Left, Right };

/// One axle's design targets. Millimetres and degrees, ISO 8855.
struct AxleDesign {
    /// Whether this axle is generated at all, and into which of the linkage
    /// template's corners.
    bool generate = true;
    QString corner;

    /// Whether a steering rack drives this axle's tie rod. Written into the
    /// template's corner, because a generator that says nothing about steering
    /// hands back a car whose rear axle steers.
    bool steered = true;

    double track = 1220.0; ///< between the contact patch centres

    double camber = -0.3; ///< static; negative leans the top of the wheel in
    double toe = 0.0;     ///< static; positive points the front of the wheel inboard

    /// The steering axis: its lean in side view and in front view, and where it
    /// meets the ground against the contact patch.
    double caster = 4.0;             ///< positive leans the top rearward
    double kingpinInclination = 2.0; ///< positive leans the top inboard
    double scrubRadius = 40.0;       ///< positive puts the tyre outboard of the axis
    double mechanicalTrail = 5.0;    ///< positive puts the contact patch behind the axis

    /// The front-view instant centre, from the roll centre it has to produce.
    double rollCentreHeight = 30.0;
    double frontViewSwingArm = 8000.0;

    /// The side-view instant centre: anti-dive on the front, anti-lift on the
    /// rear, both under braking, in percent.
    double antiPercent = 15.0;
    double sideViewSwingArm = 1500.0;

    /// Where the two ball joints go inside the wheel: this far above and below
    /// the wheel centre, on the steering axis.
    double upperJointHeight = 90.0;
    double lowerJointDrop = 90.0;

    /// The chassis pivot lines, as distances from the car's centreline. Where the
    /// pivots go without a chassis to put them on.
    double upperPivotY = 200.0;
    double lowerPivotY = 200.0;

    /// Planform: the angle each leg makes in top view with a line straight across
    /// the car, swept forward or rearward.
    double upperForwardAngle = 30.0;
    double upperRearwardAngle = 30.0;
    double lowerForwardAngle = 15.0;
    double lowerRearwardAngle = 25.0;

    /// The steering arm, from the wheel centre to the outer tie rod end.
    /// Positive puts the tie rod behind the wheel centre.
    double steeringArm = 70.0;
    /// How far inboard of the steering axis the outer tie rod end sits.
    double ackermann = 15.0;
    /// How far ahead of its outer end the inner tie rod end is put.
    double tieRodInboardOffsetX = 0.0;
    DesignPivot advisedPivot = DesignPivot::LowerFront;

    bool operator==(const AxleDesign& other) const = default;
};

/// Everything the hardpoint generator is given: the car, then each axle.
///
/// The defaults are the 2025 car -- the parameter set of the Python Geometry
/// Editor this construction is ported from -- so that a project opening the
/// generator for the first time sees a suspension rather than zeros.
struct DesignParameters {
    double wheelbase = 1530.0;
    double cogX = -1350.0;         ///< the centre of gravity, in the car's frame
    double cogHeight = 300.0;
    double frontWeight = 47.0;     ///< % of the static load on the front axle
    double frontBrakeBias = 55.0;  ///< % of the braking done by the front axle
    double loadedRadius = 228.6;   ///< the wheel centre's height above the ground
    double rimRadius = 127.0;      ///< a ball joint further from the wheel axis is outside the rim

    /// Put the inboard pivots against the imported geometry, a clearance off
    /// its surface, rather than on the pivot lines. Only worth it when that
    /// geometry is the chassis on its own.
    bool useChassis = false;
    double chassisClearance = 20.0;

    DesignSide side = DesignSide::Left;
    AxleDesign front = frontDefaults();
    AxleDesign rear = rearDefaults();

    static AxleDesign frontDefaults();
    static AxleDesign rearDefaults();

    const AxleDesign& axle(AxlePosition position) const
    {
        return position == AxlePosition::Front ? front : rear;
    }
    AxleDesign& axle(AxlePosition position)
    {
        return position == AxlePosition::Front ? front : rear;
    }

    bool operator==(const DesignParameters& other) const = default;
};

/// The manifest's form, next to the mirror rule and the wheels. Round-trip
/// safe, and a key that is missing reads back as its default -- so a manifest
/// written before some parameter existed still opens.
QJsonObject designParametersToJson(const DesignParameters& parameters);
DesignParameters designParametersFromJson(const QJsonObject& object);

QString axlePositionLabel(AxlePosition position);
QString designPivotToString(DesignPivot pivot);
DesignPivot designPivotFromString(const QString& text, DesignPivot fallback = DesignPivot::LowerFront);

} // namespace suspkin
