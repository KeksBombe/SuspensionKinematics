#include "app/session/ProjectSession.h"
#include "model/HardpointMirror.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace suspkin;

namespace {

QString dataPath(const char* name)
{
    return QStringLiteral(SUSPKIN_TEST_DATA_DIR) + QLatin1Char('/') + QLatin1String(name);
}

/// An empty project in @p directory.
Project makeProject(const QTemporaryDir& directory)
{
    QString error;
    std::optional<Project> project =
        Project::create(directory.filePath(QStringLiteral("car")), QStringLiteral("Car"), &error);
    if (!project) qFatal("Cannot create a project: %s", qPrintable(error));
    return std::move(*project);
}

/// The fixture workbook imported into @p session and shown, the way importing
/// hardpoints leaves a project.
bool importFixture(ProjectSession& session)
{
    SessionMessage problem;
    HardpointDocument& document = session.hardpoints();
    if (!document.import(dataPath("hardpoints.xlsx"), &problem)) return false;
    document.model().setTable(document.baseline());
    return true;
}

Hardpoint make(const char* name, double x, double y, double z)
{
    Hardpoint point;
    point.name = QLatin1String(name);
    point.coord[0] = x;
    point.coord[1] = y;
    point.coord[2] = z;
    return point;
}

/// Both sides of a front axle, enough for it to bind and be posed. The same
/// corner test_simulation solves.
HardpointTable frontAxle()
{
    HardpointTable corner;
    corner.points = {
        make("F_LCA_IF", 100.0, 200.0, 120.0),    make("F_LCA_IR", -100.0, 200.0, 120.0),
        make("F_LCA_O", 0.0, 560.0, 110.0),       make("F_UCA_IF", 80.0, 230.0, 280.0),
        make("F_UCA_IR", -80.0, 230.0, 280.0),    make("F_UCA_O", 0.0, 540.0, 300.0),
        make("F_TieRod_I", -120.0, 220.0, 150.0), make("F_TieRod_O", -120.0, 555.0, 145.0),
        make("F_WheelCenter", 0.0, 600.0, 220.0), make("F_ContactPatch", 0.0, 600.0, 0.0),
    };
    return mirrorHardpoints(corner, {}, MirrorSpec{}).table;
}

LinkageTemplate frontAxleTemplate()
{
    LinkageTemplate templ;
    MechanismTemplate& mechanism = templ.mechanism;
    mechanism.lowerFront = QStringLiteral("{corner}_LCA_IF");
    mechanism.lowerRear = QStringLiteral("{corner}_LCA_IR");
    mechanism.lowerOuter = QStringLiteral("{corner}_LCA_O");
    mechanism.upperFront = QStringLiteral("{corner}_UCA_IF");
    mechanism.upperRear = QStringLiteral("{corner}_UCA_IR");
    mechanism.upperOuter = QStringLiteral("{corner}_UCA_O");
    mechanism.tieRodInboard = QStringLiteral("{corner}_TieRod_I");
    mechanism.tieRodOutboard = QStringLiteral("{corner}_TieRod_O");
    mechanism.wheelCenter = QStringLiteral("{corner}_WheelCenter");
    mechanism.contactPatch = QStringLiteral("{corner}_ContactPatch");

    CornerSpec front;
    front.token = QStringLiteral("F");
    front.steeringRack = QStringLiteral("{corner}_TieRod_I");
    front.steeringStated = true;
    templ.corners = { front };
    return templ;
}

SimulationRequest bumpAt(double position)
{
    SimulationRequest request;
    request.axle = QStringLiteral("F");
    request.sweptAxles = { QStringLiteral("F") };
    request.position = position;
    request.simulating = true;
    return request;
}

} // namespace

/// The session layer, which is where the rules CLAUDE.md calls easiest to break
/// now live: the baseline, the workbook and the edits between them, the
/// template read back after every patch, and the order the cascade announces
/// what it resolved. None of it needs a window.
class TestSession : public QObject {
    Q_OBJECT

private slots:
    void savingIsInertWhileLoading();

    void overwritingTwiceDoesNotAppendMirroredRowsAgain();
    void aRenameIsARemovalAndAnAddition();
    void aRenamedPointKeepsItsConfigurationThroughAResolve();
    void aNewWorkbookIsWrittenThenReadBack();

    void aProjectWithNoTemplateIsGivenTheBuiltinOne();
    void aTemplateWithoutAMechanismIsReadWithTheBuiltinOne();

    void theSweepIsSkippedWhenNotWantedButThePoseIsNot();
    void theCascadeAnnouncesTheAxlesBeforeTheSweep();
};

void TestSession::savingIsInertWhileLoading()
{
    QTemporaryDir directory;
    ProjectSession session(makeProject(directory));

    session.setLoading(true);
    session.markDirty();
    QVERIFY(!session.savePending());

    session.setLoading(false);
    session.markDirty();
    QVERIFY(session.savePending());

    QString error;
    QVERIFY2(session.save(&error), qPrintable(error));
    QVERIFY(!session.savePending());
}

void TestSession::overwritingTwiceDoesNotAppendMirroredRowsAgain()
{
    QTemporaryDir directory;
    ProjectSession session(makeProject(directory));
    QVERIFY(importFixture(session));
    HardpointDocument& document = session.hardpoints();
    HardpointModel& model = document.model();
    const int imported = model.rowCount();

    model.setTable(mirrorHardpoints(model.table(), {}, MirrorSpec{}).table);
    document.captureMirrorProvenance();
    const int mirrored = model.rowCount();
    QVERIFY(mirrored > imported);
    QVERIFY(!document.pendingEdits().isEmpty());

    SessionMessage problem;
    QVERIFY2(document.overwriteWorkbook(&problem), qPrintable(problem.text));
    // What the window does next: the table becomes the baseline it re-read.
    model.setTable(document.baseline());
    QVERIFY(document.pendingEdits().isEmpty());
    QCOMPARE(int(document.baseline().size()), mirrored);

    // The mirrored rows are ordinary rows in the workbook now, and a second
    // overwrite patches them rather than appending them again.
    QVERIFY2(document.overwriteWorkbook(&problem), qPrintable(problem.text));
    const HardpointLoadResult reread = readHardpointsXlsx(document.workbookPath());
    QVERIFY2(reread.ok(), qPrintable(reread.error));
    QCOMPARE(int(reread.table->size()), mirrored);

    // Only the project remembers which of them are mirrors.
    int stamped = 0;
    for (const Hardpoint& point : document.baseline().points)
        if (point.isMirrored()) ++stamped;
    QCOMPARE(stamped, mirrored - imported);
}

void TestSession::aRenameIsARemovalAndAnAddition()
{
    QTemporaryDir directory;
    ProjectSession session(makeProject(directory));
    QVERIFY(importFixture(session));
    HardpointDocument& document = session.hardpoints();

    const QString from = document.baseline().points.front().name;
    const QString to = QStringLiteral("RENAMED");
    QVERIFY(document.model().renamePoint(0, to));

    const HardpointEdits pending = document.pendingEdits();
    QCOMPARE(pending.removed, QStringList{ from });
    QCOMPARE(int(pending.added.size()), 1);
    QCOMPARE(pending.added.front().name, to);
    QVERIFY(pending.changed.empty());
}

void TestSession::aRenamedPointKeepsItsConfigurationThroughAResolve()
{
    QTemporaryDir directory;
    ProjectSession session(makeProject(directory));
    QVERIFY(importFixture(session));
    HardpointDocument& document = session.hardpoints();
    HardpointModel& model = document.model();

    const QString from = model.table().points.front().name;
    HardpointConfig config;
    config.part1 = QStringLiteral("Upright");
    model.setConfig({ { from, config } });
    document.captureConfig();

    const QString to = QStringLiteral("RENAMED");
    QVERIFY(model.renamePoint(0, to));
    // Into the project before anything is resolved again. Resolving refills
    // the model from the project's copy, which would otherwise still have the
    // point under its old name and quietly take the rename back.
    document.captureConfig();
    session.resolveFromTable();

    QVERIFY(model.config().contains(to));
    QCOMPARE(model.config().value(to), config);
    QVERIFY(!model.config().contains(from));
}

void TestSession::aNewWorkbookIsWrittenThenReadBack()
{
    QTemporaryDir directory;
    ProjectSession session(makeProject(directory));
    HardpointDocument& document = session.hardpoints();
    QVERIFY(session.project().hardpoints().isEmpty());

    HardpointTable table;
    table.points.push_back(make("P1", 1.5, -2.25, 300.0));
    SessionMessage problem;
    QVERIFY2(document.adoptNewWorkbook(table, &problem), qPrintable(problem.text));

    // Written into the project, and what came back from reading it is the
    // baseline -- with the cell map that lets the next overwrite patch the row.
    QVERIFY(!session.project().hardpoints().isEmpty());
    QVERIFY(QFileInfo::exists(document.workbookPath()));
    QVERIFY(document.workbookWritable());
    QCOMPARE(int(document.baseline().size()), 1);
    const Hardpoint& point = document.baseline().points.front();
    QCOMPARE(point.name, QStringLiteral("P1"));
    QCOMPARE(point.coord[0], 1.5);
    QCOMPARE(point.coord[1], -2.25);
    QCOMPARE(point.coord[2], 300.0);

    document.model().setTable(document.baseline());
    QVERIFY(document.pendingEdits().isEmpty());
}

void TestSession::aProjectWithNoTemplateIsGivenTheBuiltinOne()
{
    QTemporaryDir directory;
    ProjectSession session(makeProject(directory));
    QVERIFY(session.project().linkageTemplate().isEmpty());

    SessionMessage problem;
    QVERIFY2(session.linkage().load(&problem), qPrintable(problem.text));
    // An ordinary project file from here on, which the user can open and edit.
    QVERIFY(!session.project().linkageTemplate().isEmpty());
    QVERIFY(QFileInfo::exists(
        session.project().absolutePath(session.project().linkageTemplate().relativePath)));
    QVERIFY(session.linkage().linkageTemplate().canSimulate());
}

void TestSession::aTemplateWithoutAMechanismIsReadWithTheBuiltinOne()
{
    QTemporaryDir directory;
    ProjectSession session(makeProject(directory));
    Project& project = session.project();

    // The built-in template as a project written before the solver had it: no
    // mechanism block.
    QJsonObject root = QJsonDocument::fromJson(builtinLinkageTemplateBytes()).object();
    QVERIFY(root.contains(QStringLiteral("mechanism")));
    root.remove(QStringLiteral("mechanism"));
    QString error;
    QVERIFY2(project.writeFile(linkageTemplateRelativePath(), QJsonDocument(root).toJson(),
                               &error),
             qPrintable(error));
    AssetRef asset;
    asset.relativePath = linkageTemplateRelativePath();
    project.setLinkageTemplate(asset);

    SessionMessage problem;
    QVERIFY2(session.linkage().load(&problem), qPrintable(problem.text));
    const LinkageTemplate& templ = session.linkage().linkageTemplate();
    QVERIFY(templ.mechanismAssumed);
    QVERIFY(templ.canSimulate());
    QCOMPARE(templ.mechanism.lowerOuter, builtinLinkageTemplate().mechanism.lowerOuter);
}

void TestSession::theSweepIsSkippedWhenNotWantedButThePoseIsNot()
{
    SimulationRunner runner;
    runner.bind(frontAxleTemplate(), frontAxle(), MirrorSpec{}, {}, QString());
    QVERIFY(!runner.simulation().isEmpty());

    SimulationRequest request = bumpAt(10.0);
    request.sweepWanted = false;
    runner.runSweep(request);
    runner.pose(request);
    QVERIFY(runner.sweeps().empty());
    QVERIFY(!runner.currentPose().isEmpty());

    request.sweepWanted = true;
    runner.runSweep(request);
    QCOMPARE(runner.sweeps().size(), std::size_t(1));
    QVERIFY(!runner.sweeps().front().isEmpty());

    // Not simulating is the table's own coordinates, which is no pose at all.
    request.simulating = false;
    runner.pose(request);
    QVERIFY(runner.currentPose().isEmpty());
}

void TestSession::theCascadeAnnouncesTheAxlesBeforeTheSweep()
{
    QTemporaryDir directory;
    ProjectSession session(makeProject(directory));
    session.hardpoints().model().setTable(frontAxle());
    SessionMessage problem;
    QVERIFY2(session.linkage().load(&problem), qPrintable(problem.text));
    session.setRequestSource([] { return bumpAt(5.0); });

    QStringList steps;
    const auto note = [&steps](const char* step) {
        return [&steps, step] { steps << QLatin1String(step); };
    };
    connect(&session, &ProjectSession::partsResolved, this, note("parts"));
    connect(&session, &ProjectSession::axlesBound, this, note("axles"));
    connect(&session, &ProjectSession::sweepRun, this, note("sweep"));
    connect(&session, &ProjectSession::posed, this, note("pose"));
    connect(&session, &ProjectSession::wheelsPlaced, this, note("wheels"));
    connect(&session, &ProjectSession::resolved, this, note("resolved"));

    session.resolveFromTable();

    // The panel's axle box follows the bound axles before the sweep asks it
    // which axle is chosen, and the parts come before anything is posed on
    // them.
    QCOMPARE(steps, QStringList({ QStringLiteral("parts"), QStringLiteral("axles"),
                                  QStringLiteral("sweep"), QStringLiteral("pose"),
                                  QStringLiteral("wheels"), QStringLiteral("resolved") }));
    QCOMPARE(session.simulation().sweeps().size(), std::size_t(1));
    QVERIFY(!session.simulation().currentPose().isEmpty());
}

QTEST_MAIN(TestSession)
#include "test_session.moc"
