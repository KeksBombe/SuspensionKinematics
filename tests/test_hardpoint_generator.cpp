#include "geom/MeshQuery.h"
#include "io/LinkageTemplate.h"
#include "io/XlsxHardpoints.h"
#include "model/DesignParameters.h"
#include "model/HardpointGenerator.h"
#include "model/SuspensionSolver.h"
#include "model/Sweep.h"
#include "project/Project.h"

#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>

#include <cmath>
#include <tuple>
#include <utility>

using namespace suspkin;

namespace {

constexpr double kRadToDeg = 57.295779513082320876798154814105;
constexpr double kDegToRad = 1.0 / kRadToDeg;

/// Tight: every one of these is a closed-form construction, and anything
/// looser would let a sign error in a small angle through.
constexpr double kTight = 1e-9;

/// The 2025 car, which is what the defaults are: the parameter set of the
/// Python Geometry Editor this construction was ported from.
DesignParameters car2025() { return DesignParameters{}; }

bool near(double a, double b, double tolerance = kTight) { return std::abs(a - b) <= tolerance; }

/// Distance from @p p to the plane through @p on with @p normal.
double offPlane(const Vec3& p, const Vec3& on, const Vec3& normal)
{
    return std::abs(dot(p - on, normal.normalized()));
}

/// A box, as a triangle mesh: what a chassis tube looks like from outside.
TriMesh box(const Vec3& min, const Vec3& max)
{
    TriMesh mesh;
    for (int i = 0; i < 8; ++i) {
        mesh.positions.push_back(QVector3D(float((i & 1) ? max.x : min.x),
                                           float((i & 2) ? max.y : min.y),
                                           float((i & 4) ? max.z : min.z)));
    }
    const uint32_t faces[12][3] = { { 0, 2, 1 }, { 1, 2, 3 }, { 4, 5, 6 }, { 5, 7, 6 },
                                    { 0, 1, 4 }, { 1, 5, 4 }, { 2, 6, 3 }, { 3, 6, 7 },
                                    { 0, 4, 2 }, { 2, 4, 6 }, { 1, 3, 5 }, { 3, 7, 5 } };
    for (const auto& face : faces)
        for (const uint32_t index : face) mesh.indices.push_back(index);
    for (const QVector3D& p : mesh.positions) mesh.bounds.expand(p);
    return mesh;
}

/// Two meshes as one, the way an assembly exported from CAD arrives.
TriMesh merged(const TriMesh& a, const TriMesh& b)
{
    TriMesh mesh = a;
    const auto offset = static_cast<uint32_t>(a.positions.size());
    mesh.positions.insert(mesh.positions.end(), b.positions.begin(), b.positions.end());
    for (const uint32_t index : b.indices) mesh.indices.push_back(index + offset);
    for (const QVector3D& p : b.positions) mesh.bounds.expand(p);
    return mesh;
}

/// The 2025 car with its upper pivot line further out than its lower one --
/// which is what makes the four chassis pivots stop being coplanar by
/// themselves. With both lines at the same distance they all sit in one
/// vertical plane and there is nothing to advise.
DesignParameters splayedPivots()
{
    DesignParameters p;
    p.front.upperPivotY = 230.0;
    p.rear.upperPivotY = 230.0;
    return p;
}

} // namespace

class TestHardpointGenerator : public QObject {
    Q_OBJECT

private slots:
    void theWheelSitsWhereTheCarPutsIt();
    void negativeCamberWalksThePatchOutboard();
    void theWheelAxisPointStatesCamberAndToe();
    void bothBallJointsAreOnTheSteeringAxis();
    void steeringAxisReproducesItsAngles();
    void theRollCentreComesOutWhereItWasAsked();
    void antiDiveIsMeasuredOverTheWholeWheelbase();
    void eachWishbonePlaneHoldsItsJointAndBothCentres();
    void theLegsKeepTheirPlanformAndEndOnTheirLine();
    void theTieRodIsBuiltTowardTheInstantCentre();
    void theAdviceIsWhereTheFourthPivotMeetsThePlane();
    void theRearTieRodUsesItsOwnSteeringArm();
    void aTargetThatDescribesNoSuspensionIsRefused();
    void theChassisPutsThePivotsAClearanceOffIt();
    void aChassisThatIsNotThereIsSaidRatherThanGuessed();

    void theGeneratorAndTheSolverAreInverses();
    void theNamesComeFromTheTemplate();
    void aGeneratedCarSweepsWithoutAWorkbook();
    void regeneratingSaysWhatItWillOverwrite();
    void theSteeringIsWrittenOnEveryAxleItGenerates();
    void theParametersRoundTripThroughTheManifest();
};

void TestHardpointGenerator::theWheelSitsWhereTheCarPutsIt()
{
    const DesignParameters p = car2025();
    const GeneratedCorner front = generateCorner(p, AxlePosition::Front);
    const GeneratedCorner rear = generateCorner(p, AxlePosition::Rear);
    QVERIFY2(front.ok, qPrintable(front.error));
    QVERIFY2(rear.ok, qPrintable(rear.error));

    // 47 % on the front: the front axle is 53 % of the wheelbase ahead of the
    // centre of gravity, the rear 47 % behind it.
    QVERIFY(near(front.at(DesignRole::WheelCenter).x, -1350.0 + 1530.0 * 0.53));
    QVERIFY(near(rear.at(DesignRole::WheelCenter).x, -1350.0 - 1530.0 * 0.47));
    QVERIFY(near(front.at(DesignRole::WheelCenter).z, 228.6));

    // The track is measured where the tyres touch.
    QVERIFY(near(front.at(DesignRole::ContactPatch).y, 610.0));
    QVERIFY(near(front.at(DesignRole::ContactPatch).z, 0.0));
    QVERIFY(near(rear.at(DesignRole::ContactPatch).y, 610.0));
    QVERIFY(front.side > 0.0);

    // And on the other side of the car when that is the side asked for.
    DesignParameters right = p;
    right.side = DesignSide::Right;
    const GeneratedCorner mirrored = generateCorner(right, AxlePosition::Front);
    QVERIFY(near(mirrored.at(DesignRole::ContactPatch).y, -610.0));
    QVERIFY(near(mirrored.at(DesignRole::UpperOuter).y, -front.at(DesignRole::UpperOuter).y));
}

void TestHardpointGenerator::negativeCamberWalksThePatchOutboard()
{
    DesignParameters p = car2025();
    p.front.camber = -2.0;
    const GeneratedCorner corner = generateCorner(p, AxlePosition::Front);
    QVERIFY2(corner.ok, qPrintable(corner.error));

    // A leaning wheel touches down on the outboard side of its centre -- which
    // is why the patch is computed and not dropped straight down (the Python
    // tool had camber fixed at zero here).
    const Vec3& wc = corner.at(DesignRole::WheelCenter);
    const Vec3& cp = corner.at(DesignRole::ContactPatch);
    QVERIFY(cp.y - wc.y > 0.0);
    QVERIFY(near(cp.y - wc.y, 228.6 * std::tan(2.0 * kDegToRad), 1e-6));
    QVERIFY(near(cp.z, 0.0));
    // The radius down the wheel's plane is the centre height over cos(camber).
    QVERIFY(near(corner.tyreRadius, 228.6 / std::cos(2.0 * kDegToRad)));
}

void TestHardpointGenerator::theWheelAxisPointStatesCamberAndToe()
{
    DesignParameters p = car2025();
    p.front.camber = -1.25;
    p.front.toe = 0.4;
    const GeneratedCorner corner = generateCorner(p, AxlePosition::Front);
    QVERIFY2(corner.ok, qPrintable(corner.error));

    const Vec3 axis =
        (corner.at(DesignRole::WheelAxis) - corner.at(DesignRole::WheelCenter)).normalized();
    // Read back the way the solver reads it: camber off the vertical component,
    // toe off the plan view -- the only place a hardpoint table can state toe.
    QVERIFY(near(-std::asin(axis.z) * kRadToDeg, -1.25));
    QVERIFY(near(std::atan2(axis.x, axis.y) * kRadToDeg, 0.4));
    QVERIFY(axis.y > 0.0); // outboard on the left
}

void TestHardpointGenerator::bothBallJointsAreOnTheSteeringAxis()
{
    const DesignParameters p = car2025();
    const GeneratedCorner corner = generateCorner(p, AxlePosition::Front);
    QVERIFY2(corner.ok, qPrintable(corner.error));

    for (const DesignRole role : { DesignRole::UpperOuter, DesignRole::LowerOuter }) {
        const Vec3 fromPierce = corner.at(role) - corner.steeringPierce;
        QVERIFY(cross(fromPierce, corner.steeringDirection).length() < kTight);
    }
    // At the heights the rim leaves room for.
    QVERIFY(near(corner.at(DesignRole::UpperOuter).z, 228.6 + 90.0));
    QVERIFY(near(corner.at(DesignRole::LowerOuter).z, 228.6 - 90.0));
    // Inside the rim: nothing to warn about with the 2025 car.
    QVERIFY2(corner.warnings.isEmpty(), qPrintable(corner.warnings.join(QLatin1Char('\n'))));
}

void TestHardpointGenerator::steeringAxisReproducesItsAngles()
{
    DesignParameters p = car2025();
    p.front.caster = 6.5;
    p.front.kingpinInclination = 8.0;
    p.front.scrubRadius = 25.0;
    p.front.mechanicalTrail = 18.0;
    const GeneratedCorner corner = generateCorner(p, AxlePosition::Front);
    QVERIFY2(corner.ok, qPrintable(corner.error));

    // Measured back off the two joints the way the solver measures them.
    const Vec3 axis = corner.at(DesignRole::UpperOuter) - corner.at(DesignRole::LowerOuter);
    QVERIFY(near(std::atan2(-axis.x, axis.z) * kRadToDeg, 6.5));
    QVERIFY(near(std::atan2(-axis.y, axis.z) * kRadToDeg, 8.0));

    // Where it meets the ground, against the patch.
    const Vec3& cp = corner.at(DesignRole::ContactPatch);
    QVERIFY(near(cp.y - corner.steeringPierce.y, 25.0));
    QVERIFY(near(corner.steeringPierce.x - cp.x, 18.0));
    QVERIFY(near(corner.steeringPierce.z, 0.0));
}

void TestHardpointGenerator::theRollCentreComesOutWhereItWasAsked()
{
    const DesignParameters p = car2025();
    for (const AxlePosition axle : { AxlePosition::Front, AxlePosition::Rear }) {
        const GeneratedCorner corner = generateCorner(p, axle);
        QVERIFY2(corner.ok, qPrintable(corner.error));
        const Vec3& cp = corner.at(DesignRole::ContactPatch);
        const Vec3& ic = corner.rollCentre;

        // A swing arm's length inboard of the patch.
        QVERIFY(near(cp.y - ic.y, p.axle(axle).frontViewSwingArm));
        // And the line from the patch through it crosses the centreline at the
        // roll centre asked for.
        const double atCentreline = cp.z + (ic.z - cp.z) * (0.0 - cp.y) / (ic.y - cp.y);
        QVERIFY(near(atCentreline, p.axle(axle).rollCentreHeight));
    }
}

void TestHardpointGenerator::antiDiveIsMeasuredOverTheWholeWheelbase()
{
    const DesignParameters p = car2025();
    const GeneratedCorner front = generateCorner(p, AxlePosition::Front);
    const GeneratedCorner rear = generateCorner(p, AxlePosition::Rear);
    QVERIFY(front.ok && rear.ok);

    // tan(theta) = anti * h / (L * share): the whole wheelbase, not half of it
    // (which is what the Python source divided by), and this axle's own share of
    // the braking.
    const auto slope = [](const GeneratedCorner& corner) {
        const Vec3 arm = corner.pitchCentre - corner.at(DesignRole::ContactPatch);
        return arm.z / std::abs(arm.x);
    };
    QVERIFY(near(slope(front), 0.15 * 300.0 / (1530.0 * 0.55)));
    QVERIFY(near(slope(rear), 0.25 * 300.0 / (1530.0 * 0.45)));

    // Walked out from the patch -- toward the rear for the front axle, toward
    // the front for the rear -- by the swing arm's length.
    QVERIFY(front.pitchCentre.x < front.at(DesignRole::ContactPatch).x);
    QVERIFY(rear.pitchCentre.x > rear.at(DesignRole::ContactPatch).x);
    QVERIFY(near(distance(front.pitchCentre, front.at(DesignRole::ContactPatch)), 1500.0));

    // An axle that does no braking has no anti-anything to speak of.
    DesignParameters allFront = p;
    allFront.frontBrakeBias = 100.0;
    const GeneratedCorner unbraked = generateCorner(allFront, AxlePosition::Rear);
    QVERIFY(unbraked.ok);
    QVERIFY(near(slope(unbraked), 0.0));
    QVERIFY(!unbraked.warnings.isEmpty());
}

void TestHardpointGenerator::eachWishbonePlaneHoldsItsJointAndBothCentres()
{
    const DesignParameters p = car2025();
    for (const AxlePosition axle : { AxlePosition::Front, AxlePosition::Rear }) {
        const GeneratedCorner c = generateCorner(p, axle);
        QVERIFY2(c.ok, qPrintable(c.error));

        // Relative to the size of the thing: the swing arm is eight metres, so
        // a micron of plane is 1e-10 of it.
        const double scale = 1e-9 * 10000.0;
        for (const auto& [outer, normal, pivots] :
             { std::tuple{ DesignRole::UpperOuter, c.upperNormal,
                           std::pair{ DesignRole::UpperFront, DesignRole::UpperRear } },
               std::tuple{ DesignRole::LowerOuter, c.lowerNormal,
                           std::pair{ DesignRole::LowerFront, DesignRole::LowerRear } } }) {
            const Vec3& joint = c.at(outer);
            QVERIFY(offPlane(c.rollCentre, joint, normal) < scale);
            QVERIFY(offPlane(c.pitchCentre, joint, normal) < scale);
            // The legs were laid in it, so the pivots are in it too.
            QVERIFY(offPlane(c.at(pivots.first), joint, normal) < scale);
            QVERIFY(offPlane(c.at(pivots.second), joint, normal) < scale);
        }
    }
}

void TestHardpointGenerator::theLegsKeepTheirPlanformAndEndOnTheirLine()
{
    const DesignParameters p = car2025();
    const GeneratedCorner c = generateCorner(p, AxlePosition::Front);
    QVERIFY2(c.ok, qPrintable(c.error));

    const auto planform = [&](DesignRole pivot, DesignRole outer) {
        const Vec3 leg = c.at(pivot) - c.at(outer);
        return std::atan2(std::abs(leg.x), std::abs(leg.y)) * kRadToDeg;
    };
    // The angle in top view, off a line straight across the car -- a tangent,
    // where the Python tool used a sine and came out 3.4 degrees short at 30.
    QVERIFY(near(planform(DesignRole::UpperFront, DesignRole::UpperOuter), 30.0));
    QVERIFY(near(planform(DesignRole::UpperRear, DesignRole::UpperOuter), 30.0));
    QVERIFY(near(planform(DesignRole::LowerFront, DesignRole::LowerOuter), 15.0));
    QVERIFY(near(planform(DesignRole::LowerRear, DesignRole::LowerOuter), 25.0));
    // Forward and rearward mean what they say.
    QVERIFY(c.at(DesignRole::UpperFront).x > c.at(DesignRole::UpperOuter).x);
    QVERIFY(c.at(DesignRole::UpperRear).x < c.at(DesignRole::UpperOuter).x);

    for (const DesignRole pivot : { DesignRole::UpperFront, DesignRole::UpperRear,
                                    DesignRole::LowerFront, DesignRole::LowerRear })
        QVERIFY(near(c.at(pivot).y, 200.0));
}

void TestHardpointGenerator::theTieRodIsBuiltTowardTheInstantCentre()
{
    const DesignParameters p = car2025();
    const GeneratedCorner c = generateCorner(p, AxlePosition::Front);
    QVERIFY2(c.ok, qPrintable(c.error));

    const Vec3& outer = c.at(DesignRole::TieRodOutboard);
    const Vec3& inner = c.at(DesignRole::TieRodInboard);

    // Behind the wheel centre by the steering arm, at the lower packaging
    // circle's height, the Ackermann offset inboard of the steering axis.
    const Vec3& wc = c.at(DesignRole::WheelCenter);
    QVERIFY(near(outer.x, wc.x - 70.0));
    QVERIFY(near(outer.z, wc.z - std::sqrt(90.0 * 90.0 - 70.0 * 70.0)));
    const double axisY = c.steeringPierce.y
                         + c.steeringDirection.y * (outer.z - c.steeringPierce.z) / c.steeringDirection.z;
    QVERIFY(near(outer.y, axisY - 15.0));

    // In front view the tie rod points at the instant centre...
    const double t = (inner.y - outer.y) / (c.rollCentre.y - outer.y);
    QVERIFY(near(outer.z + t * (c.rollCentre.z - outer.z), inner.z, 1e-6));
    // ...and its inner end is on the plane of the three pivots the advice
    // leaves standing.
    const Vec3 normal = cross(c.at(DesignRole::UpperRear) - c.at(DesignRole::UpperFront),
                              c.at(DesignRole::LowerRear) - c.at(DesignRole::UpperFront));
    QVERIFY(offPlane(inner, c.at(DesignRole::UpperFront), normal) < 1e-6);
    // Its x is the outer end's plus the offset, which moves nothing in front view.
    QVERIFY(near(inner.x, outer.x + 0.0));
    QVERIFY(inner.y < outer.y); // inboard
}

void TestHardpointGenerator::theAdviceIsWhereTheFourthPivotMeetsThePlane()
{
    // Both pivot lines at one distance put all four pivots in one vertical
    // plane, which is already what the advice would ask for.
    const GeneratedCorner square = generateCorner(car2025(), AxlePosition::Front);
    QVERIFY(square.ok && square.advice.valid);
    QVERIFY(square.advice.distance < 1e-9);

    const DesignParameters p = splayedPivots();
    const GeneratedCorner c = generateCorner(p, AxlePosition::Front);
    QVERIFY2(c.ok, qPrintable(c.error));
    QVERIFY(c.advice.valid);
    QCOMPARE(c.advice.pivot, DesignRole::LowerFront);
    QVERIFY(c.advice.distance > 1.0);

    // Advice, not applied: the pivot is still on its line.
    QVERIFY(near(c.at(DesignRole::LowerFront).y, 200.0));
    QVERIFY(near(c.advice.distance, distance(c.advice.placed, c.advice.advised)));

    // The advised point is on the plane of the other three and the tie rod...
    const Vec3 normal = cross(c.at(DesignRole::UpperRear) - c.at(DesignRole::UpperFront),
                              c.at(DesignRole::LowerRear) - c.at(DesignRole::UpperFront));
    QVERIFY(offPlane(c.advice.advised, c.at(DesignRole::UpperFront), normal) < 1e-6);
    // ...and on its own leg, so the arm keeps its planform.
    const Vec3 leg = c.at(DesignRole::LowerFront) - c.at(DesignRole::LowerOuter);
    const Vec3 advisedLeg = c.advice.advised - c.at(DesignRole::LowerOuter);
    QVERIFY(cross(leg.normalized(), advisedLeg.normalized()).length() < 1e-9);

    // Asking for a different pivot to be the advised one moves the tie rod,
    // not the pivots.
    DesignParameters other = p;
    other.front.advisedPivot = DesignPivot::UpperRear;
    const GeneratedCorner d = generateCorner(other, AxlePosition::Front);
    QVERIFY(d.ok);
    QCOMPARE(d.advice.pivot, DesignRole::UpperRear);
    QVERIFY(near(distance(d.at(DesignRole::LowerFront), c.at(DesignRole::LowerFront)), 0.0));
}

void TestHardpointGenerator::theRearTieRodUsesItsOwnSteeringArm()
{
    // geo_math.py:381 took the rear tie rod's height from the FRONT steering
    // arm, which moved it 57 mm with the shipped numbers.
    const DesignParameters p = car2025();
    const GeneratedCorner rear = generateCorner(p, AxlePosition::Rear);
    QVERIFY2(rear.ok, qPrintable(rear.error));
    const Vec3& wc = rear.at(DesignRole::WheelCenter);
    // The rear arm is exactly as long as the packaging circle is wide, so the
    // end is on the circle level with the wheel centre: nothing to warn about.
    QVERIFY(near(rear.at(DesignRole::TieRodOutboard).x, wc.x - 90.0));
    QVERIFY(near(rear.at(DesignRole::TieRodOutboard).z, wc.z));
    QVERIFY2(rear.warnings.isEmpty(), qPrintable(rear.warnings.join(QLatin1Char('\n'))));

    DesignParameters shorter = p;
    shorter.rear.steeringArm = 60.0;
    const GeneratedCorner fixedRear = generateCorner(shorter, AxlePosition::Rear);
    QVERIFY(near(fixedRear.at(DesignRole::TieRodOutboard).z,
                 wc.z - std::sqrt(90.0 * 90.0 - 60.0 * 60.0)));

    // Longer than the circle, there is nowhere on it to go, and it says so.
    DesignParameters longer = p;
    longer.rear.steeringArm = 100.0;
    const GeneratedCorner longRear = generateCorner(longer, AxlePosition::Rear);
    QVERIFY(longRear.ok);
    QVERIFY(near(longRear.at(DesignRole::TieRodOutboard).z, wc.z));
    QCOMPARE(longRear.warnings.size(), 1);
}

void TestHardpointGenerator::aTargetThatDescribesNoSuspensionIsRefused()
{
    DesignParameters outboard = car2025();
    outboard.front.upperPivotY = 700.0; // outboard of the ball joint
    const GeneratedCorner a = generateCorner(outboard, AxlePosition::Front);
    QVERIFY(!a.ok);
    QVERIFY(!a.error.isEmpty());

    DesignParameters noArm = car2025();
    noArm.front.frontViewSwingArm = 0.0;
    QVERIFY(!generateCorner(noArm, AxlePosition::Front).ok);

    DesignParameters flat = car2025();
    flat.front.caster = 75.0;
    QVERIFY(!generateCorner(flat, AxlePosition::Front).ok);
}

void TestHardpointGenerator::theChassisPutsThePivotsAClearanceOffIt()
{
    // A chassis 360 mm across, running the length of the front corner.
    const MeshQuery chassis(box(Vec3(-1200.0, -180.0, 20.0), Vec3(200.0, 180.0, 600.0)));
    QVERIFY(!chassis.isEmpty());

    DesignParameters p = car2025();
    p.useChassis = true;
    p.chassisClearance = 20.0;
    const GeneratedCorner c = generateCorner(p, AxlePosition::Front, &chassis);
    QVERIFY2(c.ok, qPrintable(c.error));
    QCOMPARE(c.pivotsOnChassis, 4);

    for (const DesignRole pivot : { DesignRole::UpperFront, DesignRole::UpperRear,
                                    DesignRole::LowerFront, DesignRole::LowerRear }) {
        // The clearance off the surface, on the outside of it...
        QVERIFY(near(chassis.distanceTo(c.at(pivot)), 20.0, 1e-4));
        QVERIFY(c.at(pivot).y > 180.0);
        // ...and still on its leg, so the arm keeps its plane and planform.
        const bool upper = pivot == DesignRole::UpperFront || pivot == DesignRole::UpperRear;
        const Vec3& outer = c.at(upper ? DesignRole::UpperOuter : DesignRole::LowerOuter);
        const Vec3 normal = upper ? c.upperNormal : c.lowerNormal;
        QVERIFY(offPlane(c.at(pivot), outer, normal) < 1e-6);
    }

    // Asked not to, it leaves the chassis out of it.
    p.useChassis = false;
    const GeneratedCorner lines = generateCorner(p, AxlePosition::Front, &chassis);
    QCOMPARE(lines.pivotsOnChassis, 0);
    QVERIFY(near(lines.at(DesignRole::UpperFront).y, 200.0));
}

void TestHardpointGenerator::aChassisThatIsNotThereIsSaidRatherThanGuessed()
{
    // Geometry nowhere near the corner: every leg misses. The Python tool put
    // the pivot at y = 200 and said nothing; this says so and uses the line.
    const MeshQuery elsewhere(box(Vec3(5000.0, -100.0, 0.0), Vec3(6000.0, 100.0, 100.0)));
    DesignParameters p = car2025();
    p.useChassis = true;
    const GeneratedCorner c = generateCorner(p, AxlePosition::Front, &elsewhere);
    QVERIFY2(c.ok, qPrintable(c.error));
    QCOMPARE(c.pivotsOnChassis, 0);
    QCOMPARE(c.warnings.size(), 4);
    QVERIFY(near(c.at(DesignRole::LowerRear).y, 200.0));

    // An assembly with the upright in it: the upper ball joint sits inside the
    // upright's own body, so a ray from there meets the upright first and is
    // not a ray at the chassis. Those two pivots go on their line and say so;
    // the lower two, outside it, still find the chassis.
    const GeneratedCorner plain = generateCorner(car2025(), AxlePosition::Front);
    const Vec3 joint = plain.at(DesignRole::UpperOuter);
    const TriMesh assembly =
        merged(box(Vec3(-1200.0, -180.0, 20.0), Vec3(200.0, 180.0, 600.0)),
               box(joint - Vec3(15.0, 15.0, 15.0), joint + Vec3(15.0, 15.0, 15.0)));
    const MeshQuery withUpright(assembly);
    const GeneratedCorner d = generateCorner(p, AxlePosition::Front, &withUpright);
    QVERIFY2(d.ok, qPrintable(d.error));
    QCOMPARE(d.pivotsOnChassis, 2);
    QCOMPARE(d.warnings.size(), 2);
    QVERIFY(near(d.at(DesignRole::UpperFront).y, 200.0));
    QVERIFY(near(withUpright.distanceTo(d.at(DesignRole::LowerFront)), 20.0, 1e-4));
}

void TestHardpointGenerator::theGeneratorAndTheSolverAreInverses()
{
    // The round trip that matters: generate a corner, give it to the solver,
    // and read back every target that went in. Camber and toe only close the
    // loop because of the wheel axis point -- without it toe went in and
    // nothing came back out.
    DesignParameters p = car2025();
    p.front.camber = -1.5;
    p.front.toe = 0.25;
    p.front.caster = 5.5;
    p.front.kingpinInclination = 6.0;
    p.front.scrubRadius = 30.0;
    p.front.mechanicalTrail = 12.0;
    p.rear.camber = -2.0;
    p.rear.toe = -0.1;

    const LinkageTemplate templ = builtinLinkageTemplate();
    for (const AxlePosition axle : { AxlePosition::Front, AxlePosition::Rear }) {
        const GeneratedCorner corner = generateCorner(p, axle);
        QVERIFY2(corner.ok, qPrintable(corner.error));

        const MechanismTemplate mechanism =
            instantiateMechanism(templ.mechanism, p.axle(axle).corner, false, MirrorSpec{});
        const HardpointTable table = bindGeneratedCorner(corner, mechanism);

        QString error;
        const std::optional<CornerSolver> solver = CornerSolver::bind(mechanism, table, &error);
        QVERIFY2(solver.has_value(), qPrintable(error));
        const CornerPose& pose = solver->designPose();
        QVERIFY2(pose.valid, qPrintable(pose.error));

        const AxleDesign& a = p.axle(axle);
        QVERIFY2(near(pose.camber, a.camber), qPrintable(QString::number(pose.camber)));
        QVERIFY2(near(pose.toe, a.toe), qPrintable(QString::number(pose.toe)));
        QVERIFY2(near(pose.caster, a.caster), qPrintable(QString::number(pose.caster)));
        QVERIFY(near(pose.kingpinInclination, a.kingpinInclination));
        QVERIFY2(near(pose.scrubRadius, a.scrubRadius, 1e-8),
                 qPrintable(QString::number(pose.scrubRadius)));
        QVERIFY(near(pose.mechanicalTrail, a.mechanicalTrail, 1e-8));
        // And the patch the solver computes is the one the generator put there.
        QVERIFY(distance(pose.contactPatch, corner.at(DesignRole::ContactPatch)) < 1e-9);

        // It moves, too: a few millimetres of bump assemble.
        const CornerPose bumped = solver->poseAtWheelTravel(20.0);
        QVERIFY2(bumped.valid, qPrintable(bumped.error));
    }
}

void TestHardpointGenerator::theNamesComeFromTheTemplate()
{
    const GeneratedCorner corner = generateCorner(car2025(), AxlePosition::Front);
    QVERIFY(corner.ok);

    // A team whose template calls its points something else gets their names,
    // and a role their template does not name is not written at all.
    MechanismTemplate theirs;
    theirs.lowerFront = QStringLiteral("FL_LOA_front");
    theirs.lowerRear = QStringLiteral("FL_LOA_rear");
    theirs.lowerOuter = QStringLiteral("FL_LOA_out");
    theirs.upperFront = QStringLiteral("FL_UPA_front");
    theirs.upperRear = QStringLiteral("FL_UPA_rear");
    theirs.upperOuter = QStringLiteral("FL_UPA_out");
    theirs.tieRodInboard = QStringLiteral("FL_TR_in");
    theirs.tieRodOutboard = QStringLiteral("FL_TR_out");
    theirs.wheelCenter = QStringLiteral("FL_WC");
    theirs.wheelAxis = QStringLiteral("FL_WA");
    // no contact patch: the solver computes it from the wheel axis anyway

    const HardpointTable table = bindGeneratedCorner(corner, theirs);
    QCOMPARE(int(table.size()), 10);
    QVERIFY(table.find(QStringLiteral("FL_LOA_front")));
    QVERIFY(!table.find(QStringLiteral("F_LCA_IF")));
    const Hardpoint* wc = table.find(QStringLiteral("FL_WC"));
    QVERIFY(wc);
    QVERIFY(near(wc->coord[2], 228.6));
}

void TestHardpointGenerator::aGeneratedCarSweepsWithoutAWorkbook()
{
    // The end-to-end check: an empty project, a generated car, and the analysis
    // sweeping it, without a workbook ever having been imported.
    const LinkageTemplate templ = builtinLinkageTemplate();
    const MirrorSpec mirror;
    const DesignPlan plan =
        planDesign(splayedPivots(), templ, mirror, HardpointTable{}, HardpointTable{});
    QVERIFY2(plan.ok(), qPrintable(plan.error));
    // Eleven roles, both axles, both sides.
    QCOMPARE(int(plan.table.size()), 11 * 2 * 2);
    QCOMPARE(plan.count(DesignChange::Kind::Added), 44);
    QCOMPARE(plan.handEditedCount(), 0);
    // One sentence of advice per axle, naming the pivot in the project's own
    // vocabulary.
    QCOMPARE(plan.advice.size(), 2);
    QVERIFY(plan.advice.front().contains(QStringLiteral("F_LCA_IF")));

    // Into a workbook made from the blank one, and back out: an ordinary
    // project from here on.
    QTemporaryDir directory;
    QString error;
    std::optional<Project> project =
        Project::create(directory.filePath(QStringLiteral("Generated")), QStringLiteral("G"), &error);
    QVERIFY2(project.has_value(), qPrintable(error));
    const std::optional<AssetRef> workbook = project->createHardpointWorkbook(plan.table, &error);
    QVERIFY2(workbook.has_value(), qPrintable(error));
    const HardpointLoadResult read = readHardpointsXlsx(project->absolutePath(workbook->relativePath));
    QVERIFY2(read.ok(), qPrintable(read.error));
    QCOMPARE(read.table->size(), plan.table.size());

    LinkageTemplate steered = templ;
    steered.corners = plan.steering;
    for (const CornerSpec& corner : steered.corners) {
        const AxleSolver axle =
            AxleSolver::build(steered.mechanism, corner, *read.table, mirror, steered.steeringDeclared());
        QVERIFY(axle.hasBothSides());
        QCOMPARE(axle.isSteered(), corner.token == QLatin1String("F"));

        SweepSettings settings;
        const SweepResult bump = runSweep(axle, settings.specFor(SweepKind::Bump));
        QVERIFY(!bump.isEmpty());
        for (const AxleSample& sample : bump.samples)
            QVERIFY2(sample.left.valid && sample.right.valid, qPrintable(sample.left.error));
        // Symmetric car, level ground: the roll centre is on the centreline.
        const AxleSample* design = bump.nearest(0.0);
        QVERIFY(design && design->rollCenterValid);
        QVERIFY(std::abs(design->rollCenterLateral) < 1e-6);
        // And at the height asked for, on both axles. The front's ball joints sit
        // a caster's worth ahead of and behind the wheel centre, which is what
        // tells the RCVD construction -- the arm planes, crossed with the
        // transverse plane -- from one that reads the arms off the joints
        // themselves. The solver used to do the latter and read 26.8 here.
        QVERIFY2(std::abs(design->rollCenterHeight - 30.0) < 1e-6,
                 qPrintable(QStringLiteral("%1: %2").arg(corner.token).arg(design->rollCenterHeight)));
    }
}

void TestHardpointGenerator::regeneratingSaysWhatItWillOverwrite()
{
    const LinkageTemplate templ = builtinLinkageTemplate();
    const MirrorSpec mirror;
    const DesignPlan first = planDesign(car2025(), templ, mirror, HardpointTable{}, HardpointTable{});
    QVERIFY2(first.ok(), qPrintable(first.error));

    // The workbook now holds what was generated. The user moves one point by
    // hand, and adds one of their own the generator knows nothing about.
    const HardpointTable baseline = first.table;
    HardpointTable current = baseline;
    const int edited = current.indexOf(QStringLiteral("F_UCA_IF"));
    current.points[std::size_t(edited)].coord[2] += 5.0;
    Hardpoint bracket;
    bracket.name = QStringLiteral("F_Camera");
    current.points.push_back(bracket);

    DesignParameters wider = car2025();
    wider.front.track = 1240.0;
    wider.rear.generate = false;
    const DesignPlan second = planDesign(wider, templ, mirror, current, baseline);
    QVERIFY2(second.ok(), qPrintable(second.error));

    // Only the front, both sides, and nothing new.
    QCOMPARE(second.count(DesignChange::Kind::Added), 0);
    QCOMPARE(int(second.changes.size()), 22);
    int handEdited = 0;
    for (const DesignChange& change : second.changes) {
        QVERIFY(change.name.startsWith(QStringLiteral("F_")));
        if (change.handEdited) {
            ++handEdited;
            QCOMPARE(change.name, QStringLiteral("F_UCA_IF"));
        }
    }
    QCOMPARE(handEdited, 1);
    // The far side moved with it.
    const Hardpoint* far = second.table.find(QStringLiteral("F_WheelCenter_M"));
    QVERIFY(far);
    QVERIFY(near(far->coord[1], -second.table.find(QStringLiteral("F_WheelCenter"))->coord[1]));
    // What the user added is theirs, and is left exactly where it was.
    QVERIFY(second.table.find(QStringLiteral("F_Camera")));
    // So is the rear, which was not asked for.
    QVERIFY(near(second.table.find(QStringLiteral("R_LCA_O"))->coord[1],
                 baseline.find(QStringLiteral("R_LCA_O"))->coord[1]));
}

void TestHardpointGenerator::theSteeringIsWrittenOnEveryAxleItGenerates()
{
    // A template from before steering was a role says nothing, so every axle
    // steers. Generating the rear alone, unsteered, must not stop the front.
    LinkageTemplate silent = builtinLinkageTemplate();
    for (CornerSpec& corner : silent.corners) {
        corner.steeringRack.clear();
        corner.steeringStated = false;
    }
    QVERIFY(!silent.steeringDeclared());

    DesignParameters rearOnly = car2025();
    rearOnly.front.generate = false;
    const DesignPlan plan = planDesign(rearOnly, silent, MirrorSpec{}, HardpointTable{}, HardpointTable{});
    QVERIFY2(plan.ok(), qPrintable(plan.error));
    QVERIFY(plan.steeringChanged);
    QCOMPARE(plan.steering.size(), std::size_t(2));
    QCOMPARE(plan.steering[0].steeringRack, silent.mechanism.tieRodInboard);
    QVERIFY(plan.steering[0].steeringStated);
    QVERIFY(plan.steering[1].steeringRack.isEmpty());
    QVERIFY(plan.steering[1].steeringStated);

    // The built-in already says front steered, rear not; generating both says
    // the same and changes nothing.
    const DesignPlan same =
        planDesign(car2025(), builtinLinkageTemplate(), MirrorSpec{}, HardpointTable{}, HardpointTable{});
    QVERIFY(same.ok());
    QVERIFY(!same.steeringChanged);

    // A corner the template does not have is not guessed at.
    DesignParameters stray = car2025();
    stray.rear.corner = QStringLiteral("M");
    QVERIFY(!planDesign(stray, builtinLinkageTemplate(), MirrorSpec{}, HardpointTable{},
                        HardpointTable{})
                 .ok());
}

void TestHardpointGenerator::theParametersRoundTripThroughTheManifest()
{
    DesignParameters p = car2025();
    p.wheelbase = 1555.5;
    p.useChassis = true;
    p.side = DesignSide::Right;
    p.front.toe = -0.125;
    p.front.advisedPivot = DesignPivot::UpperRear;
    p.rear.generate = false;
    p.rear.corner = QStringLiteral("H");
    p.rear.steered = true;

    QCOMPARE(designParametersFromJson(designParametersToJson(p)), p);
    // A key a later release adds, or an older manifest lacks, reads as its
    // default rather than as zero.
    QJsonObject partial = designParametersToJson(p);
    partial.remove(QStringLiteral("wheelbase"));
    QCOMPARE(designParametersFromJson(partial).wheelbase, DesignParameters{}.wheelbase);
    QCOMPARE(designParametersFromJson(QJsonObject{}).rear.scrubRadius, 100.0);

    QTemporaryDir directory;
    QString error;
    std::optional<Project> project =
        Project::create(directory.filePath(QStringLiteral("D")), QStringLiteral("D"), &error);
    QVERIFY2(project.has_value(), qPrintable(error));
    QVERIFY(!project->design().has_value());
    project->setDesign(p);
    QVERIFY(project->save(&error));
    const std::optional<Project> reopened = Project::open(project->manifestPath(), &error);
    QVERIFY2(reopened.has_value(), qPrintable(error));
    QVERIFY(reopened->design().has_value());
    QCOMPARE(*reopened->design(), p);
}

QTEST_APPLESS_MAIN(TestHardpointGenerator)
#include "test_hardpoint_generator.moc"
