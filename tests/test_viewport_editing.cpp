#include "render/ViewportWidget.h"

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

QTEST_MAIN(TestViewportEditing)
#include "test_viewport_editing.moc"
