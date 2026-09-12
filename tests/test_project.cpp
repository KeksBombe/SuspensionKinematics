#include "project/Project.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
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
    void aProjectFileReadsBackAndCanThenBeReplaced();
    void anOlderProjectsWheelModelIsItsTyre();
    void reimportingTheProjectsOwnCopyKeepsIt();
    void everySweepsTravelIsRememberedNotJustTheOneBeingSwept();
    void anOlderProjectsSingleRangeBecomesThatKindsOwnTravel();
    void staticAnglesAreKeptPerAxle();

    void editsAreTheDifferenceFromTheWorkbook();
    void editsRoundTripThroughTheirFile();
    void anEmptyEditSetLeavesNoFileBehind();
    void appliedEditsRestoreTheTableExactly();
    void removedPointsRoundTripThroughTheirFile();
    void aRenamedWorkbookPointIsARemovalAndAnAddition();
    void anAddedPointComesBackWhereItWasPut();
    void anOlderEditsFileStillAppendsItsPoints();
    void aSelectionOfOneFromAnOlderProjectIsStillASelection();
    void aProjectCanMakeAWorkbookOfItsOwn();
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
    // In the order they were picked, which is not row order: a chain of points
    // picked for a new part is picked the way it is drawn.
    view.selection = { 7, 4, 2 };
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
    wheels.tyre.relativePath = QStringLiteral("wheels/tyre.step");
    wheels.tyre.originalPath = QStringLiteral("/somewhere/tyre_205_50R15.step");
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
    QCOMPARE(reopened->view().selection, (QList<int>{ 7, 4, 2 }));
    // Binary, with an embedded NUL: base64 in the manifest has to survive it.
    QCOMPARE(reopened->window().geometry, window.geometry);
    QCOMPARE(reopened->window().dockState, window.dockState);
    QCOMPARE(reopened->mirror(), mirror);
    QCOMPARE(reopened->geometry().relativePath, geometry.relativePath);
    QCOMPARE(reopened->geometry().originalPath, geometry.originalPath);
    QCOMPARE(reopened->linkageTemplate().relativePath, linkage.relativePath);
    QCOMPARE(reopened->wheels().tyre.relativePath, wheels.tyre.relativePath);
    QCOMPARE(reopened->wheels().tyre.originalPath, wheels.tyre.originalPath);
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

void TestProject::aProjectFileReadsBackAndCanThenBeReplaced()
{
    QTemporaryDir directory;
    QString error;
    std::optional<Project> project =
        Project::create(directory.filePath(QStringLiteral("P")), QStringLiteral("P"), &error);
    QVERIFY2(project.has_value(), qPrintable(error));

    const QString relative = QStringLiteral("linkage/template.json");
    QVERIFY2(project->writeFile(relative, QByteArray("{ \"a\": 1 }"), &error), qPrintable(error));
    const std::optional<QByteArray> read = project->readFile(relative, &error);
    QVERIFY2(read.has_value(), qPrintable(error));
    QCOMPARE(*read, QByteArray("{ \"a\": 1 }"));

    // Read, patched and written back over itself: what Linkage > Steering Rack
    // does, and what Windows refused while the read was still holding it open.
    QVERIFY2(project->writeFile(relative, *read + "\n", &error), qPrintable(error));
    QCOMPARE(*project->readFile(relative, &error), QByteArray("{ \"a\": 1 }\n"));

    // A file that is not there is nothing, and says why.
    error.clear();
    QVERIFY(!project->readFile(QStringLiteral("linkage/none.json"), &error).has_value());
    QVERIFY(!error.isEmpty());
}

void TestProject::anOlderProjectsWheelModelIsItsTyre()
{
    // Before a wheel was understood to be the tyre and the rim together, the
    // tyre was stored as the "wheel", in wheels/wheel.<ext>.
    QTemporaryDir directory;
    QString error;
    std::optional<Project> project =
        Project::create(directory.filePath(QStringLiteral("Old")), QStringLiteral("Old"), &error);
    QVERIFY2(project.has_value(), qPrintable(error));
    QVERIFY2(project->save(&error), qPrintable(error));

    QFile manifest(project->manifestPath());
    QVERIFY(manifest.open(QIODevice::ReadOnly));
    QJsonObject root = QJsonDocument::fromJson(manifest.readAll()).object();
    manifest.close();
    QJsonObject tyre;
    tyre.insert(QStringLiteral("path"), QStringLiteral("wheels/wheel.stp"));
    QJsonObject wheels;
    wheels.insert(QStringLiteral("wheel"), tyre);
    root.insert(QStringLiteral("wheels"), wheels);
    QVERIFY(manifest.open(QIODevice::WriteOnly | QIODevice::Truncate));
    manifest.write(QJsonDocument(root).toJson());
    manifest.close();

    std::optional<Project> reopened = Project::open(project->manifestPath(), &error);
    QVERIFY2(reopened.has_value(), qPrintable(error));
    // Still the file it always was: nothing on disk is renamed.
    QCOMPARE(reopened->wheels().tyre.relativePath, QStringLiteral("wheels/wheel.stp"));

    // Written back under its new name, and read back from there.
    QVERIFY2(reopened->save(&error), qPrintable(error));
    QVERIFY(manifest.open(QIODevice::ReadOnly));
    const QJsonObject saved =
        QJsonDocument::fromJson(manifest.readAll()).object().value(QStringLiteral("wheels")).toObject();
    manifest.close();
    QVERIFY(saved.contains(QStringLiteral("tyre")));
    QVERIFY(!saved.contains(QStringLiteral("wheel")));
    const std::optional<Project> again = Project::open(project->manifestPath(), &error);
    QVERIFY2(again.has_value(), qPrintable(error));
    QCOMPARE(again->wheels().tyre.relativePath, QStringLiteral("wheels/wheel.stp"));
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

void TestProject::removedPointsRoundTripThroughTheirFile()
{
    const HardpointTable baseline = sample();
    HardpointTable current = baseline;
    current.points.erase(current.points.begin()); // F_LCA_O, deleted

    const HardpointEdits edits = diffHardpoints(baseline, current);
    QCOMPARE(edits.removed, QStringList{ QStringLiteral("F_LCA_O") });
    // A deletion is an edit: the project has something the workbook does not.
    QVERIFY(!edits.isEmpty());
    QCOMPARE(edits.count(), 1);

    QTemporaryDir directory;
    const QString path = directory.filePath(QStringLiteral("edits.json"));
    QString error;
    QVERIFY2(writeHardpointEdits(path, edits, &error), qPrintable(error));

    const std::optional<HardpointEdits> read = readHardpointEdits(path, &error);
    QVERIFY2(read.has_value(), qPrintable(error));
    QCOMPARE(read->removed, edits.removed);

    // And reopening is deleting it again, not bringing it back.
    const HardpointTable reopened = applyHardpointEdits(baseline, *read);
    QCOMPARE(int(reopened.size()), 1);
    QCOMPARE(reopened.indexOf(QStringLiteral("F_LCA_O")), -1);
}

void TestProject::aRenamedWorkbookPointIsARemovalAndAnAddition()
{
    const HardpointTable baseline = sample();
    HardpointTable current = baseline;
    current.points[0].name = QStringLiteral("F_LCA_OUT");

    // The name is the key the workbook is written back through, so to the
    // workbook a rename is its old rows going and a new point arriving.
    const HardpointEdits edits = diffHardpoints(baseline, current);
    QCOMPARE(edits.removed, QStringList{ QStringLiteral("F_LCA_O") });
    QCOMPARE(int(edits.added.size()), 1);
    QCOMPARE(edits.added.front().name, QStringLiteral("F_LCA_OUT"));
    QVERIFY(edits.changed.empty());

    // A point that exists only as an edit is renamed as itself: nothing in any
    // workbook has to be taken out for it.
    HardpointTable withNew = baseline;
    withNew.points.push_back(make("NEW", 1, 2, 3));
    HardpointTable renamedNew = withNew;
    renamedNew.points.back().name = QStringLiteral("NEWER");
    const HardpointEdits plain = diffHardpoints(baseline, renamedNew);
    QVERIFY(plain.removed.isEmpty());
    QCOMPARE(plain.added.front().name, QStringLiteral("NEWER"));
}

void TestProject::anAddedPointComesBackWhereItWasPut()
{
    HardpointTable baseline;
    baseline.points.push_back(make("A", 1, 0, 0));
    baseline.points.push_back(make("B", 2, 0, 0));
    baseline.points.push_back(make("C", 3, 0, 0));

    // A point added next to the one it belongs with, one added at the very top,
    // a run of two added together, and a workbook point renamed in place.
    HardpointTable current;
    current.points.push_back(make("TOP", 0, 0, 0));
    current.points.push_back(make("A", 1, 0, 0));
    current.points.push_back(make("A2", 1.5, 0, 0));
    current.points.push_back(make("A3", 1.7, 0, 0));
    current.points.push_back(make("BEE", 2, 0, 0)); // was B
    current.points.push_back(make("C", 3, 0, 0));

    QTemporaryDir directory;
    const QString path = directory.filePath(QStringLiteral("edits.json"));
    QString error;
    QVERIFY(writeHardpointEdits(path, diffHardpoints(baseline, current), &error));
    const std::optional<HardpointEdits> read = readHardpointEdits(path, &error);
    QVERIFY2(read.has_value(), qPrintable(error));

    // Reopened, the table reads the way it was left, not with every new point
    // piled up at the bottom.
    const HardpointTable reopened = applyHardpointEdits(baseline, *read);
    QCOMPARE(int(reopened.size()), int(current.size()));
    for (std::size_t i = 0; i < current.points.size(); ++i)
        QCOMPARE(reopened.points[i].name, current.points[i].name);
}

void TestProject::anOlderEditsFileStillAppendsItsPoints()
{
    // Written before positions were kept: no "after" on the added point.
    QTemporaryDir directory;
    const QString path = directory.filePath(QStringLiteral("edits.json"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(R"({"format": "suspkin-hardpoint-edits", "formatVersion": 1, "changed": [],
                   "added": [{"name": "F_LCA_O_R", "coord": [1, -2, 3]}]})");
    file.close();

    QString error;
    const std::optional<HardpointEdits> read = readHardpointEdits(path, &error);
    QVERIFY2(read.has_value(), qPrintable(error));
    QVERIFY(read->addedAfter.isEmpty());

    const HardpointTable reopened = applyHardpointEdits(sample(), *read);
    QCOMPARE(reopened.points.back().name, QStringLiteral("F_LCA_O_R"));
}

void TestProject::aSelectionOfOneFromAnOlderProjectIsStillASelection()
{
    QTemporaryDir directory;
    QString error;
    std::optional<Project> project =
        Project::create(directory.filePath(QStringLiteral("Old")), QStringLiteral("Old"), &error);
    QVERIFY2(project.has_value(), qPrintable(error));
    ViewState view;
    view.selectedHardpoint = 3;
    project->setView(view);
    QVERIFY(project->save(&error));

    // What a manifest from before multi-selection held: "selected" and nothing
    // else.
    QFile manifest(project->manifestPath());
    QVERIFY(manifest.open(QIODevice::ReadOnly));
    QJsonObject root = QJsonDocument::fromJson(manifest.readAll()).object();
    manifest.close();
    QJsonObject viewObject = root.value(QStringLiteral("view")).toObject();
    viewObject.remove(QStringLiteral("selection"));
    root.insert(QStringLiteral("view"), viewObject);
    QVERIFY(manifest.open(QIODevice::WriteOnly | QIODevice::Truncate));
    manifest.write(QJsonDocument(root).toJson());
    manifest.close();

    const std::optional<Project> reopened = Project::open(project->manifestPath(), &error);
    QVERIFY2(reopened.has_value(), qPrintable(error));
    QCOMPARE(reopened->view().selectedHardpoint, 3);
    QCOMPARE(reopened->view().selection, QList<int>{ 3 });
}

void TestProject::aProjectCanMakeAWorkbookOfItsOwn()
{
    QTemporaryDir directory;
    QString error;
    std::optional<Project> project =
        Project::create(directory.filePath(QStringLiteral("New")), QStringLiteral("New"), &error);
    QVERIFY2(project.has_value(), qPrintable(error));

    // Nothing to write is nothing that could be read back, so it is refused.
    QVERIFY(!project->createHardpointWorkbook(HardpointTable{}, &error).has_value());
    QVERIFY(!error.isEmpty());

    const std::optional<AssetRef> made = project->createHardpointWorkbook(sample(), &error);
    QVERIFY2(made.has_value(), qPrintable(error));
    QCOMPARE(made->relativePath, QStringLiteral("hardpoints/hardpoints.xlsx"));
    // It did not come from anywhere, so it says so.
    QVERIFY(made->originalPath.isEmpty());
    QVERIFY(QFile::exists(project->absolutePath(made->relativePath)));

    // A file already there is somebody's, and is not written over.
    const std::optional<AssetRef> second = project->createHardpointWorkbook(sample(), &error);
    QVERIFY2(second.has_value(), qPrintable(error));
    QCOMPARE(second->relativePath, QStringLiteral("hardpoints/hardpoints-2.xlsx"));
}

void TestProject::everySweepsTravelIsRememberedNotJustTheOneBeingSwept()
{
    QTemporaryDir directory;
    QString error;
    std::optional<Project> project =
        Project::create(directory.filePath(QStringLiteral("S")), QStringLiteral("S"), &error);
    QVERIFY2(project.has_value(), qPrintable(error));

    ViewState view;
    SimulationState& simulation = view.simulation;
    simulation.active = true;
    simulation.axle = QStringLiteral("F");
    simulation.kind = SweepKind::Roll;
    simulation.sweep.bumpTravel = 33.0;
    simulation.sweep.reboundTravel = 17.0;
    simulation.sweep.bumpIncrement = 1.0;
    simulation.sweep.rollAngle = 1.2;
    simulation.sweep.rollIncrement = 0.25;
    simulation.sweep.steerTravel = 35.0;
    simulation.sweep.steerIncrement = 2.0;
    simulation.sweep.rackTravel = 4.0;
    simulation.position = 0.75;
    simulation.measures = { QStringLiteral("toe"), QStringLiteral("rollCentreHeight") };
    simulation.sides = SweepSides::Right;
    simulation.animating = true;
    simulation.animationSeconds = 2.5;
    simulation.allAxles = false;
    simulation.parametersOpen = true;
    project->setView(view);
    QVERIFY2(project->save(&error), qPrintable(error));

    const std::optional<Project> reopened = Project::open(project->manifestPath(), &error);
    QVERIFY2(reopened.has_value(), qPrintable(error));
    const SimulationState& back = reopened->view().simulation;

    QCOMPARE(back.active, true);
    QCOMPARE(back.axle, QStringLiteral("F"));
    QCOMPARE(back.kind, SweepKind::Roll);
    QCOMPARE(back.position, 0.75);
    // Every plot, in the order they were on screen.
    QCOMPARE(back.measures,
             QStringList({ QStringLiteral("toe"), QStringLiteral("rollCentreHeight") }));
    QCOMPARE(back.sides, SweepSides::Right);
    QCOMPARE(back.animating, true);
    QCOMPARE(back.animationSeconds, 2.5);
    QCOMPARE(back.allAxles, false);
    QCOMPARE(back.parametersOpen, true);

    // The bump travel survives a project that was left in roll. Keeping only
    // the swept range is what used to make switching back read millimetres of
    // wheel travel off a roll angle.
    QCOMPARE(back.sweep.bumpTravel, 33.0);
    QCOMPARE(back.sweep.reboundTravel, 17.0);
    QCOMPARE(back.sweep.bumpIncrement, 1.0);
    QCOMPARE(back.sweep.rollAngle, 1.2);
    QCOMPARE(back.sweep.rollIncrement, 0.25);
    QCOMPARE(back.sweep.steerTravel, 35.0);
    QCOMPARE(back.sweep.steerIncrement, 2.0);
    QCOMPARE(back.sweep.rackTravel, 4.0);
    QVERIFY(back.sweep == simulation.sweep);
}

void TestProject::anOlderProjectsSingleRangeBecomesThatKindsOwnTravel()
{
    // What a project written before the three sweeps had their own travel
    // holds: one range, in whichever unit it was left on.
    QTemporaryDir directory;
    QString error;
    std::optional<Project> project =
        Project::create(directory.filePath(QStringLiteral("Old")), QStringLiteral("Old"), &error);
    QVERIFY2(project.has_value(), qPrintable(error));
    QVERIFY2(project->save(&error), qPrintable(error));

    QFile manifest(project->manifestPath());
    QVERIFY(manifest.open(QIODevice::ReadOnly));
    QJsonObject root = QJsonDocument::fromJson(manifest.readAll()).object();
    manifest.close();

    QJsonObject simulation;
    simulation.insert(QStringLiteral("active"), true);
    simulation.insert(QStringLiteral("kind"), QStringLiteral("bump"));
    simulation.insert(QStringLiteral("from"), -17.0);
    simulation.insert(QStringLiteral("to"), 33.0);
    simulation.insert(QStringLiteral("steps"), 51);
    simulation.insert(QStringLiteral("rack"), 3.0);
    // And one curve, from before there could be more than one plot.
    simulation.insert(QStringLiteral("measure"), QStringLiteral("camber"));
    QJsonObject view = root.value(QStringLiteral("view")).toObject();
    view.insert(QStringLiteral("simulation"), simulation);
    root.insert(QStringLiteral("view"), view);
    QVERIFY(manifest.open(QIODevice::WriteOnly | QIODevice::Truncate));
    manifest.write(QJsonDocument(root).toJson());
    manifest.close();

    const std::optional<Project> reopened = Project::open(project->manifestPath(), &error);
    QVERIFY2(reopened.has_value(), qPrintable(error));
    const SimulationState& back = reopened->view().simulation;

    // The range it was left with comes back as the travel it stood for, so an
    // older project opens where its owner left it rather than at a default.
    QCOMPARE(back.kind, SweepKind::Bump);
    QCOMPARE(back.sweep.reboundTravel, 17.0);
    QCOMPARE(back.sweep.bumpTravel, 33.0);
    QCOMPARE(back.sweep.bumpIncrement, 1.0);
    QCOMPARE(back.sweep.rackTravel, 3.0);
    QCOMPARE(back.sweep.specFor(SweepKind::Bump).steps, 51);

    // The two it says nothing about keep their defaults, in their own units,
    // rather than inheriting fifty millimetres of wheel travel.
    QCOMPARE(back.sweep.rollAngle, SweepSettings{}.rollAngle);
    QCOMPARE(back.sweep.steerTravel, SweepSettings{}.steerTravel);

    // The one curve it was showing is a list of one, and both wheels are drawn,
    // which is all an older build could do.
    QCOMPARE(back.measures, QStringList{ QStringLiteral("camber") });
    QCOMPARE(back.sides, SweepSides::Both);
    // Nor did it state any static angles: every axle reads its own off the
    // hardpoints, the way it always has.
    QVERIFY(reopened->alignment().isEmpty());
    QVERIFY(!reopened->alignmentFor(QStringLiteral("F")).has_value());
}

void TestProject::staticAnglesAreKeptPerAxle()
{
    QTemporaryDir directory;
    QString error;
    std::optional<Project> project =
        Project::create(directory.filePath(QStringLiteral("A")), QStringLiteral("A"), &error);
    QVERIFY2(project.has_value(), qPrintable(error));

    // The front stated, the rear not: an axle that is not in here reads its
    // angles off its hardpoints, and that has to survive a reload as well.
    QHash<QString, StaticAlignment> alignment;
    alignment.insert(QStringLiteral("F"), StaticAlignment{ -1.5, 0.125 });
    project->setAlignment(alignment);
    QVERIFY2(project->save(&error), qPrintable(error));

    std::optional<Project> reopened = Project::open(project->manifestPath(), &error);
    QVERIFY2(reopened.has_value(), qPrintable(error));
    QCOMPARE(reopened->alignment().size(), 1);
    const std::optional<StaticAlignment> front = reopened->alignmentFor(QStringLiteral("F"));
    QVERIFY(front.has_value());
    // Doubles, exactly: a setting typed as -1.5 comes back as -1.5.
    QCOMPARE(front->camber, -1.5);
    QCOMPARE(front->toe, 0.125);
    QVERIFY(!reopened->alignmentFor(QStringLiteral("R")).has_value());

    // An entry missing one of its two numbers is not half an answer, so it is
    // left out rather than read as a zero somebody never typed.
    QFile manifest(project->manifestPath());
    QVERIFY(manifest.open(QIODevice::ReadOnly));
    QJsonObject root = QJsonDocument::fromJson(manifest.readAll()).object();
    manifest.close();
    QJsonObject stated = root.value(QStringLiteral("alignment")).toObject();
    QJsonObject half;
    half.insert(QStringLiteral("camber"), -2.0);
    stated.insert(QStringLiteral("R"), half);
    root.insert(QStringLiteral("alignment"), stated);
    QVERIFY(manifest.open(QIODevice::WriteOnly | QIODevice::Truncate));
    manifest.write(QJsonDocument(root).toJson());
    manifest.close();

    reopened = Project::open(project->manifestPath(), &error);
    QVERIFY2(reopened.has_value(), qPrintable(error));
    QVERIFY(reopened->alignmentFor(QStringLiteral("F")).has_value());
    QVERIFY(!reopened->alignmentFor(QStringLiteral("R")).has_value());

    // Handing every axle back to its hardpoints leaves nothing behind.
    reopened->setAlignment({});
    QVERIFY2(reopened->save(&error), qPrintable(error));
    QVERIFY(manifest.open(QIODevice::ReadOnly));
    QVERIFY(!QJsonDocument::fromJson(manifest.readAll()).object().contains(
        QStringLiteral("alignment")));
    manifest.close();
}

QTEST_APPLESS_MAIN(TestProject)
#include "test_project.moc"
