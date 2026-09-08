#include "geom/MeshTopology.h"
#include "io/StlReader.h"

#include <QTest>

#include <cmath>

using namespace suspkin;

namespace {

QString dataPath(const char* name)
{
    return QStringLiteral(SUSPKIN_TEST_DATA_DIR) + QLatin1Char('/') + QLatin1String(name);
}

} // namespace

class TestStlReader : public QObject {
    Q_OBJECT

private slots:
    void detectsFormatFromSize_data();
    void detectsFormatFromSize();

    void asciiAndBinaryCubeAgree();
    void cubeWeldsAndCreases();
    void recomputesNormalsWhenFileStoresZero();
    void smoothSphereHasFewFeatureEdges();
    void skipsDegenerateTriangles();
    void reportsTruncatedBinary();

    void asciiFileSurvivingThirtyTwoBitOverflow();
};

void TestStlReader::detectsFormatFromSize_data()
{
    QTest::addColumn<QString>("file");
    QTest::addColumn<bool>("binary");

    QTest::newRow("ascii cube")            << dataPath("cube_ascii.stl")       << false;
    QTest::newRow("binary cube")           << dataPath("cube_bin.stl")         << true;
    // Binary payload behind a header that opens with "solid": the exact case a
    // leading-token sniff gets wrong.
    QTest::newRow("binary, solid header")  << dataPath("solid_header_bin.stl") << true;
    QTest::newRow("binary sphere")         << dataPath("sphere_bin.stl")       << true;
    QTest::newRow("ascii overflow")        << dataPath("overflow_ascii.stl")   << false;
}

void TestStlReader::detectsFormatFromSize()
{
    QFETCH(QString, file);
    QFETCH(bool, binary);

    const MeshLoadResult result = readStl(file);
    QVERIFY2(result.ok(), qPrintable(result.error));
    QCOMPARE(result.wasBinary, binary);
}

void TestStlReader::asciiAndBinaryCubeAgree()
{
    const MeshLoadResult a = readStl(dataPath("cube_ascii.stl"));
    const MeshLoadResult b = readStl(dataPath("cube_bin.stl"));
    QVERIFY2(a.ok(), qPrintable(a.error));
    QVERIFY2(b.ok(), qPrintable(b.error));

    QCOMPARE(a.mesh->triangleCount(), b.mesh->triangleCount());
    QCOMPARE(a.mesh->vertexCount(), b.mesh->vertexCount());
    QCOMPARE(a.mesh->bounds.center(), b.mesh->bounds.center());
}

void TestStlReader::cubeWeldsAndCreases()
{
    const MeshLoadResult result = readStl(dataPath("cube_bin.stl"));
    QVERIFY2(result.ok(), qPrintable(result.error));

    // 12 triangles welding to 8 corners; Euler gives 8 - E + 12 = 2, so E = 18.
    QCOMPARE(result.mesh->triangleCount(), std::size_t(12));
    QCOMPARE(result.mesh->vertexCount(), std::size_t(8));
    QCOMPARE(result.skippedDegenerate, 0);

    const EdgeSet edges = buildEdges(*result.mesh);
    QCOMPARE(edges.allCount(), std::size_t(18));
    // Of those 18, the 12 real cube edges are 90-degree creases and the 6 face
    // diagonals are coplanar, so exactly 12 survive the crease test.
    QCOMPARE(edges.featureCount(), std::size_t(12));
}

void TestStlReader::recomputesNormalsWhenFileStoresZero()
{
    const MeshLoadResult result = readStl(dataPath("zero_normals.stl"));
    QVERIFY2(result.ok(), qPrintable(result.error));
    QCOMPARE(result.mesh->faceNormals.size(), std::size_t(12));

    for (const QVector3D& n : result.mesh->faceNormals) {
        QVERIFY2(std::abs(n.length() - 1.0f) < 1e-4f, "normal was not recomputed to unit length");
        // Every face of an axis-aligned cube has exactly one non-zero component.
        const int axes = int(std::abs(n.x()) > 0.9f) + int(std::abs(n.y()) > 0.9f)
                       + int(std::abs(n.z()) > 0.9f);
        QCOMPARE(axes, 1);
    }
}

void TestStlReader::smoothSphereHasFewFeatureEdges()
{
    const MeshLoadResult result = readStl(dataPath("sphere_bin.stl"));
    QVERIFY2(result.ok(), qPrintable(result.error));

    const EdgeSet edges = buildEdges(*result.mesh);
    QVERIFY(edges.allCount() > 1000);
    // A smoothly tessellated sphere has no creases worth drawing. This is what
    // makes feature edges the right wireframe default: all-edges here would be a
    // solid mass of lines.
    QVERIFY2(edges.featureCount() * 4 < edges.allCount(),
             qPrintable(QStringLiteral("feature=%1 all=%2")
                            .arg(edges.featureCount()).arg(edges.allCount())));
}

void TestStlReader::skipsDegenerateTriangles()
{
    const MeshLoadResult result = readStl(dataPath("degenerate.stl"));
    QVERIFY2(result.ok(), qPrintable(result.error));

    // The two zero-area facets are dropped; the cube survives intact.
    QCOMPARE(result.skippedDegenerate, 2);
    QCOMPARE(result.mesh->triangleCount(), std::size_t(12));
    for (const QVector3D& n : result.mesh->faceNormals)
        QVERIFY(std::isfinite(n.length()) && n.length() > 0.5f);
}

void TestStlReader::reportsTruncatedBinary()
{
    const MeshLoadResult result = readStl(dataPath("truncated.stl"));
    QVERIFY(!result.ok());
    QVERIFY(!result.error.isEmpty());
    QVERIFY2(result.error.contains(QStringLiteral("truncated")),
             qPrintable(QStringLiteral("unhelpful error: %1").arg(result.error)));
}

void TestStlReader::asciiFileSurvivingThirtyTwoBitOverflow()
{
    // This file is valid ASCII STL, but its bytes at offset 80 decode to a count
    // whose 32-bit product 84 + 50*count wraps to exactly the file size. Computed
    // narrow, the format check would call it binary and parse text as floats.
    const QByteArray raw = [] {
        QFile f(dataPath("overflow_ascii.stl"));
        return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
    }();
    QVERIFY(!raw.isEmpty());

    std::uint32_t claimed = 0;
    std::memcpy(&claimed, raw.constData() + 80, sizeof claimed);
    claimed = qFromLittleEndian(claimed);

    // Confirm the fixture really is the trap it claims to be, so this test cannot
    // quietly stop testing anything.
    const quint64 wrapped =
        (84ULL + 50ULL * static_cast<quint64>(claimed)) & 0xFFFFFFFFULL;
    QCOMPARE(wrapped, static_cast<quint64>(raw.size()));
    QVERIFY(84ULL + 50ULL * static_cast<quint64>(claimed) > static_cast<quint64>(raw.size()));

    QVERIFY(!isBinaryStl(raw.constData(), raw.size()));

    const MeshLoadResult result = readStl(dataPath("overflow_ascii.stl"));
    QVERIFY2(result.ok(), qPrintable(result.error));
    QCOMPARE(result.wasBinary, false);
    QCOMPARE(result.mesh->triangleCount(), std::size_t(12));
    QCOMPARE(result.mesh->vertexCount(), std::size_t(8));
}

QTEST_APPLESS_MAIN(TestStlReader)
#include "test_stl_reader.moc"
