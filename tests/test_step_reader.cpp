#include "geom/MeshTopology.h"
#include "io/MeshImport.h"
#include "io/StepReader.h"

#include <QTest>

using namespace suspkin;

namespace {
QString fixture(const char* name)
{
    return QStringLiteral(SUSPKIN_FIXTURE_DIR) + QLatin1Char('/') + QLatin1String(name);
}
} // namespace

class TestStepReader : public QObject {
    Q_OBJECT

private slots:
    void readsAPlateWithAHole();
    void tessellationScalesWithTheModel();
    void reportsAnUnreadableFile();
    void dispatchesByExtension();
};

void TestStepReader::readsAPlateWithAHole()
{
    const MeshLoadResult result = readStep(fixture("plate_with_hole.step"));
    QVERIFY2(result.ok(), qPrintable(result.error));
    QCOMPARE(result.formatName, QStringLiteral("STEP"));

    // The fixture is an 80 x 50 x 8 mm plate; the reader must preserve the units
    // the file declares rather than rescaling.
    const QVector3D extent = result.mesh->bounds.extent();
    QVERIFY2(std::abs(extent.x() - 80.0f) < 0.5f, qPrintable(QString::number(extent.x())));
    QVERIFY2(std::abs(extent.y() - 50.0f) < 0.5f, qPrintable(QString::number(extent.y())));
    QVERIFY2(std::abs(extent.z() - 8.0f) < 0.5f, qPrintable(QString::number(extent.z())));

    QVERIFY(result.mesh->triangleCount() > 50);

    // A closed solid: every edge must be shared by exactly two faces, so the
    // tessellation is watertight rather than a pile of disconnected patches.
    const EdgeSet edges = buildEdges(*result.mesh);
    QVERIFY(edges.allCount() > 0);
    const std::size_t v = result.mesh->vertexCount();
    const std::size_t e = edges.allCount();
    const std::size_t f = result.mesh->triangleCount();
    // Euler characteristic of a solid with one through hole (a torus) is 0.
    QCOMPARE(static_cast<long long>(v) - static_cast<long long>(e)
                 + static_cast<long long>(f), 0LL);
}

void TestStepReader::tessellationScalesWithTheModel()
{
    // Curved faces must actually be subdivided, not reduced to a single facet.
    const MeshLoadResult result = readStep(fixture("plate_with_hole.step"));
    QVERIFY2(result.ok(), qPrintable(result.error));

    int nonAxisAligned = 0;
    for (const QVector3D& n : result.mesh->faceNormals) {
        const bool axis = std::abs(std::abs(n.x()) - 1.0f) < 1e-3f
                       || std::abs(std::abs(n.y()) - 1.0f) < 1e-3f
                       || std::abs(std::abs(n.z()) - 1.0f) < 1e-3f;
        if (!axis) ++nonAxisAligned;
    }
    // The bore wall contributes many differently-angled facets.
    QVERIFY2(nonAxisAligned > 20, qPrintable(QString::number(nonAxisAligned)));
}

void TestStepReader::reportsAnUnreadableFile()
{
    const MeshLoadResult result = readStep(fixture("does_not_exist.step"));
    QVERIFY(!result.ok());
    QVERIFY(!result.error.isEmpty());
}

void TestStepReader::dispatchesByExtension()
{
    QVERIFY(stepImportSupported());
    // The generic entry point must route .step to the STEP reader.
    const MeshLoadResult result = importMeshFile(fixture("plate_with_hole.step"));
    QVERIFY2(result.ok(), qPrintable(result.error));
    QCOMPARE(result.formatName, QStringLiteral("STEP"));
    QVERIFY(importFileFilter().contains(QStringLiteral("*.step")));
}

QTEST_APPLESS_MAIN(TestStepReader)
#include "test_step_reader.moc"
