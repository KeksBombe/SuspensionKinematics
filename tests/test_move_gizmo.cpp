#include "render/MoveGizmo.h"

#include "geom/Aabb.h"

#include <QTest>

#include <cmath>

using namespace suspkin;

namespace {

const QSize kViewport(1200, 800);
/// Somewhere a hardpoint really is, so the arithmetic is exercised at the
/// distances a car is drawn at rather than around the origin.
const QVector3D kPoint(-2068.622f, 271.5f, 118.0f);

/// A camera framing the point, looking at the car from where @p preset says.
Camera cameraOn(const QVector3D& point, ViewPreset preset)
{
    Aabb bounds;
    bounds.expand(point - QVector3D(600, 600, 600));
    bounds.expand(point + QVector3D(600, 600, 600));

    Camera camera;
    camera.fitTo(bounds, float(kViewport.width()) / float(kViewport.height()));
    camera.applyPreset(preset);
    return camera;
}

qreal armLengthPx(const MoveGizmo::Layout& layout, int axis)
{
    const QPointF along = layout.arms[std::size_t(axis)].tip - layout.origin;
    return std::hypot(along.x(), along.y());
}

QPointF armDirection(const MoveGizmo::Layout& layout, int axis)
{
    const QPointF along = layout.arms[std::size_t(axis)].tip - layout.origin;
    return along / std::hypot(along.x(), along.y());
}

/// Where @p point lands on screen through @p camera.
QPointF projected(const Camera& camera, const QVector3D& point)
{
    QPointF screen;
    float depth = 0.0f;
    const float aspect = float(kViewport.width()) / float(kViewport.height());
    [[maybe_unused]] const bool ok =
        Camera::projectTo(camera.viewProjectionMatrix(aspect), point, kViewport, &screen, &depth);
    return screen;
}

} // namespace

class TestMoveGizmo : public QObject {
    Q_OBJECT

private slots:
    void anArmAcrossTheViewIsTheLengthItIsDrawnAt();
    void anArmPointingAtTheEyeCannotBeDragged();
    void theGizmoIsTheSameSizeWhateverTheZoom();
    void aDraggedPointFollowsThePointer();
    void aDragAcrossAnArmMovesNothingAlongIt();
    void thePointerPicksTheArmItIsOn();
    void theHubBelongsToTheMarkerUnderneathIt();
};

void TestMoveGizmo::anArmAcrossTheViewIsTheLengthItIsDrawnAt()
{
    // Looking along -X at the front of the car: Y and Z lie across the view, so
    // both are drawn at their full length.
    const Camera camera = cameraOn(kPoint, ViewPreset::Front);
    const MoveGizmo::Layout layout = MoveGizmo::layoutAt(camera, kViewport, kPoint);

    QVERIFY(layout.visible);
    QVERIFY(std::abs(armLengthPx(layout, 1) - MoveGizmo::kArmLengthPx) < 0.01);
    QVERIFY(std::abs(armLengthPx(layout, 2) - MoveGizmo::kArmLengthPx) < 0.01);
    QVERIFY(layout.arms[1].draggable);
    QVERIFY(layout.arms[2].draggable);
}

void TestMoveGizmo::anArmPointingAtTheEyeCannotBeDragged()
{
    const Camera camera = cameraOn(kPoint, ViewPreset::Front);
    const MoveGizmo::Layout layout = MoveGizmo::layoutAt(camera, kViewport, kPoint);

    // X runs straight at the eye from here: a pixel along it would be worth an
    // unbounded number of millimetres, so it is not a handle.
    QVERIFY(!layout.arms[0].draggable);
    QCOMPARE(MoveGizmo::dragDistance(layout, 0, QPointF(0, 0), QPointF(60, 0)), 0.0);
    QCOMPARE(MoveGizmo::axisAt(layout, layout.arms[0].tip), -1);
}

void TestMoveGizmo::theGizmoIsTheSameSizeWhateverTheZoom()
{
    // The arms are a screen length, so a wheel bearing and a whole chassis get
    // the same handle. What changes is what a pixel of it is worth.
    Camera near = cameraOn(kPoint, ViewPreset::Front);
    Camera far = near;
    CameraState state = far.state();
    state.distance *= 4.0f;
    far.setState(state);

    const MoveGizmo::Layout nearLayout = MoveGizmo::layoutAt(near, kViewport, kPoint);
    const MoveGizmo::Layout farLayout = MoveGizmo::layoutAt(far, kViewport, kPoint);

    QVERIFY(std::abs(armLengthPx(nearLayout, 1) - armLengthPx(farLayout, 1)) < 0.01);
    // Four times as far away, so the same arrow spans four times as much car.
    QVERIFY(std::abs(farLayout.arms[1].worldLength / nearLayout.arms[1].worldLength - 4.0) < 0.05);
}

void TestMoveGizmo::aDraggedPointFollowsThePointer()
{
    // The promise a drag makes: the place on the arm that was taken hold of is
    // the place that stays under the pointer. An oblique view is where a flat
    // pixels-to-millimetres rate would let it slide out.
    const Camera camera = cameraOn(kPoint, ViewPreset::Isometric);
    const MoveGizmo::Layout layout = MoveGizmo::layoutAt(camera, kViewport, kPoint);

    for (int axis = 0; axis < MoveGizmo::kAxisCount; ++axis) {
        QVERIFY(layout.arms[std::size_t(axis)].draggable);

        const QVector3D direction = layout.arms[std::size_t(axis)].direction;
        const QPointF press = layout.origin + armDirection(layout, axis) * 30.0;
        const QPointF release = press + armDirection(layout, axis) * 40.0;

        const double distance = MoveGizmo::dragDistance(layout, axis, press, release);
        QVERIFY(distance != 0.0);

        const QVector3D grabbed =
            kPoint + direction * float(MoveGizmo::axisParameterAt(layout, axis, press));
        const QPointF landed = projected(camera, grabbed + direction * float(distance));
        QVERIFY(std::hypot(landed.x() - release.x(), landed.y() - release.y()) < 0.5);

        // And the point travels along that axis alone: the other two
        // coordinates are the ones the table already had.
        const QVector3D moved = kPoint + direction * float(distance);
        for (int other = 0; other < 3; ++other)
            if (other != axis) QCOMPARE(moved[other], kPoint[other]);
    }
}

void TestMoveGizmo::aDragAcrossAnArmMovesNothingAlongIt()
{
    const Camera camera = cameraOn(kPoint, ViewPreset::Isometric);
    const MoveGizmo::Layout layout = MoveGizmo::layoutAt(camera, kViewport, kPoint);

    const QPointF direction = armDirection(layout, 1);
    const QPointF across(-direction.y(), direction.x());
    const double distance =
        MoveGizmo::dragDistance(layout, 1, layout.origin, layout.origin + across * 50.0);
    QVERIFY(std::abs(distance) < 1e-9);
}

void TestMoveGizmo::thePointerPicksTheArmItIsOn()
{
    const Camera camera = cameraOn(kPoint, ViewPreset::Isometric);
    const MoveGizmo::Layout layout = MoveGizmo::layoutAt(camera, kViewport, kPoint);

    for (int axis = 0; axis < MoveGizmo::kAxisCount; ++axis) {
        const QPointF halfway = layout.origin + armDirection(layout, axis) * 50.0;
        QCOMPARE(MoveGizmo::axisAt(layout, halfway), axis);
    }

    // Well away from all three, there is nothing to take hold of.
    QCOMPARE(MoveGizmo::axisAt(layout, layout.origin + QPointF(0.0, 400.0)), -1);
}

void TestMoveGizmo::theHubBelongsToTheMarkerUnderneathIt()
{
    // All three arms start at the point, so without this the marker could never
    // be clicked once it was selected.
    const Camera camera = cameraOn(kPoint, ViewPreset::Isometric);
    const MoveGizmo::Layout layout = MoveGizmo::layoutAt(camera, kViewport, kPoint);

    QCOMPARE(MoveGizmo::axisAt(layout, layout.origin), -1);
    QCOMPARE(MoveGizmo::axisAt(layout, layout.origin + QPointF(3.0, 2.0)), -1);
}

QTEST_APPLESS_MAIN(TestMoveGizmo)
#include "test_move_gizmo.moc"
