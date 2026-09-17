#include "model/HardpointMirror.h"
#include "model/Simulation.h"

#include <QTest>

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
/// millimetres. The same plausible-rather-than-tidy car test_kinematics solves,
/// cut down to what a corner needs to bind: both wishbones, the tie rod and a
/// wheel centre.
HardpointTable frontLeftCorner()
{
    HardpointTable table;
    table.points = {
        make("F_LCA_IF", 100.0, 200.0, 120.0),  make("F_LCA_IR", -100.0, 200.0, 120.0),
        make("F_LCA_O", 0.0, 560.0, 110.0),     make("F_UCA_IF", 80.0, 230.0, 280.0),
        make("F_UCA_IR", -80.0, 230.0, 280.0),  make("F_UCA_O", 0.0, 540.0, 300.0),
        make("F_TieRod_I", -120.0, 220.0, 150.0), make("F_TieRod_O", -120.0, 555.0, 145.0),
        make("F_WheelCenter", 0.0, 600.0, 220.0), make("F_ContactPatch", 0.0, 600.0, 0.0),
    };
    return table;
}

/// The same corner a wheelbase behind, renamed: all an axle needs of another
/// axle is that it is there and where.
HardpointTable withRearAxle(const HardpointTable& front)
{
    HardpointTable table = front;
    for (const Hardpoint& point : front.points) {
        Hardpoint rear = point;
        rear.name = QStringLiteral("R") + point.name.mid(1);
        if (!rear.mirrorOf.isEmpty()) rear.mirrorOf = QStringLiteral("R") + rear.mirrorOf.mid(1);
        rear.coord[0] -= 1550.0;
        table.points.push_back(rear);
    }
    return table;
}

/// Both sides of both axles, mirrored with the tool's own rule -- which is how
/// a real project gets its other half.
HardpointTable wholeCar()
{
    return withRearAxle(mirrorHardpoints(frontLeftCorner(), {}, MirrorSpec{}).table);
}

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
    mechanism.wheelCenter = QStringLiteral("{corner}_WheelCenter");
    mechanism.contactPatch = QStringLiteral("{corner}_ContactPatch");
    return mechanism;
}

/// A front axle with a rack and a rear one without, both of them saying so --
/// so the compatibility rule that leaves an unstated template fully steered is
/// not what is being measured here.
LinkageTemplate twoAxleTemplate()
{
    LinkageTemplate templ;
    templ.mechanism = cornerMechanism();

    CornerSpec front;
    front.token = QStringLiteral("F");
    front.label = QStringLiteral("Front");
    front.steeringRack = QStringLiteral("{corner}_TieRod_I");
    front.steeringStated = true;

    CornerSpec rear;
    rear.token = QStringLiteral("R");
    rear.label = QStringLiteral("Rear");
    rear.steeringStated = true;

    templ.corners = { front, rear };
    return templ;
}

Simulation buildFor(const HardpointTable& table, const LinkageTemplate& templ,
                    const QHash<QString, StaticAlignment>& alignment = {},
                    const QString& steeringNote = QString())
{
    return Simulation::build(templ, table, MirrorSpec{}, alignment, steeringNote);
}

Simulation wholeCarSimulation() { return buildFor(wholeCar(), twoAxleTemplate()); }

} // namespace

/// What the window used to do inside itself, now that it can be asked without
/// one. Every case here ran only against a live QMainWindow before.
class TestSimulation : public QObject {
    Q_OBJECT

private slots:
    // --- binding ---------------------------------------------------------
    void everyCornerTheTemplateNamesBecomesAnAxle();
    void aCornerTheTableHasNoPointsForIsSkippedSilently();
    void anAxleThatHasGoneFallsBackToTheFirstRatherThanToNothing();
    void statedStaticAnglesOutrankWhatTheTableWouldHaveSaid();

    // --- what it says when it cannot solve --------------------------------
    void anEmptyTableAsksForHardpoints();
    void aTableWithNoCompleteAxleSaysWhatTheSolverNeeds();
    void anAxleLessTableReplacesTheNoteAboutTheMechanismRatherThanAddingToIt();
    void aNoteAboutSteeringIsCarriedBesideTheOneAboutTheMechanism();

    // --- posing ------------------------------------------------------------
    void onlyTheSelectedAxleMovesUnlessTheOthersAreAskedFor();
    void steeringNeverDragsTheOtherAxlesAlong();
    void aBodyCannotRollWithAnAxleLeftBehind();
    void aPoseIsLaidOverTheTableWithoutChangingIt();
    void aRolledBodyMovesTheChassisPickupsTooNotOnlyWhatTheSolveMoved();
    void wheelRotationsAreKeyedByTheNameOfTheirWheelCentre();
};

// ---------------------------------------------------------------------------
// Binding
// ---------------------------------------------------------------------------

void TestSimulation::everyCornerTheTemplateNamesBecomesAnAxle()
{
    const Simulation simulation = wholeCarSimulation();
    QCOMPARE(simulation.axles().size(), std::size_t(2));
    QCOMPARE(simulation.axles()[0].cornerToken(), QStringLiteral("F"));
    QCOMPARE(simulation.axles()[1].cornerToken(), QStringLiteral("R"));
    QVERIFY(simulation.note().isEmpty());
    // The rack is the front axle's, and saying so is what keeps a steer sweep
    // off the rear one.
    QVERIFY(simulation.axles()[0].isSteered());
    QVERIFY(!simulation.axles()[1].isSteered());
}

void TestSimulation::aCornerTheTableHasNoPointsForIsSkippedSilently()
{
    // A workbook with the front axle done and the rear not started yet. That is
    // an ordinary half-finished project, not something to warn about.
    const Simulation simulation =
        buildFor(mirrorHardpoints(frontLeftCorner(), {}, MirrorSpec{}).table, twoAxleTemplate());
    QCOMPARE(simulation.axles().size(), std::size_t(1));
    QCOMPARE(simulation.axles().front().cornerToken(), QStringLiteral("F"));
    QVERIFY(simulation.note().isEmpty());
}

void TestSimulation::anAxleThatHasGoneFallsBackToTheFirstRatherThanToNothing()
{
    const Simulation simulation = wholeCarSimulation();
    QCOMPARE(simulation.axleFor(QStringLiteral("R")), &simulation.axles()[1]);
    // A panel still showing an axle the table no longer holds has to be given
    // something to draw.
    QCOMPARE(simulation.axleFor(QStringLiteral("nonexistent")), &simulation.axles()[0]);
    QCOMPARE(simulation.axleFor(QString()), &simulation.axles()[0]);

    const Simulation none = buildFor(HardpointTable{}, twoAxleTemplate());
    QCOMPARE(none.axleFor(QStringLiteral("F")), nullptr);
}

void TestSimulation::statedStaticAnglesOutrankWhatTheTableWouldHaveSaid()
{
    // The patch sits directly under the wheel centre, so the table on its own
    // says zero camber.
    const Simulation plain = wholeCarSimulation();
    QCOMPARE(plain.axleFor(QStringLiteral("F"))->left()->wheelAttitude(),
             WheelAttitude::ContactPatch);
    QVERIFY(qAbs(plain.axleFor(QStringLiteral("F"))->left()->designPose().camber) < 1e-9);

    QHash<QString, StaticAlignment> alignment;
    alignment.insert(QStringLiteral("F"), StaticAlignment{ -1.5, 0.25 });
    const Simulation stated = buildFor(wholeCar(), twoAxleTemplate(), alignment);

    const CornerPose& front = stated.axleFor(QStringLiteral("F"))->left()->designPose();
    QCOMPARE(stated.axleFor(QStringLiteral("F"))->left()->wheelAttitude(), WheelAttitude::Stated);
    QVERIFY(qAbs(front.camber - -1.5) < 1e-9);
    QVERIFY(qAbs(front.toe - 0.25) < 1e-9);
    // Keyed by corner, so the axle that was not named keeps reading its own
    // hardpoints.
    QCOMPARE(stated.axleFor(QStringLiteral("R"))->left()->wheelAttitude(),
             WheelAttitude::ContactPatch);
}

// ---------------------------------------------------------------------------
// What it says when it cannot solve
// ---------------------------------------------------------------------------

void TestSimulation::anEmptyTableAsksForHardpoints()
{
    const Simulation simulation = buildFor(HardpointTable{}, twoAxleTemplate());
    QVERIFY(simulation.isEmpty());
    QVERIFY(simulation.note().contains(QStringLiteral("Import hardpoints")));
}

void TestSimulation::aTableWithNoCompleteAxleSaysWhatTheSolverNeeds()
{
    HardpointTable table;
    table.points = { make("F_WheelCenter", 0.0, 600.0, 220.0) };
    const Simulation simulation = buildFor(table, twoAxleTemplate());
    QVERIFY(simulation.isEmpty());
    QVERIFY(simulation.note().contains(QStringLiteral("complete mechanism")));
}

void TestSimulation::anAxleLessTableReplacesTheNoteAboutTheMechanismRatherThanAddingToIt()
{
    LinkageTemplate templ = twoAxleTemplate();
    templ.mechanismAssumed = true;

    // With something to solve, the assumption is worth reporting.
    const Simulation solved = buildFor(wholeCar(), templ);
    QVERIFY(solved.note().contains(QStringLiteral("built-in mechanism")));

    // With nothing to solve it is not: what the user needs is the reason there
    // is no curve, and a guess about roles is not it.
    const Simulation unsolved = buildFor(HardpointTable{}, templ);
    QVERIFY(!unsolved.note().contains(QStringLiteral("built-in mechanism")));
    QVERIFY(unsolved.note().contains(QStringLiteral("Import hardpoints")));
}

void TestSimulation::aNoteAboutSteeringIsCarriedBesideTheOneAboutTheMechanism()
{
    LinkageTemplate templ = twoAxleTemplate();
    templ.mechanismAssumed = true;
    const Simulation both = buildFor(wholeCar(), templ, {}, QStringLiteral("the rack was adopted"));
    QCOMPARE(both.note().count(QLatin1Char('\n')), 1);
    QVERIFY(both.note().endsWith(QStringLiteral("the rack was adopted")));

    const Simulation alone =
        buildFor(wholeCar(), twoAxleTemplate(), {}, QStringLiteral("the rack was adopted"));
    QCOMPARE(alone.note(), QStringLiteral("the rack was adopted"));
}

// ---------------------------------------------------------------------------
// Posing
// ---------------------------------------------------------------------------

void TestSimulation::onlyTheSelectedAxleMovesUnlessTheOthersAreAskedFor()
{
    const Simulation simulation = wholeCarSimulation();

    const SimulationPose alone =
        simulation.poseAt(QStringLiteral("F"), SweepKind::Bump, 20.0, 0.0, false);
    QCOMPARE(alone.samples.size(), std::size_t(1));
    QVERIFY(alone.samples.front().left.valid);

    const SimulationPose together =
        simulation.poseAt(QStringLiteral("F"), SweepKind::Bump, 20.0, 0.0, true);
    QCOMPARE(together.samples.size(), std::size_t(2));
    // The one that was asked for comes first: it is the one the readout shows.
    QVERIFY(qAbs(together.samples.front().left.wheelTravel - 20.0) < 1e-6);
}

void TestSimulation::steeringNeverDragsTheOtherAxlesAlong()
{
    const Simulation simulation = wholeCarSimulation();
    // Asked for every axle, and still only the steered one moves: a rack
    // belongs to one axle, and pushing a rear toe link with it would be
    // inventing a rear-steer this car has not got.
    const SimulationPose pose =
        simulation.poseAt(QStringLiteral("F"), SweepKind::Steer, 0.0, 5.0, true);
    QCOMPARE(pose.samples.size(), std::size_t(1));
    QVERIFY(!pose.bodyMotion.has_value());
}

void TestSimulation::aBodyCannotRollWithAnAxleLeftBehind()
{
    const Simulation simulation = wholeCarSimulation();

    const SimulationPose partial =
        simulation.poseAt(QStringLiteral("F"), SweepKind::Roll, 2.0, 0.0, false);
    QCOMPARE(partial.samples.size(), std::size_t(1));
    // Drawing the body rolled with one axle still level would put that axle's
    // wheels through the road.
    QVERIFY(!partial.bodyMotion.has_value());

    const SimulationPose whole =
        simulation.poseAt(QStringLiteral("F"), SweepKind::Roll, 2.0, 0.0, true);
    QCOMPARE(whole.samples.size(), std::size_t(2));
    QVERIFY(whole.bodyMotion.has_value());
}

void TestSimulation::aPoseIsLaidOverTheTableWithoutChangingIt()
{
    const Simulation simulation = wholeCarSimulation();
    const HardpointTable design = wholeCar();
    const SimulationPose pose =
        simulation.poseAt(QStringLiteral("F"), SweepKind::Bump, 20.0, 0.0, false);

    const HardpointTable posed = pose.layOver(design);
    QCOMPARE(posed.points.size(), design.points.size());

    const Hardpoint* movedCentre = posed.find(QStringLiteral("F_WheelCenter"));
    const Hardpoint* designCentre = design.find(QStringLiteral("F_WheelCenter"));
    QVERIFY(qAbs(movedCentre->coord[2] - (designCentre->coord[2] + 20.0)) < 1e-6);

    // The design table it was laid over is untouched -- which is what keeps a
    // simulation from ever reaching the edits file or a workbook.
    QCOMPARE(designCentre->coord[2], 220.0);

    // A chassis pickup is on the body, and in bump the body has not moved.
    QCOMPARE(posed.find(QStringLiteral("F_LCA_IF"))->coord[2],
             design.find(QStringLiteral("F_LCA_IF"))->coord[2]);

    // And the axle nobody posed is exactly where the table has it.
    QCOMPARE(posed.find(QStringLiteral("R_WheelCenter"))->coord[2],
             design.find(QStringLiteral("R_WheelCenter"))->coord[2]);
}

void TestSimulation::aRolledBodyMovesTheChassisPickupsTooNotOnlyWhatTheSolveMoved()
{
    const Simulation simulation = wholeCarSimulation();
    const HardpointTable design = wholeCar();
    const SimulationPose pose =
        simulation.poseAt(QStringLiteral("F"), SweepKind::Roll, 2.0, 0.0, true);
    QVERIFY(pose.bodyMotion.has_value());

    const HardpointTable posed = pose.layOver(design);
    // The chassis pickups are on the body too, and a wishbone is drawn between
    // one of them and a ball joint the solve did move.
    const Hardpoint* pickup = posed.find(QStringLiteral("F_LCA_IF"));
    const Hardpoint* designPickup = design.find(QStringLiteral("F_LCA_IF"));
    QVERIFY(qAbs(pickup->coord[2] - designPickup->coord[2]) > 1e-6);
}

void TestSimulation::wheelRotationsAreKeyedByTheNameOfTheirWheelCentre()
{
    const Simulation simulation = wholeCarSimulation();

    // Nothing being simulated leaves every wheel model at the attitude its CAD
    // file drew it in.
    QVERIFY(SimulationPose{}.wheelRotations().isEmpty());

    const SimulationPose pose =
        simulation.poseAt(QStringLiteral("F"), SweepKind::Bump, 20.0, 0.0, false);
    const WheelRotations rotations = pose.wheelRotations();
    QCOMPARE(rotations.size(), 2);
    QVERIFY(rotations.contains(QStringLiteral("F_WheelCenter")));
    QVERIFY(rotations.contains(QStringLiteral("F_WheelCenter_M")));
    // The upright turned, so the wheel bolted to it is not left pointing the
    // way its CAD file drew it.
    QVERIFY(!rotations.value(QStringLiteral("F_WheelCenter")).isIdentity());
}

QTEST_MAIN(TestSimulation)
#include "test_simulation.moc"
