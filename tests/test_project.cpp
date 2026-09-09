#include "project/Project.h"

#include <QFile>
#include <QTemporaryDir>
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

HardpointTable sample()
{
    HardpointTable table;
    table.points.push_back(make("F_LCA_O", -544.26, 554.412, 138.408));
    table.points.push_back(make("F_LCA_F", -300.0, 220.0, 150.0));
    return table;
}

} // namespace

class TestProject : public QObject {
    Q_OBJECT

private slots:
    void createsADirectoryWithAManifest();
    void refusesToCreateOverAnExistingProject();
    void roundTripsEverythingItHolds();
    void opensByDirectoryAsWellAsByFile();
    void rejectsSomethingThatIsNotAProject();
    void copiesImportedFilesIn();
    void keepsTheWheelCornersWithoutAnyModels();
    void reimportingTheProjectsOwnCopyKeepsIt();

    void editsAreTheDifferenceFromTheWorkbook();
    void editsRoundTripThroughTheirFile();
    void anEmptyEditSetLeavesNoFileBehind();
    void appliedEditsRestoreTheTableExactly();
};

void TestProject::createsADirectoryWithAManifest()
{
    QTemporaryDir directory;
    const QString root = directory.filePath(QStringLiteral("MyCar"));

    QString error;
    const std::optional<Project> project = Project::create(root, QStringLiteral("MyCar"), &error);
    QVERIFY2(project.has_value(), qPrintable(error));
    QVERIFY(QFile::exists(project->manifestPath()));
    QCOMPARE(project->name(), QStringLiteral("MyCar"));
    QVERIFY(project->created().isValid());
}

void TestProject::refusesToCreateOverAnExistingProject()
{
    QTemporaryDir directory;
    const QString root = directory.filePath(QStringLiteral("MyCar"));

    QString error;
    QVERIFY(Project::create(root, QStringLiteral("MyCar"), &error).has_value());
    // Overwriting would silently discard someone's work, so it is refused.
    QVERIFY(!Project::create(root, QStringLiteral("MyCar"), &error).has_value());
    QVERIFY(!error.isEmpty());
}

void TestProject::roundTripsEverythingItHolds()
{
    QTemporaryDir directory;
    QString error;
    std::optional<Project> project =
        Project::create(directory.filePath(QStringLiteral("P")), QStringLiteral("P"), &error);
    QVERIFY2(project.has_value(), qPrintable(error));

    ViewState view;
    view.camera.pivot = QVector3D(1.5f, -2.5f, 3.5f);
    view.camera.distance = 1234.5f;
    view.camera.azimuthDeg = -37.5f;
    view.camera.elevationDeg = 12.25f;
    view.camera.sceneRadius = 800.0f;
    view.cameraValid = true;
    view.displayMode = DisplayMode::Triangles;
    view.labelsVisible = false;
    view.linksVisible = false;
    view.wheelsVisible = false;
    view.selectedHardpoint = 4;
    project->setView(view);

    WindowState window;
    window.geometry = QByteArray("\x01\x02\x00\x03", 4);
    window.dockState = QByteArray("dock-state");
    project->setWindow(window);

    MirrorSpec mirror;
    mirror.axis = MirrorAxis::X;
    mirror.naming = MirrorNaming::Replace;
    mirror.findText = QStringLiteral("L_");
    mirror.replaceText = QStringLiteral("R_");
    mirror.caseSensitive = true;
    mirror.updateExisting = false;
    mirror.skipMirrored = false;
    project->setMirror(mirror);

    AssetRef geometry;
    geometry.relativePath = QStringLiteral("geometry/upright.step");
    geometry.originalPath = QStringLiteral("/somewhere/upright.step");
    project->setGeometry(geometry);

    HardpointRef hardpoints;
    hardpoints.workbook.relativePath = QStringLiteral("hardpoints/points.xlsx");
    hardpoints.sheetName = QStringLiteral("Geometry");
    hardpoints.mirrored.insert(QStringLiteral("F_LCA_O_R"), QStringLiteral("F_LCA_O"));
    HardpointConfig ballJoint;
    ballJoint.type = PointType::Solved;
    ballJoint.part1 = QStringLiteral("Lower wishbone");
    ballJoint.part2 = QStringLiteral("Upright");
    ballJoint.bushing = 12;
    hardpoints.config.insert(QStringLiteral("F_LCA_O"), ballJoint);
    // A row the user cleared on purpose. It has to come back as an entry that
    // says nothing, not as no entry at all, or the next open would infer it
    // again and undo the clearing.
    hardpoints.config.insert(QStringLiteral("F_LCA_IF"), HardpointConfig{});
    project->setHardpoints(hardpoints);

    AssetRef linkage;
    linkage.relativePath = QStringLiteral("linkage/template.json");
    project->setLinkageTemplate(linkage);

    WheelsRef wheels;
    wheels.wheel.relativePath = QStringLiteral("wheels/wheel.step");
    wheels.wheel.originalPath = QStringLiteral("/somewhere/tyre_205_50R15.step");
    wheels.rim.relativePath = QStringLiteral("wheels/rim.step");
    wheels.spec.setPoint(WheelCorner::FrontLeft, QStringLiteral("F_WheelCenter"));
    wheels.spec.setPoint(WheelCorner::RearRight, QStringLiteral("R_WheelCenter_R"));
    wheels.spec.modelSide = WheelModelSide::Right;
    wheels.spec.alignToCenter = false;
    project->setWheels(wheels);

    project->setLastGeometryDirectory(QStringLiteral("/tmp/cad"));
    QVERIFY2(project->save(&error), qPrintable(error));

    const std::optional<Project> reopened = Project::open(project->manifestPath(), &error);
    QVERIFY2(reopened.has_value(), qPrintable(error));

    QCOMPARE(reopened->name(), QStringLiteral("P"));
    QVERIFY(reopened->view().cameraValid);
    QCOMPARE(reopened->view().camera.pivot, view.camera.pivot);
    QCOMPARE(reopened->view().camera.distance, view.camera.distance);
    QCOMPARE(reopened->view().camera.azimuthDeg, view.camera.azimuthDeg);
    QCOMPARE(reopened->view().camera.elevationDeg, view.camera.elevationDeg);
    QCOMPARE(reopened->view().camera.sceneRadius, view.camera.sceneRadius);
    QCOMPARE(reopened->view().displayMode, DisplayMode::Triangles);
    QCOMPARE(reopened->view().labelsVisible, false);
    QCOMPARE(reopened->view().linksVisible, false);
    QCOMPARE(reopened->view().wheelsVisible, false);
    QCOMPARE(reopened->view().selectedHardpoint, 4);
    // Binary, with an embedded NUL: base64 in the manifest has to survive it.
    QCOMPARE(reopened->window().geometry, window.geometry);
    QCOMPARE(reopened->window().dockState, window.dockState);
    QCOMPARE(reopened->mirror(), mirror);
    QCOMPARE(reopened->geometry().relativePath, geometry.relativePath);
    QCOMPARE(reopened->geometry().originalPath, geometry.originalPath);
    QCOMPARE(reopened->linkageTemplate().relativePath, linkage.relativePath);
    QCOMPARE(reopened->wheels().wheel.relativePath, wheels.wheel.relativePath);
    QCOMPARE(reopened->wheels().wheel.originalPath, wheels.wheel.originalPath);
    QCOMPARE(reopened->wheels().rim.relativePath, wheels.rim.relativePath);
    // Corners the user did not pick stay unpicked rather than coming back empty
    // as some other corner's point.
    QCOMPARE(reopened->wheels().spec, wheels.spec);
    QCOMPARE(reopened->hardpoints().sheetName, QStringLiteral("Geometry"));
    // A workbook cannot say which points are mirrors, so the project must.
    QCOMPARE(reopened->hardpoints().mirrored.value(QStringLiteral("F_LCA_O_R")),
             QStringLiteral("F_LCA_O"));
    // Nor can it say what a point is for, or which bushing acts at it.
    QCOMPARE(reopened->hardpoints().config.value(QStringLiteral("F_LCA_O")), ballJoint);
    QVERIFY(reopened->hardpoints().config.contains(QStringLiteral("F_LCA_IF")));
    QVERIFY(reopened->hardpoints().config.value(QStringLiteral("F_LCA_IF")).isEmpty());
    QCOMPARE(reopened->lastGeometryDirectory(), QStringLiteral("/tmp/cad"));
}

void TestProject::keepsTheWheelCornersWithoutAnyModels()
{
    QTemporaryDir directory;
    QString error;
    std::optional<Project> project =
        Project::create(directory.filePath(QStringLiteral("P")), QStringLiteral("P"), &error);
    QVERIFY2(project.has_value(), qPrintable(error));

    // Taking the models out is not the same as saying the wheels were never on
    // these points, so the corners have to survive a save with nothing to draw.
    WheelsRef wheels;
    wheels.spec.setPoint(WheelCorner::FrontLeft, QStringLiteral("F_WheelCenter"));
    QVERIFY(wheels.isEmpty());
    project->setWheels(wheels);
    QVERIFY2(project->save(&error), qPrintable(error));

    const std::optional<Project> reopened = Project::open(project->manifestPath(), &error);
    QVERIFY2(reopened.has_value(), qPrintable(error));
    QCOMPARE(reopened->wheels().spec.point(WheelCorner::FrontLeft),
             QStringLiteral("F_WheelCenter"));
    QVERIFY(reopened->wheels().isEmpty());
}

void TestProject::opensByDirectoryAsWellAsByFile()
{
    QTemporaryDir directory;
    const QString root = directory.filePath(QStringLiteral("P"));
    QString error;
    QVERIFY(Project::create(root, QStringLiteral("P"), &error).has_value());

    const std::optional<Project> byDirectory = Project::open(root, &error);
    QVERIFY2(byDirectory.has_value(), qPrintable(error));
    QCOMPARE(QDir(byDirectory->rootPath()).canonicalPath(), QDir(root).canonicalPath());
}

void TestProject::rejectsSomethingThatIsNotAProject()
{
    QTemporaryDir directory;
    const QString path = directory.filePath(QStringLiteral("notes.suspkin"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("{\"format\":\"something-else\"}");
    file.close();

    QString error;
    QVERIFY(!Project::open(path, &error).has_value());
    QVERIFY(!error.isEmpty());
}

void TestProject::copiesImportedFilesIn()
{
    QTemporaryDir directory;
    QString error;
    std::optional<Project> project =
        Project::create(directory.filePath(QStringLiteral("P")), QStringLiteral("P"), &error);
    QVERIFY(project.has_value());

    const QString source = directory.filePath(QStringLiteral("part.stl"));
    QFile file(source);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("solid test\nendsolid test\n");
    file.close();

    const std::optional<AssetRef> asset =
        project->importAsset(source, QStringLiteral("geometry"), &error);
    QVERIFY2(asset.has_value(), qPrintable(error));
    QCOMPARE(asset->relativePath, QStringLiteral("geometry/part.stl"));
    QVERIFY(QFile::exists(project->absolutePath(asset->relativePath)));
    QCOMPARE(asset->originalPath, QFileInfo(source).absoluteFilePath());

    // The copy is the project's own, so deleting the original changes nothing.
    QVERIFY(QFile::remove(source));
    QVERIFY(QFile::exists(project->absolutePath(asset->relativePath)));
}

void TestProject::reimportingTheProjectsOwnCopyKeepsIt()
{
    QTemporaryDir directory;
    QString error;
    std::optional<Project> project =
        Project::create(directory.filePath(QStringLiteral("P")), QStringLiteral("P"), &error);
    QVERIFY(project.has_value());

    const QString source = directory.filePath(QStringLiteral("part.stl"));
    QFile file(source);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("solid test\nendsolid test\n");
    file.close();

    const std::optional<AssetRef> first =
        project->importAsset(source, QStringLiteral("geometry"), &error);
    QVERIFY(first.has_value());

    // Importing the copy the project already holds must not delete it first.
    const std::optional<AssetRef> again = project->importAsset(
        project->absolutePath(first->relativePath), QStringLiteral("geometry"), &error);
    QVERIFY2(again.has_value(), qPrintable(error));
    QVERIFY(QFile::exists(project->absolutePath(again->relativePath)));
    QCOMPARE(QFileInfo(project->absolutePath(again->relativePath)).size(), qint64(25));
}

void TestProject::editsAreTheDifferenceFromTheWorkbook()
{
    const HardpointTable baseline = sample();

    HardpointTable current = baseline;
    current.points[0].coord[2] = 999.0;
    current.points.push_back(make("F_LCA_O_R", -544.26, -554.412, 138.408));
    current.points.back().mirrorOf = QStringLiteral("F_LCA_O");

    // Provenance is the project's record, not the workbook's, so it must not on
    // its own make a point look edited.
    HardpointTable stamped = baseline;
    stamped.points[0].mirrorOf = QStringLiteral("SOMETHING");
    QVERIFY(diffHardpoints(baseline, stamped).isEmpty());

    const HardpointEdits edits = diffHardpoints(baseline, current);
    QCOMPARE(int(edits.changed.size()), 1);
    QCOMPARE(edits.changed.front().name, QStringLiteral("F_LCA_O"));
    QCOMPARE(int(edits.added.size()), 1);
    QCOMPARE(edits.added.front().name, QStringLiteral("F_LCA_O_R"));

    // An untouched table produces nothing, so opening a project and closing it
    // does not leave an edits file behind.
    QVERIFY(diffHardpoints(baseline, baseline).isEmpty());
}

void TestProject::editsRoundTripThroughTheirFile()
{
    QTemporaryDir directory;
    const QString path = directory.filePath(QStringLiteral("edits.json"));

    HardpointEdits edits;
    edits.changed.push_back(make("F_LCA_O", -544.26, 554.412, 999.0));
    Hardpoint mirrored = make("F_LCA_O_R", -544.26, -554.412, 138.408);
    mirrored.mirrorOf = QStringLiteral("F_LCA_O");
    edits.added.push_back(mirrored);

    QString error;
    QVERIFY2(writeHardpointEdits(path, edits, &error), qPrintable(error));

    const std::optional<HardpointEdits> read = readHardpointEdits(path, &error);
    QVERIFY2(read.has_value(), qPrintable(error));
    QCOMPARE(int(read->changed.size()), 1);
    QCOMPARE(read->changed.front().coord[2], 999.0);
    QCOMPARE(int(read->added.size()), 1);
    QCOMPARE(read->added.front().mirrorOf, QStringLiteral("F_LCA_O"));
    // Full double precision, not the three decimals the table shows.
    QCOMPARE(read->added.front().coord[1], -554.412);
}

void TestProject::anEmptyEditSetLeavesNoFileBehind()
{
    QTemporaryDir directory;
    const QString path = directory.filePath(QStringLiteral("edits.json"));

    QString error;
    QVERIFY(writeHardpointEdits(path, HardpointEdits{ { make("A", 1, 2, 3) }, {} }, &error));
    QVERIFY(QFile::exists(path));

    // Writing back an empty set removes the file, rather than leaving one that
    // claims there is something pending.
    QVERIFY(writeHardpointEdits(path, HardpointEdits{}, &error));
    QVERIFY(!QFile::exists(path));

    // Reading what is not there is not a failure.
    const std::optional<HardpointEdits> read = readHardpointEdits(path, &error);
    QVERIFY(read.has_value());
    QVERIFY(read->isEmpty());
}

void TestProject::appliedEditsRestoreTheTableExactly()
{
    const HardpointTable baseline = sample();

    HardpointTable current = baseline;
    current.points[1].coord[0] = -301.5;
    Hardpoint mirrored = make("F_LCA_F_R", -301.5, -220.0, 150.0);
    mirrored.mirrorOf = QStringLiteral("F_LCA_F");
    current.points.push_back(mirrored);

    const HardpointTable restored = applyHardpointEdits(baseline, diffHardpoints(baseline, current));
    QCOMPARE(int(restored.size()), int(current.size()));
    for (std::size_t i = 0; i < current.points.size(); ++i) {
        QCOMPARE(restored.points[i].name, current.points[i].name);
        for (int axis = 0; axis < 3; ++axis)
            QCOMPARE(restored.points[i].coord[axis], current.points[i].coord[axis]);
    }
}

QTEST_APPLESS_MAIN(TestProject)
#include "test_project.moc"
