#include "io/LinkageTemplate.h"
#include "model/GeomSolve.h"
#include "model/Mechanism.h"
#include "model/SuspensionSolver.h"
#include "model/Sweep.h"

#include <QTest>

#include <cmath>

using namespace suspkin;

namespace {

Hardpoint make(const char* name, double x, double y, double z)
{
    Hardpoint point;
    point.name = QLatin1String(name);
    point.coord[0] = x;
    point.coord[1] = y;
    point.coord[2] = z;
    return point;
}

/// A front left corner in the frame the tool uses: x forward, y left, z up, in
/// millimetres. Deliberately a plausible car rather than a tidy one -- the upper
/// wishbone is shorter than the lower, the tie rod is behind the wheel centre,
/// and the pushrod picks up on the upper arm, which is the layout the whole
/// exercise is about.
HardpointTable frontLeftCorner()
{
    HardpointTable table;
    table.points = {
        make("F_LCA_IF", 100.0, 200.0, 120.0),
        make("F_LCA_IR", -100.0, 200.0, 120.0),
        make("F_LCA_O", 0.0, 560.0, 110.0),
        make("F_UCA_IF", 80.0, 230.0, 280.0),
        make("F_UCA_IR", -80.0, 230.0, 280.0),
        make("F_UCA_O", 0.0, 540.0, 300.0),
        make("F_TieRod_I", -120.0, 220.0, 150.0),
        make("F_TieRod_O", -120.0, 555.0, 145.0),
        make("F_WheelCenter", 0.0, 600.0, 220.0),
        make("F_ContactPatch", 0.0, 600.0, 0.0),
        // The rocker: a 70 mm arm to the pushrod, near enough perpendicular to
        // it, a 60 mm arm to the damper perpendicular to that, and a 50 mm arm
        // to the anti-roll drop link. Proportioned like a real one, because a
        // rocker whose arm is short next to its pushrod sits at the end of its
        // travel at the design position and will not solve either side of it.
        make("F_PushRod_O", 0.0, 500.0, 295.0),
        make("F_PushRod_I", 0.0, 185.4, 560.4),
        make("F_Rocker_Center", 0.0, 150.0, 500.0),
        make("F_Rocker_AxisPoint", 100.0, 150.0, 500.0),
        make("F_Damper_O", 0.0, 95.0, 524.0),
        make("F_Damper_I", 0.0, 15.0, 341.0),
        // A U-bar across the car: its arm root is on the bar's own axis, and
        // the drop link joins that arm to the rocker.
        make("F_AntiRoll_O", 0.0, 180.0, 540.0),
        make("F_AntiRoll_I", -20.0, 200.0, 470.0),
        make("F_AntiRoll_Center", -100.0, 200.0, 450.0),
    };
    return table;
}

/// The direction a wheel's axle points, outboard on the left, for a given
/// static attitude. Toe-in and negative camber are both what the tool calls
/// positive and negative, so the numbers read as they would on a setup sheet.
Vec3 spinAxisAt(double toeDeg, double camberDeg)
{
    const double toe = toeDeg * M_PI / 180.0;
    const double camber = camberDeg * M_PI / 180.0;
    return Vec3(std::sin(toe) * std::cos(camber), std::cos(toe) * std::cos(camber),
                -std::sin(camber));
}

/// The same corner with a point on the wheel's own axis of rotation: 2 degrees
/// of toe-in and 3 of negative camber, which is an attitude no other hardpoint
/// in the table can state -- toe least of all.
HardpointTable frontLeftCornerWithAxis(double toeDeg = 2.0, double camberDeg = -3.0)
{
    HardpointTable table = frontLeftCorner();
    const Hardpoint* center = table.find(QStringLiteral("F_WheelCenter"));
    const Vec3 axis = spinAxisAt(toeDeg, camberDeg) * 150.0; // the stub axle's end
    table.points.push_back(make("F_WheelAxis", center->coord[0] + axis.x,
                                center->coord[1] + axis.y, center->coord[2] + axis.z));
    return table;
}

/// The roles those names play, written the way a template file writes them.
MechanismTemplate cornerMechanism()
{
    MechanismTemplate mechanism;
    mechanism.lowerFront = QStringLiteral("{corner}_LCA_IF");
    mechanism.lowerRear = QStringLiteral("{corner}_LCA_IR");
    mechanism.lowerOuter = QStringLiteral("{corner}_LCA_O");
    mechanism.upperFront = QStringLiteral("{corner}_UCA_IF");
    mechanism.upperRear = QStringLiteral("{corner}_UCA_IR");
    mechanism.upperOuter = QStringLiteral("{corner}_UCA_O");
    mechanism.tieRodInboard = QStringLiteral("{corner}_TieRod_I");
    mechanism.tieRodOutboard = QStringLiteral("{corner}_TieRod_O");
    // A corner bound on its own has to say whether a rack drives it: an empty
    // role means "no steering", full stop. The compatibility rule -- a template
    // that says nothing anywhere leaves every axle steered -- lives one level
    // up, in AxleSolver::build(), which is the only thing that can see a whole
    // template.
    mechanism.steeringRack = QStringLiteral("{corner}_TieRod_I");
    mechanism.wheelCenter = QStringLiteral("{corner}_WheelCenter");
    mechanism.wheelAxis = QStringLiteral("{corner}_WheelAxis");
    mechanism.contactPatch = QStringLiteral("{corner}_ContactPatch");
    mechanism.pushrodMount = PushrodMount::UpperArm;
    mechanism.pushrodOuter = QStringLiteral("{corner}_PushRod_O");
    mechanism.pushrodInner = QStringLiteral("{corner}_PushRod_I");
    mechanism.rockerPivot = QStringLiteral("{corner}_Rocker_Center");
    mechanism.rockerAxis = QStringLiteral("{corner}_Rocker_AxisPoint");
    mechanism.damperInboard = QStringLiteral("{corner}_Damper_I");
    mechanism.damperOutboard = QStringLiteral("{corner}_Damper_O");
    mechanism.antiRollRocker = QStringLiteral("{corner}_AntiRoll_O");
    mechanism.antiRollArmOuter = QStringLiteral("{corner}_AntiRoll_I");
    mechanism.antiRollArmPivot = QStringLiteral("{corner}_AntiRoll_Center");
    return mechanism;
}

/// The same corner, mirrored to the far side with the tool's own rule, which is
/// how a real project gets its other half.
HardpointTable frontAxle()
{
    return mirrorHardpoints(frontLeftCorner(), {}, MirrorSpec{}).table;
}

CornerSpec frontCorner()
{
    CornerSpec corner;
    corner.token = QStringLiteral("F");
    corner.label = QStringLiteral("Front");
    corner.steeringRack = QStringLiteral("{corner}_TieRod_I");
    return corner;
}

/// The same axle with no rack: a rear axle, in other words, whose toe link
/// inboard end is bolted to the chassis and stays there.
CornerSpec unsteeredCorner()
{
    CornerSpec corner = frontCorner();
    corner.steeringRack.clear();
    return corner;
}

/// A real rear axle: token R, and no rack.
CornerSpec rearCorner()
{
    CornerSpec corner;
    corner.token = QStringLiteral("R");
    corner.label = QStringLiteral("Rear");
    return corner;
}

/// @p front with a rear axle added: the same corners moved back by @p wheelbase
/// and renamed. All Ackermann asks of a rear axle is where it is.
HardpointTable withRearAxle(const HardpointTable& front, double wheelbase)
{
    HardpointTable table = front;
    for (const Hardpoint& point : front.points) {
        Hardpoint rear = point;
        rear.name = QStringLiteral("R") + point.name.mid(1);
        if (!rear.mirrorOf.isEmpty()) rear.mirrorOf = QStringLiteral("R") + rear.mirrorOf.mid(1);
        rear.coord[0] -= wheelbase;
        table.points.push_back(rear);
    }
    return table;
}

/// The front axle with its outer tie rod ends moved to @p y, which is what
/// swings the steering arms -- and so what sets the Ackermann.
HardpointTable frontAxleWithOuterTieRodAt(double y)
{
    HardpointTable corner = frontLeftCorner();
    for (Hardpoint& point : corner.points)
        if (point.name == QLatin1String("F_TieRod_O")) point.coord[1] = y;
    return mirrorHardpoints(corner, {}, MirrorSpec{}).table;
}

/// A rear axle for the same car, a wheelbase behind the front one. Its lower
/// wishbone is mounted 20 mm higher on the chassis, which tilts that arm down
/// towards the wheel and lifts the roll centre: a car whose roll axis climbs
/// towards the back, the way most of them do.
HardpointTable rearAxle()
{
    HardpointTable corner = frontLeftCorner();
    for (Hardpoint& point : corner.points) {
        point.name.replace(0, 1, QStringLiteral("R"));
        point.coord[0] -= 1550.0;
        if (point.name.startsWith(QLatin1String("R_LCA_I"))) point.coord[2] += 20.0;
    }
    return mirrorHardpoints(corner, {}, MirrorSpec{}).table;
}

CornerSolver bindTo(const HardpointTable& table)
{
    QString error;
    const MechanismTemplate named =
        instantiateMechanism(cornerMechanism(), QStringLiteral("F"), false, MirrorSpec{});
    std::optional<CornerSolver> solver = CornerSolver::bind(named, table, &error);
    if (!solver) qFatal("front corner did not bind: %s", qPrintable(error));
    return *solver;
}

/// The corner as every other test here has it: no axle line, so the wheel's
/// attitude is inferred from the patch under it.
CornerSolver bindFront() { return bindTo(frontLeftCorner()); }
/// The same corner, told outright which way its wheel points.
CornerSolver bindFrontWithAxis() { return bindTo(frontLeftCornerWithAxis()); }

} // namespace

class TestKinematics : public QObject {
    Q_OBJECT

private slots:
    // ---- the primitives -------------------------------------------------

    void rotatingAboutAnAxisKeepsTheDistanceToIt();
    void aCircleMeetsASphereTwiceOnceOrNotAtAll();
    void trilaterationFindsBothMirrorImages();
    void aRigidTransformIsRecoveredFromThreePoints();
    void parallelLinesHaveNoIntersection();
    void twoPlanesCrossAlongALine();

    // ---- the mechanism --------------------------------------------------

    void instantiationFillsInTheCornerAndTheMirror();
    void aTableMissingACornerIsAbsentRatherThanBroken();

    // ---- the solve ------------------------------------------------------

    void theDesignPositionIsAFixedPoint();
    void linkLengthsAreHeldThroughTheWholeSweep();
    void wheelTravelIsReachedToTheMicron();
    void aShorterUpperArmGainsNegativeCamberInBump();
    void steeringTheRackTurnsTheWheel();
    void theRockerAndDamperFollowTheWheel();
    void travelBeyondTheMechanismIsRefusedNotFaked();

    // ---- the wheel's own axis -------------------------------------------

    void theWheelAxisStatesCamberAndToeTogether();
    void theContactPatchIsComputedInTheWheelsOwnPlane();
    void aCornerWithNoContactPatchStillFindsTheGround();
    void theContactPatchWalksRoundTheTyreInsteadOfRidingTheUpright();
    void theUprightsMotionIsWhatTurnsTheWheelModel();
    void staticAnglesAreWhatTheWheelIsBuiltFrom();
    void staticAnglesWinOverTheTablesOwnWheelAxis();

    // ---- which axle has a steering rack ---------------------------------

    void anAxleWithNoRackIgnoresTheSteeringInputEntirely();
    void aTemplateThatSaysNothingLeavesEveryAxleSteered();
    void aRackThatPicksUpSomewhereElseIsRefusedWithAReason();
    void aSteerSweepOfAnUnsteeredAxleComesBackEmptyAndSaysWhy();

    // ---- the axle and the sweeps ----------------------------------------

    void anAxleSortsItsTwoSidesByWhereTheyAre();
    void aBumpSweepMovesBothWheelsTheSameWay();
    void aRollSweepMovesThemOppositeWaysAndTwistsTheBar();
    void theRollCentreIsOnTheCentrelineWhenTheAxleIsSymmetric();
    void theInstantCentreIsWhereTheArmPlanesCross();
    void theRollAxisRunsThroughEveryAxlesRollCentre();
    void rollingTheBodyAboutItLeavesTheTyresWhereTheyStand();
    void aRolledBodyLeansItsWheelsWithIt();
    void camberToGroundPartsFromCamberByTheRollAngle();
    void aSteerSweepChangesToeAndNotRideHeight();
    void theCsvHasOneRowPerStepAndSaysNothingAboutWhatDidNotSolve();
    void theShippedTemplateSolvesTheCornerItDescribes();

    // ---- Ackermann ------------------------------------------------------

    void ackermannIsMeasuredAgainstTheIdealOuterAngle();
    void theWheelbaseIsTakenToTheFarAxle();
    void aSteerSweepReadsAckermannTheSameTurningEitherWay();
    void ackermannIsReadOffTheWheelsThemselves();
    void angledSteeringArmsAddAckermann();

    // ---- what a sweep is asked for --------------------------------------

    void aTravelAndAnIncrementBecomeARangeAndAStepCount();
    void bumpAndReboundAreNotAssumedToBeEqual();
    void eachKindKeepsItsOwnTravelInItsOwnUnit();
    void anIncrementThatWouldNeverEndIsBoundedNotObeyed();

    // ---- how the numbers are shown --------------------------------------

    void aCurveIsNotStretchedPastWhatItsUnitCanSay();
    void whichSidesAPlotDrawsRoundTrips();
};

void TestKinematics::rotatingAboutAnAxisKeepsTheDistanceToIt()
{
    const Axis axis = axisThrough(Vec3(0, 0, 0), Vec3(0, 0, 5));
    const Vec3 point(10, 0, 3);
    const Vec3 turned = rotateAbout(point, axis, M_PI / 2.0);

    QVERIFY(std::abs(turned.x - 0.0) < 1e-9);
    QVERIFY(std::abs(turned.y - 10.0) < 1e-9);
    QVERIFY(std::abs(turned.z - 3.0) < 1e-9);
    QVERIFY(std::abs(axis.distanceTo(turned) - axis.distanceTo(point)) < 1e-12);

    // A full turn comes back to where it started.
    const Vec3 round = rotateAbout(point, axis, 2.0 * M_PI);
    QVERIFY(distance(round, point) < 1e-9);
}

void TestKinematics::aCircleMeetsASphereTwiceOnceOrNotAtAll()
{
    // The unit circle in the z = 0 plane.
    const Circle circle = circleAbout(Vec3(1, 0, 0), axisThrough(Vec3(), Vec3(0, 0, 1)));
    QCOMPARE(circle.radius, 1.0);

    Vec3 hits[2];
    // A sphere at the origin of radius 1 contains the whole circle: no crossing.
    QCOMPARE(intersectCircleSphere(circle, Vec3(), 1.0, hits), 0);

    // One centred out along x cuts it in two places, symmetric about x.
    QCOMPARE(intersectCircleSphere(circle, Vec3(2, 0, 0), 1.5, hits), 2);
    QVERIFY(std::abs(hits[0].x - hits[1].x) < 1e-9);
    QVERIFY(std::abs(hits[0].y + hits[1].y) < 1e-9);
    for (const Vec3& hit : hits) {
        QVERIFY(std::abs(hit.length() - 1.0) < 1e-9);
        QVERIFY(std::abs(distance(hit, Vec3(2, 0, 0)) - 1.5) < 1e-9);
    }

    // Tangent from outside: exactly one.
    QCOMPARE(intersectCircleSphere(circle, Vec3(3, 0, 0), 2.0, hits), 1);
    QVERIFY(distance(hits[0], Vec3(1, 0, 0)) < 1e-6);

    // Out of reach entirely.
    QCOMPARE(intersectCircleSphere(circle, Vec3(10, 0, 0), 1.0, hits), 0);
}

void TestKinematics::trilaterationFindsBothMirrorImages()
{
    const Vec3 a(0, 0, 0);
    const Vec3 b(4, 0, 0);
    const Vec3 c(0, 3, 0);
    const Vec3 truth(1, 1, 2);

    Vec3 hits[2];
    QCOMPARE(trilaterate(a, distance(a, truth), b, distance(b, truth), c, distance(c, truth), hits),
             2);

    // The two answers are the point and its reflection in the anchors' plane.
    const int match = distance(hits[0], truth) < distance(hits[1], truth) ? 0 : 1;
    QVERIFY(distance(hits[match], truth) < 1e-9);
    QVERIFY(std::abs(hits[1 - match].z + truth.z) < 1e-9);

    // Collinear anchors determine nothing.
    QCOMPARE(trilaterate(a, 1.0, b, 1.0, Vec3(8, 0, 0), 1.0, hits), 0);
}

void TestKinematics::aRigidTransformIsRecoveredFromThreePoints()
{
    const Vec3 from[3] = { Vec3(0, 0, 0), Vec3(10, 0, 0), Vec3(0, 7, 0) };
    const Axis axis = axisThrough(Vec3(1, 2, 3), Vec3(1, 2, 9));
    Vec3 to[3];
    for (int i = 0; i < 3; ++i) to[i] = rotateAbout(from[i], axis, 0.4) + Vec3(5, -2, 8);

    bool ok = false;
    const Rigid rigid = rigidFromTriangle(from, to, &ok);
    QVERIFY(ok);
    for (int i = 0; i < 3; ++i) QVERIFY(distance(rigid.map(from[i]), to[i]) < 1e-9);

    // A fourth point of the same body lands where the body puts it.
    const Vec3 extra(3, 4, 5);
    const Vec3 expected = rotateAbout(extra, axis, 0.4) + Vec3(5, -2, 8);
    QVERIFY(distance(rigid.map(extra), expected) < 1e-9);

    // Collinear input has no orientation to recover.
    const Vec3 flat[3] = { Vec3(0, 0, 0), Vec3(1, 0, 0), Vec3(2, 0, 0) };
    rigidFromTriangle(flat, flat, &ok);
    QVERIFY(!ok);
}

void TestKinematics::parallelLinesHaveNoIntersection()
{
    bool ok = true;
    intersectLines2D(Vec3(0, 0, 0), Vec3(0, 1, 0), Vec3(0, 0, 5), Vec3(0, 1, 5), 0, &ok);
    QVERIFY(!ok);

    const Vec3 hit =
        intersectLines2D(Vec3(0, 0, 0), Vec3(0, 2, 0), Vec3(0, 0, 1), Vec3(0, 2, 1), 0, &ok);
    QVERIFY(!ok);
    Q_UNUSED(hit);

    // Two arms that do meet, in the front view.
    const Vec3 crossing =
        intersectLines2D(Vec3(0, 0, 0), Vec3(0, 10, 10), Vec3(0, 0, 20), Vec3(0, 10, 10), 0, &ok);
    QVERIFY(ok);
    QVERIFY(std::abs(crossing.y - 10.0) < 1e-9);
    QVERIFY(std::abs(crossing.z - 10.0) < 1e-9);
}

void TestKinematics::twoPlanesCrossAlongALine()
{
    // z = 0 and y = 0 share the x axis, which crosses x = 5 at (5, 0, 0). The
    // normals are not unit length and the points are anywhere on the planes.
    bool ok = false;
    Vec3 hit = planesCrossing(Vec3(0, 0, 2), Vec3(1, 1, 0), Vec3(0, 3, 0), Vec3(7, 0, 4), 0, 5.0,
                              &ok);
    QVERIFY(ok);
    QVERIFY(distance(hit, Vec3(5, 0, 0)) < 1e-12);

    // z = y and y + z = 10 meet along y = z = 5, wherever along x it is asked.
    hit = planesCrossing(Vec3(0, -1, 1), Vec3(), Vec3(0, 1, 1), Vec3(0, 0, 10), 0, -3.0, &ok);
    QVERIFY(ok);
    QVERIFY(distance(hit, Vec3(-3, 5, 5)) < 1e-12);

    // Parallel planes share no line at all.
    planesCrossing(Vec3(0, 0, 1), Vec3(), Vec3(0, 0, 2), Vec3(0, 0, 5), 0, 0.0, &ok);
    QVERIFY(!ok);
    // And the x axis never reaches the plane y = 3.
    planesCrossing(Vec3(0, 0, 1), Vec3(), Vec3(0, 1, 0), Vec3(), 1, 3.0, &ok);
    QVERIFY(!ok);
}

void TestKinematics::instantiationFillsInTheCornerAndTheMirror()
{
    const MechanismTemplate base =
        instantiateMechanism(cornerMechanism(), QStringLiteral("R"), false, MirrorSpec{});
    QCOMPARE(base.lowerOuter, QStringLiteral("R_LCA_O"));
    QCOMPARE(base.pushrodInner, QStringLiteral("R_PushRod_I"));
    QCOMPARE(base.pushrodMount, PushrodMount::UpperArm);

    // The far side goes through the project's own rule, never a hard-coded one.
    const MechanismTemplate mirrored =
        instantiateMechanism(cornerMechanism(), QStringLiteral("R"), true, MirrorSpec{});
    QCOMPARE(mirrored.lowerOuter, QStringLiteral("R_LCA_O_M"));
    QCOMPARE(mirrored.antiRollArmPivot, QStringLiteral("R_AntiRoll_Center_M"));
}

void TestKinematics::aTableMissingACornerIsAbsentRatherThanBroken()
{
    const MechanismTemplate rear =
        instantiateMechanism(cornerMechanism(), QStringLiteral("R"), false, MirrorSpec{});
    const MechanismCoverage coverage = coverMechanism(rear, frontLeftCorner());
    QVERIFY(coverage.absent);
    QVERIFY(!coverage.solvable());
    // Nothing is reported: a workbook holding one axle is not a broken workbook.
    QVERIFY(coverage.missingRequired.isEmpty());
    QVERIFY(coverage.missingOptional.isEmpty());

    QString error;
    QVERIFY(!CornerSolver::bind(rear, frontLeftCorner(), &error).has_value());
    QVERIFY(!error.isEmpty());
}

void TestKinematics::theDesignPositionIsAFixedPoint()
{
    const CornerSolver solver = bindFront();
    const CornerPose& design = solver.designPose();
    QVERIFY(design.valid);
    QVERIFY(solver.isLeft());

    const HardpointTable table = frontLeftCorner();
    for (const PosedPoint& posed : design.points) {
        const Hardpoint* original = table.find(posed.name);
        QVERIFY2(original, qPrintable(posed.name));
        const Vec3 was(original->coord[0], original->coord[1], original->coord[2]);
        QVERIFY2(distance(posed.position, was) < 1e-9, qPrintable(posed.name));
    }

    QVERIFY(std::abs(design.wheelTravel) < 1e-9);
    QVERIFY(std::abs(design.camberChange) < 1e-12);
    QVERIFY(std::abs(design.toeChange) < 1e-12);
    QVERIFY(std::abs(design.damperTravel) < 1e-12);

    // This corner is drawn with the wheel centre straight above the contact
    // patch, so it has no static camber, and the tie rod is where it is, so it
    // has no static toe either.
    QVERIFY(std::abs(design.camber) < 1e-9);
    QVERIFY(std::abs(design.toe) < 1e-9);

    // The steering axis leans its top inboard by six degrees and stands upright
    // in side view, which is what the coordinates say.
    QVERIFY(std::abs(design.caster) < 1e-9);
    QVERIFY(std::abs(design.kingpinInclination - 6.0086) < 0.01);

    QVERIFY(design.hasDamper);
    QVERIFY(design.hasAntiRoll);
    QVERIFY(design.instantCenterValid);
}

void TestKinematics::linkLengthsAreHeldThroughTheWholeSweep()
{
    // The invariant that catches a wrong branch. Every rod in the mechanism has
    // a fixed length by construction; if the solve ever picks the other root of
    // a circle meeting a sphere, one of these lets go.
    const CornerSolver solver = bindFront();
    const HardpointTable table = frontLeftCorner();
    const CornerPose& design = solver.designPose();

    const auto lengthOf = [](const CornerPose& pose, const char* a, const char* b) {
        const Vec3* first = pose.find(QLatin1String(a));
        const Vec3* second = pose.find(QLatin1String(b));
        return first && second ? distance(*first, *second) : -1.0;
    };
    const auto chassisLength = [&table](const CornerPose& pose, const char* fixed,
                                        const char* moving) {
        const Hardpoint* anchor = table.find(QLatin1String(fixed));
        const Vec3* end = pose.find(QLatin1String(moving));
        if (!anchor || !end) return -1.0;
        return distance(Vec3(anchor->coord[0], anchor->coord[1], anchor->coord[2]), *end);
    };

    struct Rod {
        const char* a;
        const char* b;
        double design;
    };
    Rod rods[] = {
        { "F_LCA_O", "F_UCA_O", 0.0 },      { "F_LCA_O", "F_TieRod_O", 0.0 },
        { "F_UCA_O", "F_TieRod_O", 0.0 },   { "F_LCA_O", "F_WheelCenter", 0.0 },
        { "F_TieRod_I", "F_TieRod_O", 0.0 }, { "F_PushRod_O", "F_PushRod_I", 0.0 },
        { "F_AntiRoll_O", "F_AntiRoll_I", 0.0 },
    };
    for (Rod& rod : rods) rod.design = lengthOf(design, rod.a, rod.b);
    for (const Rod& rod : rods) QVERIFY2(rod.design > 0.0, rod.a);

    // Chassis-fixed pivots hold their own links too.
    const double lowerArm = chassisLength(design, "F_LCA_IF", "F_LCA_O");
    const double upperArm = chassisLength(design, "F_UCA_IF", "F_UCA_O");
    const double rockerArm = chassisLength(design, "F_Rocker_Center", "F_PushRod_I");
    const double barArm = chassisLength(design, "F_AntiRoll_Center", "F_AntiRoll_I");

    const CornerPose* previous = &design;
    CornerPose held;
    for (double travel = -40.0; travel <= 40.0001; travel += 1.0) {
        const CornerPose pose = solver.poseAtWheelTravel(travel, 0.0, previous);
        QVERIFY2(pose.valid, qPrintable(QStringLiteral("travel %1: %2")
                                            .arg(travel)
                                            .arg(pose.error)));
        for (const Rod& rod : rods) {
            const double now = lengthOf(pose, rod.a, rod.b);
            QVERIFY2(std::abs(now - rod.design) < 1e-6,
                     qPrintable(QStringLiteral("%1-%2 at %3 mm: %4 vs %5")
                                    .arg(QLatin1String(rod.a), QLatin1String(rod.b))
                                    .arg(travel)
                                    .arg(now)
                                    .arg(rod.design)));
        }
        QVERIFY(std::abs(chassisLength(pose, "F_LCA_IF", "F_LCA_O") - lowerArm) < 1e-6);
        QVERIFY(std::abs(chassisLength(pose, "F_UCA_IF", "F_UCA_O") - upperArm) < 1e-6);
        QVERIFY(std::abs(chassisLength(pose, "F_Rocker_Center", "F_PushRod_I") - rockerArm) < 1e-6);
        QVERIFY(std::abs(chassisLength(pose, "F_AntiRoll_Center", "F_AntiRoll_I") - barArm) < 1e-6);

        held = pose;
        previous = &held;
    }
}

void TestKinematics::wheelTravelIsReachedToTheMicron()
{
    const CornerSolver solver = bindFront();
    for (double travel : { -30.0, -12.5, 0.0, 7.25, 30.0 }) {
        const CornerPose pose = solver.poseAtWheelTravel(travel);
        QVERIFY2(pose.valid, qPrintable(pose.error));
        QVERIFY(std::abs(pose.wheelTravel - travel) < 1e-6);
    }

    // The contact patch is driven the same way, which is what a roll sweep asks
    // for: the ground moves, not the wheel.
    const CornerPose lifted = solver.poseAtContactPatchRise(15.0);
    QVERIFY2(lifted.valid, qPrintable(lifted.error));
    QVERIFY(std::abs(lifted.contactPatchRise - 15.0) < 1e-6);
}

void TestKinematics::aShorterUpperArmGainsNegativeCamberInBump()
{
    const CornerSolver solver = bindFront();
    const CornerPose bump = solver.poseAtWheelTravel(25.0);
    const CornerPose droop = solver.poseAtWheelTravel(-25.0);
    QVERIFY(bump.valid);
    QVERIFY(droop.valid);

    // The upper wishbone is the shorter of the two, so the top of the wheel is
    // pulled in as it rises. That is the whole reason it is drawn shorter.
    QVERIFY(bump.camberChange < -0.2);
    QVERIFY(droop.camberChange > 0.2);

    // The track narrows at the top of the travel and the contact patch scrubs.
    QVERIFY(std::abs(bump.halfTrackChange) > 0.0);

    // Camber runs one way across the whole travel -- most negative at the top,
    // most positive at the bottom -- with no step in it. A wrong branch at any
    // one step would show up here as a reversal.
    double last = -1e9;
    for (double travel = 30.0; travel >= -30.0; travel -= 2.5) {
        const CornerPose pose = solver.poseAtWheelTravel(travel);
        QVERIFY(pose.valid);
        QVERIFY2(pose.camber > last, qPrintable(QStringLiteral("camber %1 at %2 mm follows %3")
                                                    .arg(pose.camber)
                                                    .arg(travel)
                                                    .arg(last)));
        last = pose.camber;
    }
}

void TestKinematics::steeringTheRackTurnsTheWheel()
{
    const CornerSolver solver = bindFront();
    const CornerPose straight = solver.poseAtWheelTravel(0.0, 0.0);
    const CornerPose steered = solver.poseAtWheelTravel(0.0, 10.0);
    QVERIFY(steered.valid);

    // Moving the rack end changes toe and nothing about the ride height.
    QVERIFY(std::abs(steered.toeChange) > 0.5);
    QVERIFY(std::abs(steered.wheelTravel) < 1e-6);
    QVERIFY(std::abs(steered.camberChange - straight.camberChange) < 0.05);

    // The tie rod is still a tie rod.
    const Vec3* inner = steered.find(QStringLiteral("F_TieRod_I"));
    const Vec3* outer = steered.find(QStringLiteral("F_TieRod_O"));
    QVERIFY(inner && outer);
    const Vec3* wasInner = straight.find(QStringLiteral("F_TieRod_I"));
    const Vec3* wasOuter = straight.find(QStringLiteral("F_TieRod_O"));
    QVERIFY(std::abs(distance(*inner, *outer) - distance(*wasInner, *wasOuter)) < 1e-6);
    QVERIFY(std::abs(inner->y - wasInner->y - 10.0) < 1e-9);
}

void TestKinematics::theRockerAndDamperFollowTheWheel()
{
    const CornerSolver solver = bindFront();
    const CornerPose bump = solver.poseAtWheelTravel(25.0);
    const CornerPose droop = solver.poseAtWheelTravel(-25.0);

    QVERIFY(bump.hasDamper);
    // A damper does something: bump and droop are not the same length, and the
    // rocker turned to make that happen.
    QVERIFY(std::abs(bump.damperTravel - droop.damperTravel) > 1.0);
    QVERIFY(std::abs(bump.rockerAngle) > 1e-3);
    QVERIFY(bump.rockerAngle * droop.rockerAngle < 0.0); // opposite ways

    // A pushrod on the upper arm moves with the upper arm, not with the upright.
    QVERIFY(std::abs(bump.upperArmAngle) > 1e-3);

    // Bump closes the damper, which is the whole point of connecting one.
    QVERIFY(bump.damperTravel < 0.0);
    QVERIFY(droop.damperTravel > 0.0);
    // And by a believable amount for a pushrod car: the damper moves less than
    // the wheel does.
    const double ratio = (droop.damperTravel - bump.damperTravel) / 50.0;
    QVERIFY(ratio > 0.05);
    QVERIFY(ratio < 1.5);

    QVERIFY(bump.hasAntiRoll);
    QVERIFY(std::abs(bump.antiRollArmAngle) > 1e-4);
    QVERIFY(bump.antiRollArmAngle * droop.antiRollArmAngle < 0.0);
}

void TestKinematics::travelBeyondTheMechanismIsRefusedNotFaked()
{
    const CornerSolver solver = bindFront();
    // A metre of bump is not a suspension movement. It has to come back as a
    // failure with something to say, never as a pose that quietly stopped short.
    const CornerPose absurd = solver.poseAtWheelTravel(1000.0);
    QVERIFY(!absurd.valid);
    QVERIFY(!absurd.error.isEmpty());
}

void TestKinematics::theWheelAxisStatesCamberAndToeTogether()
{
    const CornerPose& design = bindFrontWithAxis().designPose();
    QVERIFY(std::abs(design.camber - (-3.0)) < 1e-6);
    QVERIFY(std::abs(design.toe - 2.0) < 1e-6);

    // The same table without that point cannot say either thing: a contact patch
    // straight under the wheel centre reads as a wheel standing square, which is
    // what every workbook without an axle line silently claims.
    const CornerPose& assumed = bindFront().designPose();
    QVERIFY(std::abs(assumed.camber) < 1e-6);
    QVERIFY(std::abs(assumed.toe) < 1e-6);
}

void TestKinematics::theContactPatchIsComputedInTheWheelsOwnPlane()
{
    const CornerPose& design = bindFrontWithAxis().designPose();

    // On the road, which is where the workbook's own patch was...
    QVERIFY(std::abs(design.contactPatch.z) < 1e-9);
    // ...square to the axle rather than straight down from the centre...
    const Vec3 arm = design.contactPatch - design.wheelCenter;
    QVERIFY(std::abs(dot(arm, design.spinAxis)) < 1e-9);
    // ...and outboard of the centre, because that is what negative camber does
    // to a contact patch and half the reason for computing it at all.
    QVERIFY(design.contactPatch.y > 600.0);
    QVERIFY(design.contactPatch.y < 615.0);
}

void TestKinematics::aCornerWithNoContactPatchStillFindsTheGround()
{
    HardpointTable table = frontLeftCornerWithAxis();
    const int patch = table.indexOf(QStringLiteral("F_ContactPatch"));
    QVERIFY(patch >= 0);
    table.points.erase(table.points.begin() + patch);

    // Nothing is lost by leaving the patch out of the workbook: the wheel centre,
    // the axle and the ground are between them enough to say where the tyre is.
    const CornerSolver solver = bindTo(table);
    QVERIFY(std::abs(solver.designPose().contactPatch.z) < 1e-9);

    // Including for a roll sweep, which asks a corner for a patch height rather
    // than a wheel height and had nothing real to aim at without one.
    const CornerPose lifted = solver.poseAtContactPatchRise(15.0);
    QVERIFY2(lifted.valid, qPrintable(lifted.error));
    QVERIFY(std::abs(lifted.contactPatchRise - 15.0) < 1e-6);
}

void TestKinematics::theContactPatchWalksRoundTheTyreInsteadOfRidingTheUpright()
{
    const CornerSolver solver = bindFrontWithAxis();
    const CornerPose& design = solver.designPose();
    const double radius = distance(design.wheelCenter, design.contactPatch);

    for (const double travel : { -30.0, -10.0, 10.0, 30.0 }) {
        const CornerPose pose = solver.poseAtWheelTravel(travel);
        QVERIFY2(pose.valid, qPrintable(pose.error));

        // A tyre does not lift off the road when the wheel leans: the patch stays
        // a tyre radius from the centre, in the wheel's own plane, at the bottom
        // of it. Carried rigidly with the upright it would swing out sideways.
        const Vec3 arm = pose.contactPatch - pose.wheelCenter;
        QVERIFY(std::abs(arm.length() - radius) < 1e-9);
        QVERIFY(std::abs(dot(arm, pose.spinAxis)) < 1e-9);
        QVERIFY(arm.z < 0.0);
    }
}

void TestKinematics::theUprightsMotionIsWhatTurnsTheWheelModel()
{
    const CornerSolver solver = bindFrontWithAxis();
    const CornerPose design = solver.designPose();
    const CornerPose steered = solver.poseAtWheelTravel(0.0, 12.0);
    QVERIFY2(steered.valid, qPrintable(steered.error));

    // The published motion is the upright's own: it takes the design geometry
    // onto the solved geometry. That is what a wheel *model* has to be turned
    // by -- a mesh cannot be laid over the table by name the way a point can.
    QVERIFY(distance(steered.uprightMotion.map(design.wheelCenter), steered.wheelCenter) < 1e-9);
    QVERIFY(distance(steered.uprightMotion.map(design.lowerOuter), steered.lowerOuter) < 1e-9);
    QVERIFY(distance(steered.uprightMotion.rotate(design.spinAxis), steered.spinAxis) < 1e-9);

    // And it really has turned: this is the steering the wheel models were not
    // doing. The name is published with it so the model can be found by it.
    QVERIFY(std::abs(steered.toeChange) > 0.5);
    QCOMPARE(steered.wheelCenterName, QStringLiteral("F_WheelCenter"));

    // The axis point itself rides the upright rigidly, so it is still an axle's
    // length from the centre once the wheel has been turned.
    const Vec3* axis = steered.find(QStringLiteral("F_WheelAxis"));
    QVERIFY(axis != nullptr);
    QVERIFY(std::abs(distance(*axis, steered.wheelCenter) - 150.0) < 1e-9);
}

void TestKinematics::staticAnglesAreWhatTheWheelIsBuiltFrom()
{
    // The corner whose only word on the wheel is a contact patch straight under
    // its centre -- an upright wheel -- told outright what Lotus's Set Static
    // Angles would tell it.
    const StaticAlignment stated{ -1.5, 0.25 };
    const AxleSolver axle = AxleSolver::build(cornerMechanism(), frontCorner(), frontAxle(),
                                              MirrorSpec{}, true, stated);
    QVERIFY(axle.hasBothSides());

    for (const std::optional<CornerSolver>* side : { &axle.left(), &axle.right() }) {
        const CornerSolver& solver = **side;
        const CornerPose& design = solver.designPose();
        QCOMPARE(solver.wheelAttitude(), WheelAttitude::Stated);
        // Exactly those numbers, on both sides: the far side is the mirror image,
        // which is the same camber and the same toe.
        QVERIFY(std::abs(design.camber - stated.camber) < 1e-12);
        QVERIFY(std::abs(design.toe - stated.toe) < 1e-12);
        QVERIFY(distance(design.spinAxis, spinAxisFor(stated, solver.side())) < 1e-12);

        // The contact patch is computed from them: on the road, square to the
        // axle, and walked outboard by the negative camber -- not where the
        // workbook's patch point was, straight under the centre.
        QVERIFY(std::abs(design.contactPatch.z) < 1e-9);
        QVERIFY(std::abs(dot(design.contactPatch - design.wheelCenter, design.spinAxis)) < 1e-9);
        QVERIFY(solver.side() * (design.contactPatch.y - design.wheelCenter.y) > 5.0);

        // And the curves are measured from there: a bump is still a bump, and
        // camber changes by what the wishbones do to it.
        const CornerPose bump = solver.poseAtWheelTravel(20.0);
        QVERIFY2(bump.valid, qPrintable(bump.error));
        QVERIFY(std::abs(bump.camberChange - (bump.camber - stated.camber)) < 1e-12);
    }

    // The table alone still decides for an axle nobody has stated angles for,
    // and says which point it went by.
    const AxleSolver bare =
        AxleSolver::build(cornerMechanism(), frontCorner(), frontAxle(), MirrorSpec{}, true);
    QCOMPARE(bare.left()->wheelAttitude(), WheelAttitude::ContactPatch);
    QVERIFY(std::abs(bare.left()->designPose().camber) < 1e-9);
    QCOMPARE(bindFrontWithAxis().wheelAttitude(), WheelAttitude::WheelAxis);
}

void TestKinematics::staticAnglesWinOverTheTablesOwnWheelAxis()
{
    // A table that says -3 degrees of camber and 2 of toe-in with an axle point,
    // and a project that says something else. The number typed in wins: the
    // point is what nobody has moved since.
    const MechanismTemplate named =
        instantiateMechanism(cornerMechanism(), QStringLiteral("F"), false, MirrorSpec{});
    QString error;
    const std::optional<CornerSolver> solver = CornerSolver::bind(
        named, frontLeftCornerWithAxis(), &error, StaticAlignment{ -0.5, -0.1 });
    QVERIFY2(solver.has_value(), qPrintable(error));
    const CornerPose& design = solver->designPose();
    QVERIFY(std::abs(design.camber - (-0.5)) < 1e-12);
    QVERIFY(std::abs(design.toe - (-0.1)) < 1e-12);

    // The axle point is carried on the axis that is actually being used, as far
    // from the centre as the table has it, so what is drawn is what is measured.
    const CornerPose steered = solver->poseAtWheelTravel(0.0, 8.0);
    QVERIFY2(steered.valid, qPrintable(steered.error));
    for (const CornerPose* pose : { &design, &steered }) {
        const Vec3* axis = pose->find(QStringLiteral("F_WheelAxis"));
        QVERIFY(axis != nullptr);
        const Vec3 reach = *axis - pose->wheelCenter;
        QVERIFY(std::abs(reach.length() - 150.0) < 1e-9);
        QVERIFY(cross(reach, pose->spinAxis).length() < 1e-9);
    }
}

// ---------------------------------------------------------------------------
// Which axle has a steering rack
// ---------------------------------------------------------------------------

void TestKinematics::anAxleWithNoRackIgnoresTheSteeringInputEntirely()
{
    MechanismTemplate mechanism = cornerMechanism();
    mechanism.steeringRack.clear(); // a rear axle: the toe link is chassis-bolted

    QString error;
    const MechanismTemplate named =
        instantiateMechanism(mechanism, QStringLiteral("F"), false, MirrorSpec{});
    std::optional<CornerSolver> solver = CornerSolver::bind(named, frontLeftCorner(), &error);
    QVERIFY2(solver.has_value(), qPrintable(error));
    QVERIFY(!solver->isSteered());

    // Not "steers a little", not "steers and is ignored downstream": the rack is
    // not there, so twelve millimetres of it changes nothing at all. This is the
    // bug it was written for -- a rear tie rod end walking out of the car.
    const CornerPose still = solver->poseAtWheelTravel(0.0, 0.0);
    const CornerPose asked = solver->poseAtWheelTravel(0.0, 12.0);
    QVERIFY2(asked.valid, qPrintable(asked.error));
    QCOMPARE(asked.rackTravel, 0.0);
    QVERIFY(distance(asked.tieRodInboard, still.tieRodInboard) < 1e-12);
    QVERIFY(distance(asked.wheelCenter, still.wheelCenter) < 1e-12);
    QVERIFY(std::abs(asked.toe - still.toe) < 1e-12);

    // And the same corner with the rack named still steers, so this is the role
    // doing the work and not the fixture.
    QVERIFY(bindFront().isSteered());
    QVERIFY(std::abs(bindFront().poseAtWheelTravel(0.0, 12.0).toeChange) > 0.5);
}

void TestKinematics::aTemplateThatSaysNothingLeavesEveryAxleSteered()
{
    // Every project made before the role existed relies on this: no corner names
    // a rack, so the axle keeps the steering it has always had.
    CornerSpec silent = frontCorner();
    silent.steeringRack.clear();
    MechanismTemplate mechanism = cornerMechanism();
    mechanism.steeringRack.clear();

    const AxleSolver assumed =
        AxleSolver::build(mechanism, silent, frontAxle(), MirrorSpec{}, false);
    QVERIFY(assumed.isSteered());
    QVERIFY(assumed.warnings().isEmpty());

    // The same silence, once the template has spoken elsewhere, means no.
    const AxleSolver declared =
        AxleSolver::build(mechanism, silent, frontAxle(), MirrorSpec{}, true);
    QVERIFY(!declared.isSteered());
    QVERIFY(declared.warnings().isEmpty()); // saying nothing is an answer, not a fault
}

void TestKinematics::aRackThatPicksUpSomewhereElseIsRefusedWithAReason()
{
    // A rack acting anywhere but the inboard tie rod end is a steering linkage
    // -- an idler, a drag link -- and there is no such body in this solve.
    CornerSpec corner = frontCorner();
    corner.steeringRack = QStringLiteral("{corner}_TieRod_O");

    const AxleSolver axle =
        AxleSolver::build(cornerMechanism(), corner, frontAxle(), MirrorSpec{}, true);
    QVERIFY(!axle.isSteered());
    QCOMPARE(axle.warnings().size(), 1);
    QVERIFY(axle.warnings().first().contains(QStringLiteral("F_TieRod_O")));
    QVERIFY(axle.warnings().first().contains(QStringLiteral("F_TieRod_I")));
}

void TestKinematics::aSteerSweepOfAnUnsteeredAxleComesBackEmptyAndSaysWhy()
{
    const AxleSolver axle =
        AxleSolver::build(cornerMechanism(), unsteeredCorner(), frontAxle(), MirrorSpec{}, true);
    QVERIFY(!axle.isEmpty());

    SweepSpec spec;
    spec.kind = SweepKind::Steer;
    spec.from = -20.0;
    spec.to = 20.0;
    spec.steps = 9;
    const SweepResult result = runSweep(axle, spec);

    // A line of zeroes would read as "this suspension has no bump steer", which
    // is a claim about the car. Nothing plotted, and a reason, is the truth.
    QVERIFY(result.samples.empty());
    QCOMPARE(result.warnings.size(), 1);
    QVERIFY(result.warnings.first().contains(QStringLiteral("steering")));

    // Bump still sweeps: it is the steering that is missing, not the axle.
    spec.kind = SweepKind::Bump;
    spec.from = -10.0;
    spec.to = 10.0;
    const SweepResult bump = runSweep(axle, spec);
    QCOMPARE(bump.samples.size(), std::size_t(9));
    QVERIFY(bump.warnings.isEmpty());
}

void TestKinematics::anAxleSortsItsTwoSidesByWhereTheyAre()
{
    const AxleSolver axle =
        AxleSolver::build(cornerMechanism(), frontCorner(), frontAxle(), MirrorSpec{});
    QVERIFY(!axle.isEmpty());
    QVERIFY2(axle.warnings().isEmpty(), qPrintable(axle.warnings().join(QStringLiteral("; "))));
    QVERIFY(axle.hasBothSides());
    QCOMPARE(axle.label(), QStringLiteral("Front"));

    // The template wrote one side out; which side of the car each instance
    // landed on is read off the coordinates.
    QVERIFY(axle.left()->isLeft());
    QVERIFY(!axle.right()->isLeft());
    QVERIFY(axle.left()->designPose().wheelCenter.y > 0.0);
    QVERIFY(axle.right()->designPose().wheelCenter.y < 0.0);

    // An axle the table does not hold at all is empty, and says nothing.
    CornerSpec rear;
    rear.token = QStringLiteral("R");
    rear.label = QStringLiteral("Rear");
    const AxleSolver missing =
        AxleSolver::build(cornerMechanism(), rear, frontAxle(), MirrorSpec{});
    QVERIFY(missing.isEmpty());
    QVERIFY(missing.warnings().isEmpty());
}

void TestKinematics::aBumpSweepMovesBothWheelsTheSameWay()
{
    const AxleSolver axle =
        AxleSolver::build(cornerMechanism(), frontCorner(), frontAxle(), MirrorSpec{});

    SweepSpec spec;
    spec.kind = SweepKind::Bump;
    spec.from = -30.0;
    spec.to = 30.0;
    spec.steps = 25;

    const SweepResult result = runSweep(axle, spec);
    QCOMPARE(result.samples.size(), std::size_t(25));
    QVERIFY2(result.warnings.isEmpty(), qPrintable(result.warnings.join(QStringLiteral("; "))));

    for (const AxleSample& sample : result.samples) {
        QVERIFY(sample.left.valid);
        QVERIFY(sample.right.valid);
        // Both wheels go the same way, and each gets exactly the travel asked for.
        QVERIFY(std::abs(sample.left.wheelTravel - sample.input) < 1e-6);
        QVERIFY(std::abs(sample.right.wheelTravel - sample.input) < 1e-6);
        // A mirrored axle is symmetric, so the two sides read the same.
        QVERIFY(std::abs(sample.left.camber - sample.right.camber) < 1e-6);
        QVERIFY(std::abs(sample.left.toe - sample.right.toe) < 1e-6);
        QVERIFY(std::abs(sample.left.halfTrackChange - sample.right.halfTrackChange) < 1e-6);
        // Both arms of the bar turn together over a bump, so it does nothing.
        QVERIFY(sample.hasAntiRoll);
        QVERIFY(std::abs(sample.antiRollTwist) < 1e-6);
    }

    // The damper moves less than the wheel does, which is what a rocker is for.
    // And the ratio is compression per bump, so it is positive: a damper that
    // closes a millimetre for each millimetre of wheel travel reads +1, not -1.
    const AxleSample* middle = result.nearest(0.0);
    QVERIFY(middle);
    QVERIFY(middle->leftInstallationRatio > 0.05);
    QVERIFY(middle->leftInstallationRatio < 1.0);
    QVERIFY(std::abs(middle->leftInstallationRatio - middle->rightInstallationRatio) < 1e-6);
}

void TestKinematics::aRollSweepMovesThemOppositeWaysAndTwistsTheBar()
{
    const AxleSolver axle =
        AxleSolver::build(cornerMechanism(), frontCorner(), frontAxle(), MirrorSpec{});

    SweepSpec spec;
    spec.kind = SweepKind::Roll;
    spec.from = -2.0;
    spec.to = 2.0;
    spec.steps = 21;

    const SweepResult result = runSweep(axle, spec);
    QVERIFY2(result.warnings.isEmpty(), qPrintable(result.warnings.join(QStringLiteral("; "))));
    QCOMPARE(result.samples.size(), std::size_t(21));

    for (const AxleSample& sample : result.samples) {
        QVERIFY(sample.left.valid);
        QVERIFY(sample.right.valid);
        if (std::abs(sample.input) < 1e-9) continue;
        // One wheel goes up as the other goes down. Not by the same amount:
        // what roll sets equal and opposite is how far each contact patch moves
        // on the tilted ground, and a wishbone does not answer bump and droop
        // symmetrically.
        QVERIFY(sample.left.wheelTravel * sample.right.wheelTravel < 0.0);
        QVERIFY(std::abs(sample.left.contactPatchRise + sample.right.contactPatchRise) < 1e-6);
        // And now the bar is doing something, which is the whole point of it.
        QVERIFY(std::abs(sample.antiRollTwist) > 1e-3);
    }

    // The symmetry that does hold: rolling the other way swaps the two sides.
    for (double angle : { 0.4, 1.2, 2.0 }) {
        const AxleSample* one = result.nearest(angle);
        const AxleSample* other = result.nearest(-angle);
        QVERIFY(one && other);
        QVERIFY(std::abs(one->left.wheelTravel - other->right.wheelTravel) < 1e-6);
        QVERIFY(std::abs(one->left.camber - other->right.camber) < 1e-6);
        QVERIFY(std::abs(one->antiRollTwist + other->antiRollTwist) < 1e-6);
    }

    // Twist grows with roll and reverses with it.
    const AxleSample* left = result.nearest(-2.0);
    const AxleSample* right = result.nearest(2.0);
    QVERIFY(left && right);
    QVERIFY(left->antiRollTwist * right->antiRollTwist < 0.0);
    QVERIFY(std::abs(right->antiRollTwist) > std::abs(result.nearest(0.4)->antiRollTwist));
}

void TestKinematics::theRollCentreIsOnTheCentrelineWhenTheAxleIsSymmetric()
{
    const AxleSolver axle =
        AxleSolver::build(cornerMechanism(), frontCorner(), frontAxle(), MirrorSpec{});

    SweepSpec spec;
    spec.kind = SweepKind::Bump;
    spec.from = 0.0;
    spec.to = 0.0;
    spec.steps = 2;

    const SweepResult result = runSweep(axle, spec);
    QVERIFY(!result.isEmpty());
    const AxleSample& sample = result.samples.front();
    QVERIFY(sample.rollCenterValid);
    // Both sides are mirror images, so the two construction lines cross on the
    // car's centreline.
    QVERIFY(std::abs(sample.rollCenterLateral) < 1e-6);
    // And it sits above the ground but below the wheel centre, which is where a
    // double wishbone puts it.
    QVERIFY(sample.rollCenterHeight > 0.0);
    QVERIFY(sample.rollCenterHeight < sample.left.wheelCenter.z);
}

void TestKinematics::theInstantCentreIsWhereTheArmPlanesCross()
{
    // The corner the other tests use has level pivot axes and both ball joints
    // in the transverse plane through the wheel centre, which is exactly where
    // crossing the arm planes and reading the arms off the joints agree. Tilt
    // the upper axis the way anti-dive does and move the joints a caster's
    // worth apart, and they no longer do.
    HardpointTable table = frontLeftCorner();
    for (Hardpoint& point : table.points) {
        if (point.name == QLatin1String("F_UCA_IF")) point.coord[2] = 300.0;
        if (point.name == QLatin1String("F_UCA_IR")) point.coord[2] = 260.0;
        if (point.name == QLatin1String("F_UCA_O")) point.coord[0] = -20.0;
        if (point.name == QLatin1String("F_LCA_O")) point.coord[0] = 15.0;
    }
    const CornerSolver solver = bindTo(table);
    const auto at = [&table](const char* name) {
        const Hardpoint* point = table.find(QLatin1String(name));
        return Vec3(point->coord[0], point->coord[1], point->coord[2]);
    };

    for (const double travel : { -20.0, 0.0, 20.0 }) {
        const CornerPose pose = solver.poseAtWheelTravel(travel, 0.0, nullptr);
        QVERIFY2(pose.valid, qPrintable(pose.error));
        QVERIFY(pose.instantCenterValid);
        const Vec3& centre = pose.instantCenter;

        // In the transverse plane through the wheel centre...
        QVERIFY(std::abs(centre.x - pose.wheelCenter.x) < 1e-9);
        // ...and in each arm's plane: the one through its pivot axis and its
        // ball joint where the joint is now.
        const Vec3 lower =
            cross(at("F_LCA_IR") - at("F_LCA_IF"), pose.lowerOuter - at("F_LCA_IF")).normalized();
        const Vec3 upper =
            cross(at("F_UCA_IR") - at("F_UCA_IF"), pose.upperOuter - at("F_UCA_IF")).normalized();
        QVERIFY(std::abs(dot(lower, centre - at("F_LCA_IF"))) < 1e-6);
        QVERIFY(std::abs(dot(upper, centre - at("F_UCA_IF"))) < 1e-6);
    }
}

void TestKinematics::theRollAxisRunsThroughEveryAxlesRollCentre()
{
    const AxleSolver front =
        AxleSolver::build(cornerMechanism(), frontCorner(), frontAxle(), MirrorSpec{});
    const AxleSolver rear =
        AxleSolver::build(cornerMechanism(), rearCorner(), rearAxle(), MirrorSpec{});
    QVERIFY(front.hasBothSides() && rear.hasBothSides());

    const std::optional<Vec3> frontCentre = front.designRollCentre();
    const std::optional<Vec3> rearCentre = rear.designRollCentre();
    QVERIFY(frontCentre && rearCentre);

    // As a point it is the same roll centre the sweep reports -- that height
    // over the ground the patches stand on, at the axle, on the centreline of
    // a mirrored car.
    SweepSpec design;
    design.from = 0.0;
    design.to = 0.0;
    design.steps = 2;
    const AxleSample atDesign = runSweep(front, design).samples.front();
    QVERIFY(std::abs(frontCentre->z - atDesign.rollCenterHeight) < 1e-9);
    QVERIFY(std::abs(frontCentre->y) < 1e-6);
    QVERIFY(std::abs(frontCentre->x - 0.0) < 1e-9);
    QVERIFY(std::abs(rearCentre->x + 1550.0) < 1e-9);
    // The rear's higher lower wishbone is what lifts its roll centre.
    QVERIFY(rearCentre->z > frontCentre->z + 10.0);

    const RollAxis axis = rollAxisThrough({ front, rear });
    QVERIFY(axis.valid);
    QVERIFY(std::abs(axis.direction.length() - 1.0) < 1e-12);
    // Forward, so positive roll means what it means in the sweep.
    QVERIFY(axis.direction.x > 0.0);
    const Axis line{ axis.origin, axis.direction };
    QVERIFY(line.distanceTo(*frontCentre) < 1e-9);
    QVERIFY(line.distanceTo(*rearCentre) < 1e-9);
    // Climbing towards the back, which is falling towards the front.
    QVERIFY(axis.direction.z < -1e-3);

    // With one axle there is one roll centre, and the axis runs straight along
    // the car through it.
    const RollAxis alone = rollAxisThrough({ front });
    QVERIFY(alone.valid);
    QVERIFY(distance(alone.origin, *frontCentre) < 1e-12);
    QVERIFY(distance(alone.direction, Vec3(1, 0, 0)) < 1e-12);

    QVERIFY(!rollAxisThrough({}).valid);
    // An invalid axis moves nothing.
    const Rigid still = bodyRollMotion(RollAxis{}, 3.0);
    QVERIFY(distance(still.map(Vec3(100, 200, 300)), Vec3(100, 200, 300)) < 1e-12);
}

void TestKinematics::rollingTheBodyAboutItLeavesTheTyresWhereTheyStand()
{
    const std::vector<AxleSolver> axles = {
        AxleSolver::build(cornerMechanism(), frontCorner(), frontAxle(), MirrorSpec{}),
        AxleSolver::build(cornerMechanism(), rearCorner(), rearAxle(), MirrorSpec{}),
    };
    const RollAxis axis = rollAxisThrough(axles);
    QVERIFY(axis.valid);
    // What the car would be doing if it rolled about the ground under its
    // centreline instead: the axis a roll sweep tilts the ground about.
    RollAxis atGround;
    atGround.direction = Vec3(1, 0, 0);
    atGround.valid = true;

    for (const double degrees : { -2.0, -0.5, 0.5, 2.0 }) {
        const Rigid body = bodyRollMotion(axis, degrees);
        const Rigid dragged = bodyRollMotion(atGround, degrees);

        for (const AxleSolver& axle : axles) {
            // The roll centres are on the axis, so they stay put.
            const Vec3 centre = *axle.designRollCentre();
            QVERIFY(distance(body.map(centre), centre) < 1e-9);

            const AxleSample sample = sampleAxleAt(axle, SweepKind::Roll, degrees, 0.0);
            for (const std::optional<CornerSolver>* side : { &axle.left(), &axle.right() }) {
                const CornerPose& pose = side == &axle.left() ? sample.left : sample.right;
                QVERIFY(pose.valid);
                const Vec3& design = (*side)->designPose().contactPatch;
                const Vec3 onRoad = body.map(pose.contactPatch);
                // Still on the road...
                QVERIFY(std::abs(onRoad.z - design.z) < 0.25);
                // ...and not dragged across it. What is left is second order
                // in the roll -- the roll centre moves as the car rolls -- where
                // turning about the ground drags the tyre by the roll centre's
                // height times the angle.
                const double slide = std::abs(onRoad.y - design.y);
                const double dragging = std::abs(dragged.map(pose.contactPatch).y - design.y);
                QVERIFY2(slide < 0.25 && slide < 0.2 * dragging,
                         qPrintable(QStringLiteral("%1 at %2 deg: %3 mm against %4 mm")
                                        .arg(axle.label())
                                        .arg(degrees)
                                        .arg(slide)
                                        .arg(dragging)));
            }
        }
    }
}

void TestKinematics::aRolledBodyLeansItsWheelsWithIt()
{
    const std::vector<AxleSolver> axles = {
        AxleSolver::build(cornerMechanism(), frontCorner(), frontAxle(), MirrorSpec{}),
        AxleSolver::build(cornerMechanism(), rearCorner(), rearAxle(), MirrorSpec{}),
    };
    const RollAxis axis = rollAxisThrough(axles);

    const double degrees = 2.0;
    const Rigid body = bodyRollMotion(axis, degrees);
    const AxleSample sample = sampleAxleAt(axles.front(), SweepKind::Roll, degrees, 0.0);
    const auto camberToRoad = [&body](const CornerPose& pose) {
        return -std::asin(body.rotate(pose.spinAxis).z) * 180.0 / M_PI;
    };

    // Positive roll lifts the left of the car, so the body leans right, the
    // way it does in a left-hand corner. Everything on it leans the same way:
    // the inside wheel's top goes towards the car and the outside wheel's away
    // from it, by the roll less whatever the wishbones win back -- which is
    // the body-relative camber the sweep reports.
    QVERIFY(std::abs(camberToRoad(sample.left) - (sample.left.camber - degrees)) < 0.01);
    QVERIFY(std::abs(camberToRoad(sample.right) - (sample.right.camber + degrees)) < 0.01);
    // And the wishbones do win some back, but nowhere near all of it.
    QVERIFY(sample.left.camber > 0.0);
    QVERIFY(camberToRoad(sample.left) < 0.0);
}

void TestKinematics::camberToGroundPartsFromCamberByTheRollAngle()
{
    const std::vector<AxleSolver> axles = {
        AxleSolver::build(cornerMechanism(), frontCorner(), frontAxle(), MirrorSpec{}),
        AxleSolver::build(cornerMechanism(), rearCorner(), rearAxle(), MirrorSpec{}),
    };
    const AxleSolver& axle = axles.front();

    // Over a bump and a steer the road stays level under the car, so the two
    // are one number.
    for (const SweepKind kind : { SweepKind::Bump, SweepKind::Steer }) {
        SweepSpec spec;
        spec.kind = kind;
        spec.from = -20.0;
        spec.to = 20.0;
        spec.steps = 9;
        for (const AxleSample& sample : runSweep(axle, spec).samples) {
            QVERIFY(sample.left.valid && sample.right.valid);
            QCOMPARE(sample.left.camberToGround, sample.left.camber);
            QCOMPARE(sample.right.camberToGround, sample.right.camber);
        }
    }

    // In a roll the body leans and takes its wheels with it: against the road
    // the inside wheel -- the left, in positive roll -- gains the roll angle of
    // negative camber and the outside one loses it. For a wheel pointing
    // straight ahead that is exact; what the roll steers it by is second order.
    SweepSpec roll;
    roll.kind = SweepKind::Roll;
    roll.from = -3.0;
    roll.to = 3.0;
    roll.steps = 7;
    const SweepResult result = runSweep(axle, roll);
    for (const AxleSample& sample : result.samples) {
        QVERIFY(sample.left.valid && sample.right.valid);
        QVERIFY(std::abs(sample.left.camberToGround - (sample.left.camber - sample.input)) < 1e-4);
        QVERIFY(std::abs(sample.right.camberToGround - (sample.right.camber + sample.input)) < 1e-4);
    }
    QVERIFY(std::abs(result.nearest(3.0)->left.camberToGround - result.nearest(3.0)->left.camber)
            > 2.5);

    // And it is the camber a picture of the car rolled on a level road shows --
    // the body turned about its roll axis, which is nearly but not quite the
    // ground line the sweep tilts.
    const Rigid body = bodyRollMotion(rollAxisThrough(axles), 2.0);
    const AxleSample* two = result.nearest(2.0);
    QVERIFY(two);
    const double onRoad = -std::asin(body.rotate(two->left.spinAxis).z) * 180.0 / M_PI;
    QVERIFY(std::abs(two->left.camberToGround - onRoad) < 0.01);

    // The single-position readout agrees with the curve.
    const AxleSample single = sampleAxleAt(axle, SweepKind::Roll, 2.0, 0.0);
    QVERIFY(std::abs(single.left.camberToGround - two->left.camberToGround) < 1e-6);

    double value = 0.0;
    QVERIFY(sweepMeasureValue(*two, SweepMeasure::CamberToGround, false, &value));
    QCOMPARE(value, two->right.camberToGround);
}

void TestKinematics::aSteerSweepChangesToeAndNotRideHeight()
{
    const AxleSolver axle =
        AxleSolver::build(cornerMechanism(), frontCorner(), frontAxle(), MirrorSpec{});

    SweepSpec spec;
    spec.kind = SweepKind::Steer;
    spec.from = -15.0;
    spec.to = 15.0;
    spec.steps = 13;

    const SweepResult result = runSweep(axle, spec);
    QVERIFY2(result.warnings.isEmpty(), qPrintable(result.warnings.join(QStringLiteral("; "))));

    for (const AxleSample& sample : result.samples) {
        QVERIFY(sample.left.valid);
        QVERIFY(std::abs(sample.left.wheelTravel) < 1e-6);
        QVERIFY(std::abs(sample.right.wheelTravel) < 1e-6);
    }

    // The rack moves both rods the same way in space, so one wheel toes in as
    // the other toes out -- the steer, before any Ackermann is read off it.
    const AxleSample* locked = result.nearest(15.0);
    QVERIFY(locked);
    QVERIFY(std::abs(locked->left.toeChange) > 0.5);
    QVERIFY(locked->left.toeChange * locked->right.toeChange < 0.0);
}

void TestKinematics::theCsvHasOneRowPerStepAndSaysNothingAboutWhatDidNotSolve()
{
    const AxleSolver axle =
        AxleSolver::build(cornerMechanism(), frontCorner(), frontAxle(), MirrorSpec{});

    SweepSpec spec;
    spec.kind = SweepKind::Bump;
    spec.from = -10.0;
    spec.to = 10.0;
    spec.steps = 5;

    const QByteArray csv = sweepToCsv(runSweep(axle, spec));
    const QList<QByteArray> lines = csv.split('\n');
    // Header, five rows, and the trailing newline's empty tail.
    QCOMPARE(lines.size(), 7);
    QVERIFY(lines.first().startsWith("Wheel travel [mm]"));
    QVERIFY(lines.first().contains("camber_left [deg]"));
    QVERIFY(lines.first().contains("camber_to_ground_left [deg]"));
    QVERIFY(lines.first().contains("installation_ratio_right [mm/mm]"));

    const int columns = lines.first().count(',') + 1;
    for (int row = 1; row <= 5; ++row) QCOMPARE(lines[row].count(',') + 1, columns);

    // A position that did not solve leaves its fields empty rather than writing
    // a zero somebody would later plot as a real measurement.
    SweepSpec impossible = spec;
    impossible.from = -400.0;
    impossible.to = 400.0;
    impossible.steps = 3;
    const SweepResult refused = runSweep(axle, impossible);
    QVERIFY(!refused.warnings.isEmpty());
    const QList<QByteArray> refusedLines = sweepToCsv(refused).split('\n');
    QVERIFY(refusedLines[1].contains(",,,,"));
}

void TestKinematics::theShippedTemplateSolvesTheCornerItDescribes()
{
    // The one test that ties the JSON the application ships to the solver that
    // reads it. The mechanism block names {corner}_LCA_IF and the rest; this
    // table holds exactly those names for corner F, on both sides.
    const LinkageTemplate templ = builtinLinkageTemplate();
    QVERIFY(templ.canSimulate());
    QVERIFY(!templ.corners.empty());
    QCOMPARE(templ.corners.front().token, QStringLiteral("F"));

    const AxleSolver axle =
        AxleSolver::build(templ.mechanism, templ.corners.front(), frontAxle(), MirrorSpec{});
    QVERIFY2(axle.warnings().isEmpty(), qPrintable(axle.warnings().join(QStringLiteral("; "))));
    QVERIFY(axle.hasBothSides());
    QCOMPARE(axle.label(), QStringLiteral("Front"));

    // The pushrod is on the upper wishbone, which is what this car does and what
    // the template's own note says it does.
    QCOMPARE(templ.mechanism.pushrodMount, PushrodMount::UpperArm);

    SweepSpec spec;
    spec.from = -30.0;
    spec.to = 30.0;
    spec.steps = 31;
    const SweepResult result = runSweep(axle, spec);
    QVERIFY2(result.warnings.isEmpty(), qPrintable(result.warnings.join(QStringLiteral("; "))));

    const AxleSample* bump = result.nearest(30.0);
    const AxleSample* design = result.nearest(0.0);
    const AxleSample* droop = result.nearest(-30.0);
    QVERIFY(bump && design && droop);

    // Numbers a suspension engineer would recognise rather than merely finite
    // ones: this corner gains negative camber into bump, the damper moves less
    // than the wheel, and the roll centre stays low and near the centreline.
    QVERIFY(bump->left.camberChange < -0.3);
    QVERIFY(droop->left.camberChange > 0.3);
    QVERIFY(std::abs(design->left.camber) < 1e-9);
    QVERIFY(design->leftInstallationRatio > 0.1);
    QVERIFY(design->leftInstallationRatio < 1.0);
    QVERIFY(design->rollCenterValid);
    QVERIFY(std::abs(design->rollCenterLateral) < 1e-6);
    QVERIFY(design->rollCenterHeight > 0.0);
    QVERIFY(design->rollCenterHeight < 200.0);

    // And the pushrod really is carried by the upper arm: its outer end keeps
    // its distance to both of that wishbone's chassis pivots, which it could
    // only do by turning with it.
    const HardpointTable table = frontAxle();
    const auto anchor = [&table](const char* name) {
        const Hardpoint* point = table.find(QLatin1String(name));
        return Vec3(point->coord[0], point->coord[1], point->coord[2]);
    };
    const Vec3 frontPivot = anchor("F_UCA_IF");
    const Vec3 rearPivot = anchor("F_UCA_IR");
    const double toFront = distance(anchor("F_PushRod_O"), frontPivot);
    const double toRear = distance(anchor("F_PushRod_O"), rearPivot);
    for (const AxleSample* sample : { bump, design, droop }) {
        const Vec3* outer = sample->left.find(QStringLiteral("F_PushRod_O"));
        QVERIFY(outer);
        QVERIFY(std::abs(distance(*outer, frontPivot) - toFront) < 1e-6);
        QVERIFY(std::abs(distance(*outer, rearPivot) - toRear) < 1e-6);
    }
}

// ---------------------------------------------------------------------------
// Ackermann
// ---------------------------------------------------------------------------

void TestKinematics::ackermannIsMeasuredAgainstTheIdealOuterAngle()
{
    const double wheelbase = 1530.0;
    const double track = 1200.0;
    const double inner = 20.0;
    // The outer angle that puts both wheels' axles through one point on the
    // rear axle's line, worked out the long way round: how far inboard of the
    // inner wheel that point is, then the angle the outer wheel needs to face it.
    const double turnRadius = wheelbase / std::tan(inner * M_PI / 180.0);
    const double ideal = std::atan(wheelbase / (turnRadius + track)) * 180.0 / M_PI;

    double percent = 0.0;
    QVERIFY(ackermannPercent(inner, ideal, wheelbase, track, &percent));
    QVERIFY(std::abs(percent - 100.0) < 1e-9);

    // Parallel steer is none at all, an outer wheel turned further than the
    // inner is anti-Ackermann, one turned less than ideal is more than 100, and
    // half way between parallel and ideal is half.
    QVERIFY(ackermannPercent(inner, inner, wheelbase, track, &percent));
    QVERIFY(std::abs(percent) < 1e-9);
    QVERIFY(ackermannPercent(inner, inner + 1.0, wheelbase, track, &percent));
    QVERIFY(percent < 0.0);
    QVERIFY(ackermannPercent(inner, ideal - 1.0, wheelbase, track, &percent));
    QVERIFY(percent > 100.0);
    QVERIFY(ackermannPercent(inner, 0.5 * (inner + ideal), wheelbase, track, &percent));
    QVERIFY(std::abs(percent - 50.0) < 1e-9);

    // Straight ahead there is nothing to take a ratio of, and without a
    // wheelbase or a track nothing to take it against.
    QVERIFY(!ackermannPercent(0.0, 0.0, wheelbase, track, &percent));
    QVERIFY(!ackermannPercent(0.01, 0.01, wheelbase, track, &percent));
    QVERIFY(!ackermannPercent(inner, ideal, 0.0, track, &percent));
    QVERIFY(!ackermannPercent(inner, ideal, wheelbase, 0.0, &percent));
}

void TestKinematics::theWheelbaseIsTakenToTheFarAxle()
{
    const HardpointTable car = withRearAxle(frontAxle(), 1530.0);
    std::vector<AxleSolver> axles;
    axles.push_back(AxleSolver::build(cornerMechanism(), frontCorner(), car, MirrorSpec{}, true));
    axles.push_back(AxleSolver::build(cornerMechanism(), rearCorner(), car, MirrorSpec{}, true));
    QVERIFY(axles[0].hasBothSides());
    QVERIFY(axles[1].hasBothSides());
    QVERIFY(axles[0].isSteered());
    QVERIFY(!axles[1].isSteered());

    // Nothing is assumed: an axle knows its wheelbase only once told.
    QCOMPARE(axles[0].wheelbase(), 0.0);
    assignWheelbases(axles);
    QVERIFY(std::abs(axles[0].wheelbase() - 1530.0) < 1e-9);
    QVERIFY(std::abs(axles[1].wheelbase() - 1530.0) < 1e-9);

    // An axle with nothing else in the table has nothing to measure to. Its
    // steer sweep still runs; it just does not claim a percentage.
    std::vector<AxleSolver> alone;
    alone.push_back(
        AxleSolver::build(cornerMechanism(), frontCorner(), frontAxle(), MirrorSpec{}, true));
    assignWheelbases(alone);
    QCOMPARE(alone.front().wheelbase(), 0.0);

    SweepSpec spec;
    spec.kind = SweepKind::Steer;
    spec.from = -15.0;
    spec.to = 15.0;
    spec.steps = 7;
    const SweepResult result = runSweep(alone.front(), spec);
    QCOMPARE(result.samples.size(), std::size_t(7));
    for (const AxleSample& sample : result.samples) {
        QVERIFY(sample.left.valid);
        QVERIFY(!sample.ackermannValid);
        double value = 0.0;
        QVERIFY(!sweepMeasureValue(sample, SweepMeasure::Ackermann, true, &value));
    }
}

void TestKinematics::aSteerSweepReadsAckermannTheSameTurningEitherWay()
{
    AxleSolver axle =
        AxleSolver::build(cornerMechanism(), frontCorner(), frontAxle(), MirrorSpec{}, true);
    axle.setWheelbase(1530.0);

    SweepSpec spec;
    spec.kind = SweepKind::Steer;
    spec.from = -20.0;
    spec.to = 20.0;
    spec.steps = 21;
    const SweepResult result = runSweep(axle, spec);
    QVERIFY2(result.warnings.isEmpty(), qPrintable(result.warnings.join(QStringLiteral("; "))));

    for (const AxleSample& sample : result.samples) {
        // Straight ahead is the one place there is no Ackermann to speak of.
        if (std::abs(sample.input) < 1e-9) {
            QVERIFY(!sample.ackermannValid);
            continue;
        }
        QVERIFY(sample.ackermannValid);
        QVERIFY(std::isfinite(sample.ackermann));
        double value = 0.0;
        QVERIFY(sweepMeasureValue(sample, SweepMeasure::Ackermann, true, &value));
        QCOMPARE(value, sample.ackermann);
    }

    // The axle is a mirror image, so turning left is turning right: which wheel
    // is inner swaps, and the percentage does not notice.
    for (double rack : { 4.0, 10.0, 20.0 }) {
        const AxleSample* one = result.nearest(rack);
        const AxleSample* other = result.nearest(-rack);
        QVERIFY(one && other);
        QVERIFY(std::abs(one->ackermann - other->ackermann) < 1e-6);
    }

    // The readout is solved on its own rather than continued from a neighbour,
    // and still agrees with the curve.
    const AxleSample single = sampleAxleAt(axle, SweepKind::Steer, 10.0, 0.0);
    QVERIFY(single.ackermannValid);
    QVERIFY(std::abs(single.ackermann - result.nearest(10.0)->ackermann) < 1e-6);

    // The CSV carries it, once, at the end.
    const QList<QByteArray> lines = sweepToCsv(result).split('\n');
    QVERIFY(lines.first().endsWith(",ackermann [%]"));
    QVERIFY(lines[1 + 10].endsWith(",")); // straight ahead: an empty field, not a zero

    // A bump sweep turns no wheel with the rack, so it has no Ackermann to
    // give -- not even with the rack held over to one side.
    spec.kind = SweepKind::Bump;
    spec.from = -10.0;
    spec.to = 10.0;
    spec.steps = 5;
    spec.rackTravel = 10.0;
    for (const AxleSample& sample : runSweep(axle, spec).samples) {
        QVERIFY(sample.left.valid);
        QVERIFY(!sample.ackermannValid);
    }
}

void TestKinematics::ackermannIsReadOffTheWheelsThemselves()
{
    // Worked out again from nothing but where the two wheels' axles point in
    // plan view, so that neither the sign toe is read with on each side nor
    // which wheel counts as inner is taken on trust from the code under test.
    const auto turnedLeft = [](const CornerPose& pose, const CornerPose& design) {
        return (std::atan2(pose.spinAxis.y, pose.spinAxis.x)
                - std::atan2(design.spinAxis.y, design.spinAxis.x))
               * 180.0 / M_PI;
    };

    const double wheelbase = 1530.0;
    const double track = 1200.0; // the fixture's contact patches are at y = +-600
    AxleSolver axle = AxleSolver::build(cornerMechanism(), frontCorner(),
                                        frontAxleWithOuterTieRodAt(520.0), MirrorSpec{}, true);
    axle.setWheelbase(wheelbase);

    for (double rack : { -16.0, -6.0, 6.0, 16.0 }) {
        const AxleSample sample = sampleAxleAt(axle, SweepKind::Steer, rack, 0.0);
        QVERIFY(sample.left.valid && sample.right.valid);
        const double left = turnedLeft(sample.left, axle.left()->designPose());
        const double right = turnedLeft(sample.right, axle.right()->designPose());
        QVERIFY(left * right > 0.0); // both wheels steer the same way

        // The inner wheel is on the side the car turns towards.
        const double inner = std::abs(left > 0.0 ? left : right);
        const double outer = std::abs(left > 0.0 ? right : left);
        const double turnRadius = wheelbase / std::tan(inner * M_PI / 180.0);
        const double ideal = std::atan(wheelbase / (turnRadius + track)) * 180.0 / M_PI;

        QVERIFY(sample.ackermannValid);
        QVERIFY(std::abs(sample.ackermann - 100.0 * (inner - outer) / (inner - ideal)) < 1e-6);
    }
}

void TestKinematics::angledSteeringArmsAddAckermann()
{
    // The fixture's steering arms point straight back, which is parallel steer
    // near enough. Swinging the outer tie rod ends inboard -- towards the
    // trapezoid every steering textbook draws -- is what adds Ackermann, and
    // the further the more.
    const auto ackermannAt = [](const HardpointTable& table) {
        AxleSolver axle =
            AxleSolver::build(cornerMechanism(), frontCorner(), table, MirrorSpec{}, true);
        axle.setWheelbase(1530.0);
        const AxleSample sample = sampleAxleAt(axle, SweepKind::Steer, 10.0, 0.0);
        return sample.ackermannValid ? sample.ackermann : std::nan("");
    };

    const double straight = ackermannAt(frontAxle());
    const double angled = ackermannAt(frontAxleWithOuterTieRodAt(530.0));
    const double more = ackermannAt(frontAxleWithOuterTieRodAt(500.0));
    QVERIFY(std::isfinite(straight) && std::isfinite(angled) && std::isfinite(more));

    QVERIFY(std::abs(straight) < 10.0);
    QVERIFY(angled > straight + 10.0);
    QVERIFY(more > angled + 10.0);

    // And the other way is anti-Ackermann: arms swung outboard turn the outer
    // wheel further than the inner one.
    const double outboard = ackermannAt(frontAxleWithOuterTieRodAt(580.0));
    QVERIFY(outboard < -10.0);
}

void TestKinematics::aTravelAndAnIncrementBecomeARangeAndAStepCount()
{
    SweepSettings settings;
    settings.bumpTravel = 30.0;
    settings.reboundTravel = 20.0;
    settings.bumpIncrement = 1.0;

    const SweepSpec spec = settings.specFor(SweepKind::Bump);
    QCOMPARE(spec.kind, SweepKind::Bump);
    QCOMPARE(spec.from, -20.0);
    QCOMPARE(spec.to, 30.0);
    // Fifty millimetres at one millimetre a step is fifty steps, which is
    // fifty-one positions: both ends are solved.
    QCOMPARE(spec.steps, 51);
    QCOMPARE(spec.inputAt(0), -20.0);
    QCOMPARE(spec.inputAt(spec.steps - 1), 30.0);
    QVERIFY(std::abs(spec.inputAt(20) - 0.0) < 1e-12);
}

void TestKinematics::bumpAndReboundAreNotAssumedToBeEqual()
{
    // A rebound written with the minus sign already in it still means downward.
    SweepSettings settings;
    settings.bumpTravel = 33.0;
    settings.reboundTravel = -17.0;
    settings.bumpIncrement = 1.0;

    const SweepSpec spec = settings.specFor(SweepKind::Bump);
    QCOMPARE(spec.from, -17.0);
    QCOMPARE(spec.to, 33.0);
    QCOMPARE(spec.steps, 51);
}

void TestKinematics::eachKindKeepsItsOwnTravelInItsOwnUnit()
{
    // The whole reason the settings hold all three at once: switching from a
    // bump sweep to a roll sweep used to carry twenty-five millimetres of wheel
    // travel across as twenty-five degrees of body roll, which is not a corner
    // any car takes.
    SweepSettings settings;
    settings.bumpTravel = 25.0;
    settings.reboundTravel = 25.0;
    settings.bumpIncrement = 1.0;
    settings.rollAngle = 1.2;
    settings.rollIncrement = 0.25;
    settings.steerTravel = 35.0;
    settings.steerIncrement = 2.0;

    const SweepSpec bump = settings.specFor(SweepKind::Bump);
    const SweepSpec roll = settings.specFor(SweepKind::Roll);
    const SweepSpec steer = settings.specFor(SweepKind::Steer);

    QCOMPARE(bump.to, 25.0);
    QCOMPARE(roll.to, 1.2);
    QCOMPARE(roll.from, -1.2);
    QCOMPARE(steer.to, 35.0);

    // And each is solved as finely as it was asked to be, not as finely as its
    // neighbour.
    QCOMPARE(bump.steps, 51);
    QCOMPARE(roll.steps, 11);
    QCOMPARE(steer.steps, 36);

    QCOMPARE(sweepInputUnit(SweepKind::Bump), QStringLiteral("mm"));
    QCOMPARE(sweepInputUnit(SweepKind::Roll), QStringLiteral("deg"));
}

void TestKinematics::anIncrementThatWouldNeverEndIsBoundedNotObeyed()
{
    SweepSettings settings;
    settings.rollAngle = 3.0;

    // A field cleared to nothing is a range with no step in it. It comes back
    // as the two ends rather than as an unbounded solve.
    settings.rollIncrement = 0.0;
    QCOMPARE(settings.specFor(SweepKind::Roll).steps, 2);

    // And a step small enough to ask for millions of positions stops at what a
    // plot can hold.
    settings.rollIncrement = 1e-6;
    QCOMPARE(settings.specFor(SweepKind::Roll).steps, kMaxSweepSteps);

    // A travel of nothing is a sweep that stands still, not one that divides by
    // zero.
    settings.rollAngle = 0.0;
    settings.rollIncrement = 0.25;
    const SweepSpec still = settings.specFor(SweepKind::Roll);
    QCOMPARE(still.steps, 2);
    QCOMPARE(still.inputAt(0), 0.0);
    QCOMPARE(still.inputAt(1), 0.0);
}

void TestKinematics::aCurveIsNotStretchedPastWhatItsUnitCanSay()
{
    // A tenth of a millimetre and a hundredth of a degree: a roll centre that
    // wanders a nanometre off the centreline is drawn as the straight line it
    // is, while bump steer of a few hundredths of a degree still fills a plot.
    QCOMPARE(sweepMeasureResolution(SweepMeasure::RollCentreLateral), 0.1);
    QCOMPARE(sweepMeasureResolution(SweepMeasure::Toe), 0.01);
    QCOMPARE(sweepMeasureResolution(SweepMeasure::InstallationRatio), 0.001);
    for (const SweepMeasure measure : sweepMeasures())
        QVERIFY(sweepMeasureResolution(measure) > 0.0);

    // And the readout never says "-0.000".
    QCOMPARE(sweepValueText(-3e-9), QStringLiteral("0.000"));
    QCOMPARE(sweepValueText(-0.0004), QStringLiteral("0.000"));
    QCOMPARE(sweepValueText(-0.0006), QStringLiteral("-0.001"));
    QCOMPARE(sweepValueText(10.0), QStringLiteral("10.000"));
}

void TestKinematics::whichSidesAPlotDrawsRoundTrips()
{
    for (const SweepSides sides : { SweepSides::Both, SweepSides::Left, SweepSides::Right })
        QCOMPARE(sweepSidesFromString(sweepSidesToString(sides)), sides);
    // Something a later release might write reads as the default, not as one
    // wheel quietly disappearing from every plot.
    QCOMPARE(sweepSidesFromString(QStringLiteral("front")), SweepSides::Both);
    QCOMPARE(sweepSidesFromString(QString()), SweepSides::Both);

    // Camber to ground is a curve per wheel, keyed like every other.
    QVERIFY(sweepMeasureIsPerSide(SweepMeasure::CamberToGround));
    QCOMPARE(sweepMeasureFromKey(sweepMeasureKey(SweepMeasure::CamberToGround)),
             SweepMeasure::CamberToGround);
}

QTEST_MAIN(TestKinematics)
#include "test_kinematics.moc"
