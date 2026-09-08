#include "model/Wheels.h"

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

/// A car's worth of wheel centres: front axle at x = 800, rear at x = -750,
/// left at +y, in the ISO 8855 frame the whole tool works in.
HardpointTable sample()
{
    HardpointTable table;
    table.points.push_back(make("F_LCA_O", 800.0, 520.0, 130.0));
    table.points.push_back(make("F_WheelCenter", 800.0, 600.0, 230.0));
    table.points.push_back(make("F_WheelCenter_R", 800.0, -600.0, 230.0));
    table.points.push_back(make("R_WheelCenter", -750.0, 600.0, 230.0));
    table.points.push_back(make("R_WheelCenter_R", -750.0, -600.0, 230.0));
    return table;
}

/// A wheel modelled well away from the origin, so "put its centre on the
/// hardpoint" is distinguishable from "put its origin there".
Aabb offCentreModel()
{
    Aabb box;
    box.expand(QVector3D(90.0f, 10.0f, -100.0f));
    box.expand(QVector3D(110.0f, 30.0f, 100.0f)); // centre (100, 20, 0)
    return box;
}

WheelSpec fullSpec()
{
    WheelSpec spec;
    spec.setPoint(WheelCorner::FrontLeft, QStringLiteral("F_WheelCenter"));
    spec.setPoint(WheelCorner::FrontRight, QStringLiteral("F_WheelCenter_R"));
    spec.setPoint(WheelCorner::RearLeft, QStringLiteral("R_WheelCenter"));
    spec.setPoint(WheelCorner::RearRight, QStringLiteral("R_WheelCenter_R"));
    return spec;
}

} // namespace

class TestWheels : public QObject {
    Q_OBJECT

private slots:
    void guessesTheCornersFromWhereThePointsAre();
    void guessesOneAxleAsTheFront();
    void guessesNothingFromATableWithoutWheelCentres();
    void resolvesEveryNamedCorner();
    void skipsACornerThatIsNotInTheTable();
    void mirrorsTheSideTheModelIsNotDrawnFor();
    void mirrorsNothingForASymmetricModel();
    void putsTheModelCentreOnTheHardpoint();
    void placesByTheModelsOwnOriginWhenAsked();
    void mirrorsAboutThePlaneThroughTheAnchor();
    void boundsCoverEveryPlacedCopy();
    void specRoundTripsThroughItsStrings();
};

void TestWheels::guessesTheCornersFromWhereThePointsAre()
{
    const WheelSpec spec = guessWheelSpec(sample());
    // Nothing here reads the names apart from finding the candidates: +x is
    // forward and +y is left, so the four sort themselves.
    QCOMPARE(spec.point(WheelCorner::FrontLeft), QStringLiteral("F_WheelCenter"));
    QCOMPARE(spec.point(WheelCorner::FrontRight), QStringLiteral("F_WheelCenter_R"));
    QCOMPARE(spec.point(WheelCorner::RearLeft), QStringLiteral("R_WheelCenter"));
    QCOMPARE(spec.point(WheelCorner::RearRight), QStringLiteral("R_WheelCenter_R"));
    QCOMPARE(spec.namedCount(), 4);
}

void TestWheels::guessesOneAxleAsTheFront()
{
    HardpointTable table;
    table.points.push_back(make("Radmitte_L", -750.0, 600.0, 230.0));
    table.points.push_back(make("Radmitte_R", -750.0, -600.0, 230.0));

    // Both are at the same station, so there is nothing to tell front from rear.
    // They go to the front row, where the user moves them if that is wrong.
    const WheelSpec spec = guessWheelSpec(table);
    QCOMPARE(spec.point(WheelCorner::FrontLeft), QStringLiteral("Radmitte_L"));
    QCOMPARE(spec.point(WheelCorner::FrontRight), QStringLiteral("Radmitte_R"));
    QCOMPARE(spec.namedCount(), 2);
}

void TestWheels::guessesNothingFromATableWithoutWheelCentres()
{
    HardpointTable table;
    table.points.push_back(make("F_LCA_O", 800.0, 520.0, 130.0));
    QVERIFY(guessWheelSpec(table).isEmpty());
}

void TestWheels::resolvesEveryNamedCorner()
{
    QStringList warnings;
    const std::vector<WheelPlacement> placements = resolveWheels(fullSpec(), sample(), &warnings);

    QCOMPARE(placements.size(), std::size_t(4));
    QVERIFY(warnings.isEmpty());
    QCOMPARE(placements[0].corner, WheelCorner::FrontLeft);
    QCOMPARE(placements[0].center, QVector3D(800.0f, 600.0f, 230.0f));
    QCOMPARE(placements[3].corner, WheelCorner::RearRight);
    QCOMPARE(placements[3].center, QVector3D(-750.0f, -600.0f, 230.0f));
}

void TestWheels::skipsACornerThatIsNotInTheTable()
{
    WheelSpec spec = fullSpec();
    spec.setPoint(WheelCorner::RearRight, QStringLiteral("R_WheelCenter_Right"));

    QStringList warnings;
    const std::vector<WheelPlacement> placements = resolveWheels(spec, sample(), &warnings);

    // A workbook may hold one axle, or may not have been mirrored yet: the rest
    // still draws, and the miss is reported rather than being an error.
    QCOMPARE(placements.size(), std::size_t(3));
    QCOMPARE(warnings.size(), 1);
    QVERIFY(warnings.first().contains(QStringLiteral("R_WheelCenter_Right")));
}

void TestWheels::mirrorsTheSideTheModelIsNotDrawnFor()
{
    WheelSpec spec = fullSpec();
    spec.modelSide = WheelModelSide::Left;
    for (const WheelPlacement& placement : resolveWheels(spec, sample()))
        QCOMPARE(placement.mirrored, !isLeftWheel(placement.corner));

    spec.modelSide = WheelModelSide::Right;
    for (const WheelPlacement& placement : resolveWheels(spec, sample()))
        QCOMPARE(placement.mirrored, isLeftWheel(placement.corner));
}

void TestWheels::mirrorsNothingForASymmetricModel()
{
    WheelSpec spec = fullSpec();
    spec.modelSide = WheelModelSide::Symmetric;
    for (const WheelPlacement& placement : resolveWheels(spec, sample()))
        QVERIFY(!placement.mirrored);
}

void TestWheels::putsTheModelCentreOnTheHardpoint()
{
    WheelPlacement placement;
    placement.center = QVector3D(800.0f, 600.0f, 230.0f);

    const Aabb model = offCentreModel();
    const QMatrix4x4 transform = wheelTransform(placement, model, true);
    QCOMPARE(transform.map(model.center()), placement.center);

    // And the whole body comes with it: the box keeps its size, moved.
    const Aabb placed = transformedBounds(model, transform);
    QCOMPARE(placed.center(), placement.center);
    QCOMPARE(placed.extent(), model.extent());
}

void TestWheels::placesByTheModelsOwnOriginWhenAsked()
{
    WheelPlacement placement;
    placement.center = QVector3D(800.0f, 600.0f, 230.0f);

    // A model already positioned in vehicle coordinates is placed by its origin,
    // so its offset from the hardpoint is the one the CAD file gave it.
    const QMatrix4x4 transform = wheelTransform(placement, offCentreModel(), false);
    QCOMPARE(transform.map(QVector3D(0.0f, 0.0f, 0.0f)), placement.center);
    QCOMPARE(transform.map(QVector3D(10.0f, 0.0f, 0.0f)),
             placement.center + QVector3D(10.0f, 0.0f, 0.0f));
}

void TestWheels::mirrorsAboutThePlaneThroughTheAnchor()
{
    WheelPlacement placement;
    placement.center = QVector3D(800.0f, -600.0f, 230.0f);
    placement.mirrored = true;

    const Aabb model = offCentreModel();
    const QMatrix4x4 transform = wheelTransform(placement, model, true);

    // The anchor still lands on the hardpoint, ...
    QCOMPARE(transform.map(model.center()), placement.center);
    // ... y is the axis that flips, and only y ...
    QCOMPARE(transform.map(model.center() + QVector3D(5.0f, 7.0f, 9.0f)),
             placement.center + QVector3D(5.0f, -7.0f, 9.0f));
    // ... and a mirrored body occupies a box of the same size.
    QCOMPARE(transformedBounds(model, transform).extent(), model.extent());
}

void TestWheels::boundsCoverEveryPlacedCopy()
{
    const std::vector<WheelPlacement> placements = resolveWheels(fullSpec(), sample());
    Aabb model;
    model.expand(QVector3D(-20.0f, -100.0f, -300.0f));
    model.expand(QVector3D(20.0f, 100.0f, 300.0f));

    const Aabb bounds = wheelBounds(placements, model, true);
    // Front axle at x = 800 and rear at x = -750, plus half the model's length.
    QCOMPARE(bounds.min, QVector3D(-770.0f, -700.0f, -70.0f));
    QCOMPARE(bounds.max, QVector3D(820.0f, 700.0f, 530.0f));

    QVERIFY(wheelBounds({}, model, true).isEmpty());
    QVERIFY(wheelBounds(placements, Aabb{}, true).isEmpty());
}

void TestWheels::specRoundTripsThroughItsStrings()
{
    // The project file stores these, so a spec written by one release has to
    // read back the same in the next.
    for (const WheelCorner corner : kWheelCorners)
        QCOMPARE(wheelCornerFromString(wheelCornerToString(corner)), corner);
    for (const WheelModelSide side :
         { WheelModelSide::Left, WheelModelSide::Right, WheelModelSide::Symmetric })
        QCOMPARE(wheelModelSideFromString(wheelModelSideToString(side)), side);

    QCOMPARE(wheelCornerFromString(QStringLiteral("nonsense"), WheelCorner::RearRight),
             WheelCorner::RearRight);
    QCOMPARE(wheelModelSideFromString(QString(), WheelModelSide::Symmetric),
             WheelModelSide::Symmetric);
}

QTEST_APPLESS_MAIN(TestWheels)
#include "test_wheels.moc"
