#include "geom/MeshQuery.h"

#include <QRandomGenerator>
#include <QTest>

#include <cmath>
#include <limits>

using namespace suspkin;

namespace {

/// A closed box, twelve triangles.
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
    return mesh;
}

/// A wavy sheet of @p n by @p n quads: enough triangles that the tree is
/// several levels deep, and curved so no two leaves are alike.
TriMesh sheet(int n)
{
    TriMesh mesh;
    for (int j = 0; j <= n; ++j) {
        for (int i = 0; i <= n; ++i) {
            const float x = float(i) * 10.0f;
            const float y = float(j) * 10.0f;
            mesh.positions.push_back(QVector3D(x, y, 20.0f * std::sin(x * 0.02f) * std::cos(y * 0.03f)));
        }
    }
    const auto at = [n](int i, int j) { return uint32_t(j * (n + 1) + i); };
    for (int j = 0; j < n; ++j) {
        for (int i = 0; i < n; ++i) {
            for (const uint32_t index : { at(i, j), at(i + 1, j), at(i + 1, j + 1), at(i, j),
                                          at(i + 1, j + 1), at(i, j + 1) })
                mesh.indices.push_back(index);
        }
    }
    return mesh;
}

/// The nearest point of a triangle by brute force: sample it densely. Slow and
/// obviously right, which is what a reference is for.
double bruteDistance(const TriMesh& mesh, const Vec3& p)
{
    double best = std::numeric_limits<double>::infinity();
    for (std::size_t t = 0; t < mesh.indices.size(); t += 3) {
        const Vec3 a = Vec3::fromVector(mesh.positions[mesh.indices[t]]);
        const Vec3 b = Vec3::fromVector(mesh.positions[mesh.indices[t + 1]]);
        const Vec3 c = Vec3::fromVector(mesh.positions[mesh.indices[t + 2]]);
        constexpr int kSteps = 40;
        for (int i = 0; i <= kSteps; ++i) {
            for (int j = 0; i + j <= kSteps; ++j) {
                const double u = double(i) / kSteps;
                const double v = double(j) / kSteps;
                best = std::min(best, distance(p, a + (b - a) * u + (c - a) * v));
            }
        }
    }
    return best;
}

} // namespace

class TestMeshQuery : public QObject {
    Q_OBJECT

private slots:
    void aRayMeetsTheNearFaceFirst();
    void aRayThatMissesSaysSo();
    void aRayStopsWhereItWasToldTo();
    void distanceIsToTheNearestFeature();
    void anEmptyMeshAnswersNothing();
    void aDeepTreeAgreesWithBruteForce();
};

void TestMeshQuery::aRayMeetsTheNearFaceFirst()
{
    const MeshQuery query(box(Vec3(-10, -10, -10), Vec3(10, 10, 10)));
    QCOMPARE(query.triangleCount(), std::size_t(12));

    // From outside, straight in: the near face, not the far one.
    const std::optional<RayHit> hit = query.castRay(Vec3(-50, 1, 2), Vec3(1, 0, 0));
    QVERIFY(hit.has_value());
    QCOMPARE(hit->distance, 40.0);
    QCOMPARE(hit->point.x, -10.0);

    // A direction that is not unit length means the same ray.
    const std::optional<RayHit> scaled = query.castRay(Vec3(-50, 1, 2), Vec3(7, 0, 0));
    QVERIFY(scaled.has_value());
    QCOMPARE(scaled->distance, 40.0);

    // From inside, a face is met from its back -- which a chassis tube's
    // normals, pointing whichever way the exporter felt like, have to allow.
    const std::optional<RayHit> inside = query.castRay(Vec3(0, 0, 0), Vec3(0, 0, -1));
    QVERIFY(inside.has_value());
    QCOMPARE(inside->distance, 10.0);
}

void TestMeshQuery::aRayThatMissesSaysSo()
{
    const MeshQuery query(box(Vec3(-10, -10, -10), Vec3(10, 10, 10)));
    QVERIFY(!query.castRay(Vec3(-50, 0, 0), Vec3(-1, 0, 0)).has_value());
    QVERIFY(!query.castRay(Vec3(-50, 30, 0), Vec3(1, 0, 0)).has_value());
    // No direction is no ray.
    QVERIFY(!query.castRay(Vec3(-50, 0, 0), Vec3(0, 0, 0)).has_value());
}

void TestMeshQuery::aRayStopsWhereItWasToldTo()
{
    const MeshQuery query(box(Vec3(-10, -10, -10), Vec3(10, 10, 10)));
    QVERIFY(!query.castRay(Vec3(-50, 0, 0), Vec3(1, 0, 0), 39.0).has_value());
    QVERIFY(query.castRay(Vec3(-50, 0, 0), Vec3(1, 0, 0), 41.0).has_value());
}

void TestMeshQuery::distanceIsToTheNearestFeature()
{
    const MeshQuery query(box(Vec3(-10, -10, -10), Vec3(10, 10, 10)));
    // Off a face, an edge and a corner.
    QCOMPARE(query.distanceTo(Vec3(25, 0, 0)), 15.0);
    QVERIFY(std::abs(query.distanceTo(Vec3(13, 14, 0)) - 5.0) < 1e-12);
    QVERIFY(std::abs(query.distanceTo(Vec3(11, 12, 12)) - 3.0) < 1e-12);
    // Unsigned: from inside it is the way to the nearest wall.
    QCOMPARE(query.distanceTo(Vec3(7, 0, 0)), 3.0);
    QCOMPARE(query.distanceTo(Vec3(10, 3, 3)), 0.0);
}

void TestMeshQuery::anEmptyMeshAnswersNothing()
{
    const MeshQuery query{ TriMesh{} };
    QVERIFY(query.isEmpty());
    QVERIFY(!query.castRay(Vec3(), Vec3(1, 0, 0)).has_value());
    QVERIFY(std::isinf(query.distanceTo(Vec3())));
}

void TestMeshQuery::aDeepTreeAgreesWithBruteForce()
{
    const TriMesh mesh = sheet(24); // 1152 triangles
    const MeshQuery query(mesh);
    QCOMPARE(query.triangleCount(), mesh.indices.size() / 3);

    QRandomGenerator random(20260910);
    for (int i = 0; i < 25; ++i) {
        const Vec3 p(random.bounded(-40.0) + random.bounded(320.0), random.bounded(280.0),
                     random.bounded(120.0) - 60.0);
        // The reference samples each triangle on a grid, so it can only ever be
        // a little too far; the tree's answer is exact.
        const double exact = query.distanceTo(p);
        const double reference = bruteDistance(mesh, p);
        QVERIFY2(exact <= reference + 1e-9, qPrintable(QStringLiteral("%1 > %2").arg(exact).arg(reference)));
        QVERIFY2(reference - exact < 0.2, qPrintable(QStringLiteral("%1 vs %2").arg(exact).arg(reference)));

        // Straight down onto the sheet from above always lands on it, and
        // where it lands is on the surface.
        const Vec3 above(10.0 + random.bounded(220.0), 10.0 + random.bounded(220.0), 100.0);
        const std::optional<RayHit> hit = query.castRay(above, Vec3(0, 0, -1));
        QVERIFY(hit.has_value());
        QVERIFY(query.distanceTo(hit->point) < 1e-6);
    }
}

QTEST_APPLESS_MAIN(TestMeshQuery)
#include "test_mesh_query.moc"
