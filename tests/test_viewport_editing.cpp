#include "render/ViewportWidget.h"

#include "model/HardpointConfig.h"
#include "render/MoveGizmo.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QSignalSpy>
#include <QTest>

#include <cmath>

using namespace suspkin;

namespace {

/// Two points of a front upper wishbone, far from the origin the way a real
/// workbook's are.
HardpointTable twoPoints()
{
    HardpointTable table;
    Hardpoint inner;
    inner.name = QStringLiteral("F_UCA_IF");
    inner.coord[0] = -2068.622;
    inner.coord[1] = 271.5;
    inner.coord[2] = 318.0;
    Hardpoint outer;
    outer.name = QStringLiteral("F_UCA_O");
    outer.coord[0] = -2000.0;
    outer.coord[1] = 560.0;
    outer.coord[2] = 330.0;
    table.points = { inner, outer };
    return table;
}

/// A viewport with those points in it, framed and with the first one selected.
///
/// Never shown: nothing here paints, and the events go in by hand, so no
/// graphics context is ever needed. The offscreen platform says "QOpenGLWidget
/// is not supported" when one is built on it; that is the warning, not a
/// failure, and nothing in these cases asks it to draw.
struct SelectedPoint {
    ViewportWidget viewport;

    SelectedPoint()
    {
        viewport.resize(1000, 700);
        viewport.setHardpoints(twoPoints());
        viewport.fitToView();
        viewport.setSelectedHardpoint(0);
    }

    MoveGizmo::Layout gizmo() const
    {
        return MoveGizmo::layoutAt(cameraOf(), viewport.size(), QVector3D(-2068.622f, 271.5f, 318.0f));
    }

    Camera cameraOf() const
    {
        Camera camera;
        camera.setState(viewport.cameraState());
        return camera;
    }

    /// Somewhere along @p axis's arm, clear of the hub.
    QPoint onArm(int axis, qreal pixels) const
    {
        const MoveGizmo::Layout layout = gizmo();
        QPointF along = layout.arms[std::size_t(axis)].tip - layout.origin;
        along /= std::hypot(along.x(), along.y());
        return (layout.origin + along * pixels).toPoint();
    }

    void press(const QPoint& at)
    {
        QMouseEvent event(QEvent::MouseButtonPress, QPointF(at), viewport.mapToGlobal(QPointF(at)),
                          Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(&viewport, &event);
    }

    void moveTo(const QPoint& at)
    {
        QMouseEvent event(QEvent::MouseMove, QPointF(at), viewport.mapToGlobal(QPointF(at)),
                          Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(&viewport, &event);
    }

    void release(const QPoint& at)
    {
        QMouseEvent event(QEvent::MouseButtonRelease, QPointF(at),
                          viewport.mapToGlobal(QPointF(at)), Qt::LeftButton, Qt::NoButton,
                          Qt::NoModifier);
        QCoreApplication::sendEvent(&viewport, &event);
    }

    void pressKey(Qt::Key key)
    {
        QKeyEvent event(QEvent::KeyPress, key, Qt::NoModifier);
        QCoreApplication::sendEvent(&viewport, &event);
    }
};

/// A front left upper wishbone: two pivots on the frame and the ball joint
/// they carry between them.
HardpointTable wishbone()
{
    HardpointTable table;
    Hardpoint front;
    front.name = QStringLiteral("F_UCA_IF");
    front.coord[0] = -2068.622;
    front.coord[1] = 271.5;
    front.coord[2] = 318.0;
    Hardpoint rear;
    rear.name = QStringLiteral("F_UCA_IR");
    rear.coord[0] = -2268.622;
    rear.coord[1] = 271.5;
    rear.coord[2] = 318.0;
    Hardpoint outer;
    outer.name = QStringLiteral("F_UCA_O");
    outer.coord[0] = -2160.0;
    outer.coord[1] = 560.0;
    outer.coord[2] = 330.0;
    table.points = { front, rear, outer };
    return table;
}

/// The A-arm as a template draws it: one closed chain through all three, which
/// is the topology whatever the viewport does with it. Written literally, so
/// there is no corner to substitute and no far side to mirror.
Linkage wishboneLinkage(const HardpointTable& table)
{
    ChainTemplate chain;
    chain.points = QStringList{ QStringLiteral("F_UCA_IF"), QStringLiteral("F_UCA_O"),
                                QStringLiteral("F_UCA_IR") };
    chain.closed = true;

    PartTemplate part;
    part.id = QStringLiteral("upperWishbone");
    part.label = QStringLiteral("Upper wishbone");
    part.kind = PartKind::Wishbone;
    part.perCorner = false;
    part.chains.push_back(chain);

    LinkageTemplate templ;
    templ.parts.push_back(part);
    return buildLinkage(templ, table, MirrorSpec{});
}

/// What the two pivots are: bolted to the car, which is what the template's
/// mechanism block says about them and what the table shows.
HardpointConfigMap wishboneConfig()
{
    HardpointConfig pivot;
    pivot.type = PointType::ToBody;
    HardpointConfig ballJoint;
    ballJoint.type = PointType::Solved;

    HardpointConfigMap config;
    config.insert(QStringLiteral("F_UCA_IF"), pivot);
    config.insert(QStringLiteral("F_UCA_IR"), pivot);
    config.insert(QStringLiteral("F_UCA_O"), ballJoint);
    return config;
}

} // namespace

class TestViewportEditing : public QObject {
    Q_OBJECT

private slots:
    void draggingAnArmReportsHowFarItWent();
    void aDragThatWentNowhereIsNotAnEdit();
    void escapePutsTheMarkerBack();
    void draggingAnArmDoesNotOrbitOrSelect();
    void anAxisKeyAsksForTheCoordinate();
    void aPosedMechanismHasNoArrowsAndNoKeys();
    void noMemberIsDrawnBetweenTwoChassisPoints();
    void chassisFlagsGoWithTheTableTheyWereMeasuredOn();
};

void TestViewportEditing::draggingAnArmReportsHowFarItWent()
{
    SelectedPoint scene;
    QSignalSpy moved(&scene.viewport, &ViewportWidget::hardpointMoved);
    QSignalSpy dragging(&scene.viewport, &ViewportWidget::hardpointDragging);

    const QPoint from = scene.onArm(1, 40.0);
    const QPoint to = scene.onArm(1, 90.0);
    const double expected = MoveGizmo::dragDistance(scene.gizmo(), 1, QPointF(from), QPointF(to));

    scene.press(from);
    scene.moveTo(to);
    scene.release(to);

    QCOMPARE(moved.size(), 1);
    QCOMPARE(moved.front().at(0).toInt(), 0); // the selected point
    QCOMPARE(moved.front().at(1).toInt(), 1); // Y
    QVERIFY(std::abs(moved.front().at(2).toDouble() - expected) < 1e-9);
    QVERIFY(expected != 0.0);
    // And it said where it was on the way, so the coordinate can be read while
    // it moves.
    QVERIFY(!dragging.isEmpty());
}

void TestViewportEditing::aDragThatWentNowhereIsNotAnEdit()
{
    SelectedPoint scene;
    QSignalSpy moved(&scene.viewport, &ViewportWidget::hardpointMoved);

    const QPoint at = scene.onArm(2, 45.0);
    scene.press(at);
    scene.release(at);

    // Taking hold of an arrow and letting go again must not mark the project
    // dirty, and must not write a coordinate nobody changed.
    QCOMPARE(moved.size(), 0);
}

void TestViewportEditing::escapePutsTheMarkerBack()
{
    SelectedPoint scene;
    QSignalSpy moved(&scene.viewport, &ViewportWidget::hardpointMoved);

    scene.press(scene.onArm(0, 40.0));
    scene.moveTo(scene.onArm(0, 120.0));
    scene.pressKey(Qt::Key_Escape);
    scene.release(scene.onArm(0, 120.0));

    QCOMPARE(moved.size(), 0);
}

void TestViewportEditing::draggingAnArmDoesNotOrbitOrSelect()
{
    SelectedPoint scene;
    const CameraState before = scene.viewport.cameraState();
    QSignalSpy selection(&scene.viewport, &ViewportWidget::hardpointSelectionEdited);

    scene.press(scene.onArm(1, 40.0));
    scene.moveTo(scene.onArm(1, 140.0));
    scene.release(scene.onArm(1, 140.0));

    const CameraState after = scene.viewport.cameraState();
    QCOMPARE(after.azimuthDeg, before.azimuthDeg);
    QCOMPARE(after.elevationDeg, before.elevationDeg);
    QCOMPARE(selection.size(), 0);
    QCOMPARE(scene.viewport.selectedHardpoint(), 0);
}

void TestViewportEditing::anAxisKeyAsksForTheCoordinate()
{
    SelectedPoint scene;
    QSignalSpy asked(&scene.viewport, &ViewportWidget::coordinateEntryRequested);

    scene.pressKey(Qt::Key_X);
    scene.pressKey(Qt::Key_Z);

    QCOMPARE(asked.size(), 2);
    QCOMPARE(asked.at(0).at(0).toInt(), 0);
    QCOMPARE(asked.at(0).at(1).toInt(), 0);
    QCOMPARE(asked.at(1).at(1).toInt(), 2);
}

void TestViewportEditing::aPosedMechanismHasNoArrowsAndNoKeys()
{
    SelectedPoint scene;
    const QPoint onArm = scene.onArm(1, 45.0);
    scene.viewport.setPointEditingEnabled(false);

    QSignalSpy moved(&scene.viewport, &ViewportWidget::hardpointMoved);
    QSignalSpy asked(&scene.viewport, &ViewportWidget::coordinateEntryRequested);

    scene.press(onArm);
    scene.moveTo(scene.onArm(1, 100.0));
    scene.release(scene.onArm(1, 100.0));
    scene.pressKey(Qt::Key_Y);

    QCOMPARE(moved.size(), 0);
    QCOMPARE(asked.size(), 0);
}

void TestViewportEditing::noMemberIsDrawnBetweenTwoChassisPoints()
{
    ViewportWidget viewport;
    viewport.resize(1000, 700);
    const HardpointTable table = wishbone();
    viewport.setHardpoints(table);
    viewport.setLinkage(wishboneLinkage(table));
    viewport.fitToView();

    // The A closes, because that is what the template says it is.
    QCOMPARE(viewport.drawnSegmentCount(), 3);

    viewport.setGroundedPoints(groundedPoints(table, wishboneConfig()));

    // The two legs are left; the edge between the pivots, which is a line
    // across the frame rather than a member, is not drawn.
    QCOMPARE(viewport.drawnSegmentCount(), 2);

    // Nothing was taken away from the points themselves: all three are still
    // on screen to be labelled and picked, and the part is still there.
    QVERIFY(viewport.hasLinkage());
    for (int index = 0; index < 3; ++index) {
        QPointF screen;
        QVERIFY(viewport.markerPosition(index, &screen));
    }
    viewport.setSelectedHardpoint(0);
    QCOMPARE(viewport.selectedHardpoint(), 0);
}

void TestViewportEditing::chassisFlagsGoWithTheTableTheyWereMeasuredOn()
{
    ViewportWidget viewport;
    viewport.resize(1000, 700);
    const HardpointTable table = wishbone();
    viewport.setHardpoints(table);
    viewport.setLinkage(wishboneLinkage(table));
    viewport.setGroundedPoints(groundedPoints(table, wishboneConfig()));
    QCOMPARE(viewport.drawnSegmentCount(), 2);

    // A flag list as long as somebody else's table is not applied to this one.
    viewport.setGroundedPoints(std::vector<bool>{ true, true });
    QCOMPARE(viewport.drawnSegmentCount(), 2);

    // And replacing the points drops them, exactly as it drops the parts: both
    // are answers about rows that have just gone.
    viewport.setHardpoints(table);
    viewport.setLinkage(wishboneLinkage(table));
    QCOMPARE(viewport.drawnSegmentCount(), 3);
}

QTEST_MAIN(TestViewportEditing)
#include "test_viewport_editing.moc"
