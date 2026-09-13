#include "geom/MeshTopology.h"
#include "io/MeshImport.h"
#include "io/StepReader.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
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
    void carriesExactSurfaceNormals();
    void readsAPathWithNonAsciiCharacters();
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

void TestStepReader::carriesExactSurfaceNormals()
{
    const MeshLoadResult result = readStep(fixture("plate_with_hole.step"));
    QVERIFY2(result.ok(), qPrintable(result.error));

    // Importing STEP rather than a mesh is only worth it if the analytic normals
    // survive: the triangles approximate the surface, the normals do not.
    QVERIFY2(result.mesh->hasCornerNormals(), "no per-corner normals were produced");
    for (const QVector3D& n : result.mesh->cornerNormals)
        QVERIFY2(std::abs(n.length() - 1.0f) < 1e-3f, "a corner normal is not unit length");

    // On the curved bore the three corners of a triangle must disagree. If every
    // triangle's corners shared one normal these would just be facet normals
    // wearing a disguise, and the hole would shade as a prism.
    int varyingTriangles = 0;
    for (std::size_t t = 0; t < result.mesh->triangleCount(); ++t) {
        const QVector3D& a = result.mesh->cornerNormals[3 * t + 0];
        const QVector3D& b = result.mesh->cornerNormals[3 * t + 1];
        const QVector3D& c = result.mesh->cornerNormals[3 * t + 2];
        if ((a - b).length() > 1e-4f || (b - c).length() > 1e-4f) ++varyingTriangles;
    }
    QVERIFY2(varyingTriangles > 10,
             qPrintable(QStringLiteral("only %1 triangles have varying normals")
                            .arg(varyingTriangles)));
}

void TestStepReader::readsAPathWithNonAsciiCharacters()
{
    // A user's own name in the path is enough to break this: Open CASCADE reads
    // a narrow path as UTF-8, and the local 8-bit encoding Windows still uses
    // handed it bytes it could not decode, so the file was never opened and the
    // failure read as a malformed STEP.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    // Spelled with escapes so the test does not depend on how the compiler
    // decodes this source file.
    const QString sharpS(QChar(0x00DF));   // ss
    const QString uUmlaut(QChar(0x00FC));  // u"
    const QString folder =
        dir.filePath(QStringLiteral("Loh") + sharpS + QStringLiteral(" M") + uUmlaut
                     + QStringLiteral("ller"));
    QVERIFY(QDir().mkpath(folder));
    const QString copy = folder + QStringLiteral("/pl") + QString(QChar(0x00E4))
                       + QStringLiteral("ttchen.step");
    QVERIFY(QFile::copy(fixture("plate_with_hole.step"), copy));

    const MeshLoadResult result = readStep(copy);
    QVERIFY2(result.ok(), qPrintable(result.error));
    QVERIFY(result.mesh->triangleCount() > 50);
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
