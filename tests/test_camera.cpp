#include "render/Camera.h"

#include <QTest>

#include <cmath>

using namespace suspkin;

namespace {

Aabb boxAround(const QVector3D& center, float halfSize)
{
    Aabb b;
    b.expand(center - QVector3D(halfSize, halfSize, halfSize));
    b.expand(center + QVector3D(halfSize, halfSize, halfSize));
    return b;
}

} // namespace

class TestCamera : public QObject {
    Q_OBJECT

private slots:
    void presetsFaceTheExpectedAxis_data();
    void presetsFaceTheExpectedAxis();

    void topViewPutsVehicleForwardUpTheScreen();
    void fitFramesTheBoxAtAnyScale_data();
    void fitFramesTheBoxAtAnyScale();
    void narrowWindowPullsCameraBack();
    void zoomIsClampedToSceneScale();
};

void TestCamera::presetsFaceTheExpectedAxis_data()
{
    QTest::addColumn<int>("preset");
    QTest::addColumn<QVector3D>("expectedDirection");

    // Direction from pivot towards the eye, in ISO 8855: X forward, Y left, Z up.
    QTest::newRow("front") << int(ViewPreset::Front) << QVector3D(1, 0, 0);
    QTest::newRow("rear")  << int(ViewPreset::Rear)  << QVector3D(-1, 0, 0);
    QTest::newRow("left")  << int(ViewPreset::Left)  << QVector3D(0, 1, 0);
    QTest::newRow("right") << int(ViewPreset::Right) << QVector3D(0, -1, 0);
}

void TestCamera::presetsFaceTheExpectedAxis()
{
    QFETCH(int, preset);
    QFETCH(QVector3D, expectedDirection);

    Camera camera;
    camera.fitTo(boxAround(QVector3D(0, 0, 0), 10.0f), 1.5f);
    camera.applyPreset(static_cast<ViewPreset>(preset));

    const QVector3D actual = camera.eye().normalized();
    QVERIFY2((actual - expectedDirection).length() < 1e-4f,
             qPrintable(QStringLiteral("eye direction was (%1, %2, %3)")
                            .arg(actual.x()).arg(actual.y()).arg(actual.z())));
}

void TestCamera::topViewPutsVehicleForwardUpTheScreen()
{
    Camera camera;
    camera.fitTo(boxAround(QVector3D(0, 0, 0), 10.0f), 1.5f);
    camera.applyPreset(ViewPreset::Top);

    // Looking down, a plan view should read with the nose up the screen and the
    // vehicle's right-hand side on the right.
    QVERIFY(camera.eye().z() > 0.0f);
    QVERIFY2(camera.up().x() > 0.9f, "vehicle forward (+X) is not up the screen");
    QVERIFY2(camera.right().y() < -0.9f, "vehicle right (-Y) is not on screen right");
}

void TestCamera::fitFramesTheBoxAtAnyScale_data()
{
    QTest::addColumn<float>("halfSize");
    QTest::newRow("10 mm bracket") << 5.0f;
    QTest::newRow("wheel")         << 330.0f;
    QTest::newRow("chassis")       << 1500.0f;
}

void TestCamera::fitFramesTheBoxAtAnyScale()
{
    QFETCH(float, halfSize);

    // Offset from the origin, as a part positioned in vehicle coordinates is.
    const QVector3D center(1200.0f, -350.0f, 240.0f);
    const Aabb bounds = boxAround(center, halfSize);
    constexpr float aspect = 16.0f / 9.0f;

    Camera camera;
    camera.fitTo(bounds, aspect);
    camera.applyPreset(ViewPreset::Isometric);

    const float radius = 0.5f * bounds.diagonal();
    const float distance = (camera.eye() - center).length();
    QVERIFY2(distance > radius, "camera ended up inside the model");

    // Every corner must land inside the frustum, and the depth range must stay
    // tight enough to be useful.
    const QMatrix4x4 mvp = camera.projectionMatrix(aspect) * camera.viewMatrix();
    for (int i = 0; i < 8; ++i) {
        const QVector3D corner(
            (i & 1) ? bounds.max.x() : bounds.min.x(),
            (i & 2) ? bounds.max.y() : bounds.min.y(),
            (i & 4) ? bounds.max.z() : bounds.min.z());
        const QVector4D clip = mvp * QVector4D(corner, 1.0f);
        QVERIFY2(clip.w() > 0.0f, "corner fell behind the camera");
        const QVector3D ndc = clip.toVector3D() / clip.w();
        QVERIFY2(std::abs(ndc.x()) <= 1.0f && std::abs(ndc.y()) <= 1.0f
                     && std::abs(ndc.z()) <= 1.0f,
                 qPrintable(QStringLiteral("corner %1 outside the frustum: ndc (%2, %3, %4)")
                                .arg(i).arg(ndc.x()).arg(ndc.y()).arg(ndc.z())));
    }
}

void TestCamera::narrowWindowPullsCameraBack()
{
    const Aabb bounds = boxAround(QVector3D(0, 0, 0), 50.0f);

    Camera wide;
    wide.fitTo(bounds, 2.0f);
    Camera narrow;
    narrow.fitTo(bounds, 0.4f);

    // A tall, narrow window is limited by horizontal FOV, so it must back off further.
    QVERIFY(narrow.eye().length() > wide.eye().length());
}

void TestCamera::zoomIsClampedToSceneScale()
{
    Camera camera;
    camera.fitTo(boxAround(QVector3D(0, 0, 0), 10.0f), 1.5f);
    camera.applyPreset(ViewPreset::Front);

    for (int i = 0; i < 500; ++i) camera.zoom(10.0f);   // zoom in hard
    QVERIFY(camera.eye().length() > 0.0f);
    QVERIFY(std::isfinite(camera.eye().length()));

    for (int i = 0; i < 500; ++i) camera.zoom(-10.0f);  // and back out
    QVERIFY(std::isfinite(camera.eye().length()));

    const QMatrix4x4 proj = camera.projectionMatrix(1.5f);
    QVERIFY(std::isfinite(proj(0, 0)) && std::isfinite(proj(2, 2)));
}

QTEST_APPLESS_MAIN(TestCamera)
#include "test_camera.moc"
