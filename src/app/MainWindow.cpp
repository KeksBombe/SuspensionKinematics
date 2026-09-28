#include "app/MainWindow.h"

#include "app/AnalysisPanel.h"
#include "app/HardpointModel.h"
#include "app/HardpointPanel.h"
#include "app/PointEditController.h"
#include "app/RecentProjects.h"
#include "app/Ribbon.h"
#include "app/features/SharedCommands.h"
#include "app/framework/FeatureRegistry.h"
#include "app/framework/WindowChrome.h"

#include <QApplication>
#include <QCloseEvent>
#include <QDockWidget>
#include <QFileInfo>
#include <QImage>
#include <QLabel>
#include <QMatrix4x4>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QStatusBar>

#include <utility>

namespace suspkin {

MainWindow::MainWindow(Project project, QWidget* parent)
    : QMainWindow(parent), m_session(new ProjectSession(std::move(project), this))
{
    m_viewport = new ViewportWidget(this);
    setCentralWidget(m_viewport);

    m_meshLabel = new QLabel(tr("No chassis imported"), this);
    m_hardpointLabel = new QLabel(this);
    m_glLabel = new QLabel(this);
    statusBar()->addWidget(m_meshLabel, 1);
    statusBar()->addPermanentWidget(m_hardpointLabel);
    statusBar()->addPermanentWidget(m_glLabel);

    connect(m_viewport, &ViewportWidget::contextReady, m_glLabel, &QLabel::setText);

    // The docks before the actions: the panel toggles are actions on them.
    buildHardpointDock();
    buildAnalysisDock();
    // After both docks: what the session resolves is drawn on them.
    connectSession();
    // After the dock, because moving a point in the viewport goes through the
    // same model the table edits.
    m_pointEditing = new PointEditController(
        m_viewport, hardpoints(),
        [this](const QString& text, int milliseconds) { showStatus(text, milliseconds); }, this);
    buildCommands();
    buildRibbon();
    finishCommands(this, m_commands);

    connect(m_viewport, &ViewportWidget::viewChanged, this, [this] { markDirty(); });

    // Closing the window is the usual way out, but not the only one: --screenshot
    // and a session manager both end the run without one. A pending debounced
    // save has to survive those too.
    connect(qApp, &QCoreApplication::aboutToQuit, this, [this] {
        if (m_session->savePending()) saveProject();
    });

    resize(1280, 820);
    // Before the project's own layout goes on: this is the layout a project
    // that has never been arranged gets, and what Reset Panel Layout restores.
    m_defaultDockState = saveState();
    openProjectContents();

    // Last, so a feature that reaches for the project or the network finds the
    // window already up and its project loaded.
    for (const std::unique_ptr<Feature>& feature : m_features) feature->windowReady();
}

MainWindow::~MainWindow() = default;

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

void MainWindow::buildHardpointDock()
{
    HardpointModel* model = hardpoints();
    m_hardpointPanel = new HardpointPanel(model, this);

    m_hardpointDock = new QDockWidget(tr("Hardpoints"), this);
    // Named so QMainWindow::saveState() can put it back where the user left it.
    m_hardpointDock->setObjectName(QStringLiteral("hardpointDock"));
    m_hardpointDock->setWidget(m_hardpointPanel);
    m_hardpointDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    addDockWidget(Qt::RightDockWidgetArea, m_hardpointDock);
    m_hardpointDock->hide(); // nothing to show until something is imported

    // Selecting in one view selects in the other. Neither side re-emits what it
    // was just told, so the two cannot chase each other. What can be done to a
    // selection -- delete it, rename it, make a part through it -- follows it.
    connect(m_viewport, &ViewportWidget::hardpointSelectionEdited, this,
            [this](const QList<int>& rows, int current) {
                m_hardpointPanel->setSelectedRows(rows, current);
                updateChrome();
            });
    connect(m_hardpointPanel, &HardpointPanel::selectionChanged, this,
            [this](const QList<int>& rows, int current) {
                m_viewport->setSelectedHardpoints(rows, current);
                updateChrome();
            });

    // A rename changes what everything resolved by name finds: the parts, the
    // solve, the wheels. The model has already moved the point's configuration
    // and its mirrors' provenance; the project is told, and the rest resolved
    // again.
    connect(model, &HardpointModel::pointRenamed, this,
            [this](int row, const QString& from, const QString& to) {
                m_session->wheels().followRenames({ { from, to } });

                // Into the project before anything is resolved again: the
                // configuration table is refilled from the project's copy, and
                // that copy has to have the point under its new name already.
                m_session->hardpoints().captureConfig();
                m_session->hardpoints().captureMirrorProvenance();

                const QList<int> selection = m_viewport->selectedHardpoints();
                syncTableToViewport(false);
                selectPoints(selection.isEmpty() ? QList<int>{ row } : selection, row);
                markDirty();
                m_session->recordEdit(tr("Rename %1 to %2").arg(from, to));
                statusBar()->showMessage(tr("Renamed %1 to %2").arg(from, to), 5000);
            });

    connect(model, &HardpointModel::coordinateChanged, this, [this](int row) {
        const HardpointTable& table = hardpoints()->table();
        if (row < 0 || row >= static_cast<int>(table.points.size())) return;
        m_viewport->moveHardpoint(row, table.points[static_cast<std::size_t>(row)].toVector());
        // A coordinate is a link length, so the mechanism has to be measured
        // again. Posing it again takes the wheels with it.
        m_session->resolveMechanism();
        markDirty();
        // The table's cell, the arrows and the X, Y and Z field all arrive
        // here, so each of them is one step back.
        m_session->recordEdit(tr("Move %1").arg(table.points[static_cast<std::size_t>(row)].name));
    });

    // What a point is for is project state like everything else, so an accepted
    // edit is in the project before this lambda returns. The model has already
    // refused anything that could not mean something.
    connect(model, &HardpointModel::configChanged, this, [this](int row) {
        m_session->hardpoints().captureConfig();
        // A point that has just become -- or stopped being -- a chassis pivot
        // changes what is drawn through it.
        syncGroundedPoints();
        markDirty();
        const HardpointTable& table = hardpoints()->table();
        if (row >= 0 && row < static_cast<int>(table.points.size()))
            m_session->recordEdit(
                tr("Configure %1").arg(table.points[static_cast<std::size_t>(row)].name));
    });
}

// ---------------------------------------------------------------------------
// Undo and redo
// ---------------------------------------------------------------------------

void MainWindow::restoreEditState(const EditState& from, const EditState& to)
{
    // Taken before the rows move under it.
    const QStringList selected = selectedPointNames();

    // The field was typing into a point that may be about to move or go.
    m_pointEditing->dismiss();
    HardpointModel* model = hardpoints();
    // Read off the table as it is on screen, which is what the wheels name.
    const QHash<QString, QString> renamed = renamedPoints(model->table(), to.table);

    model->setTable(to.table);
    model->setConfig(to.config);
    project().setAlignment(to.alignment);
    m_session->wheels().followRenames(renamed);
    // Into the project before anything is resolved again: the configuration is
    // refilled from the project's copy, and a step that renamed or deleted a
    // point would otherwise be taken back by that refill.
    m_session->hardpoints().captureConfig();
    m_session->hardpoints().captureMirrorProvenance();
    syncTableToViewport(false);
    markDirty();

    // What the step touched, so the user sees what came back. A step whose
    // points have all gone again -- an undone Add -- leaves the selection that
    // was there, less what went.
    const QStringList touched = touchedPoints(from, to);
    selectPointsNamed(touched.isEmpty() ? selected : touched);
}

QStringList MainWindow::selectedPointNames() const
{
    const HardpointTable& table = m_session->hardpoints().model().table();
    QStringList names;
    for (const int row : m_viewport->selectedHardpoints())
        if (row >= 0 && row < static_cast<int>(table.size()))
            names << table.points[static_cast<std::size_t>(row)].name;
    return names;
}

void MainWindow::selectPointsNamed(const QStringList& names)
{
    const HardpointTable& table = hardpoints()->table();
    QList<int> rows;
    for (const QString& name : names) {
        const int row = table.indexOf(name);
        if (row >= 0) rows << row;
    }
    selectPoints(rows, rows.isEmpty() ? -1 : rows.front());
}

void MainWindow::buildAnalysisDock()
{
    m_analysisPanel = new AnalysisPanel(this);

    m_analysisDock = new QDockWidget(tr("Analysis"), this);
    // Named so QMainWindow::saveState() can put it back where the user left it.
    m_analysisDock->setObjectName(QStringLiteral("analysisDock"));
    m_analysisDock->setWidget(m_analysisPanel);
    m_analysisDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea
                                    | Qt::BottomDockWidgetArea);
    addDockWidget(Qt::RightDockWidgetArea, m_analysisDock);
    m_analysisDock->hide(); // nothing to solve until hardpoints are imported

    // Which axle, and how far it is being asked to move, are both things the
    // project remembers, so every one of these ends in markDirty().
    connect(m_analysisPanel, &AnalysisPanel::axleChanged, this, [this] {
        m_session->resolveSimulation();
        markDirty();
    });
    connect(m_analysisPanel, &AnalysisPanel::specChanged, this, [this] {
        m_session->resolveSimulation();
        markDirty();
    });
    connect(m_analysisPanel, &AnalysisPanel::positionChanged, this, [this] {
        m_session->resolvePose();
        // A position that is moving thirty times a second is not worth writing
        // out thirty times a second. Stopping is what saves where it stopped.
        if (!m_analysisPanel->animating()) markDirty();
    });
    connect(m_analysisPanel, &AnalysisPanel::animatingChanged, this, [this] { markDirty(); });
    connect(m_analysisPanel, &AnalysisPanel::simulatingChanged, this, [this] {
        m_session->resolvePose();
        markDirty();
    });
    connect(m_analysisPanel, &AnalysisPanel::measureChanged, this, [this] { markDirty(); });
    connect(m_analysisPanel, &AnalysisPanel::plotAxlesChanged, this, [this] {
        m_session->runSweep();
        markDirty();
    });
    connect(m_analysisPanel, &AnalysisPanel::sidesChanged, this, [this] { markDirty(); });
    // How fast the animation runs and whether the parameters window is open
    // change nothing about the curve, but they are still the user's arrangement
    // and the project keeps them.
    connect(m_analysisPanel, &AnalysisPanel::playbackChanged, this, [this] { markDirty(); });

    // The sweep is skipped while the dock is shut, so opening it is what asks
    // for one. Reopening a project restores the dock, and this catches that too.
    connect(m_analysisDock, &QDockWidget::visibilityChanged, this, [this](bool visible) {
        if (visible && m_session->simulation().sweeps().empty()) m_session->runSweep();
    });
}

void MainWindow::connectSession()
{
    connect(m_session, &ProjectSession::saveDue, this, [this] { saveProject(); });
    m_session->setRequestSource([this] { return simulationRequest(); });

    // setHardpoints() drops the parts, because they are indices into the table,
    // so the points always go in first and the parts after them.
    connect(m_session, &ProjectSession::partsResolved, this,
            [this] { m_viewport->setLinkage(m_session->linkage().parts()); });
    connect(m_session, &ProjectSession::axlesBound, this, &MainWindow::showAxles);
    connect(m_session, &ProjectSession::sweepRun, this, &MainWindow::showSweep);
    connect(m_session, &ProjectSession::posed, this, &MainWindow::showPose);
    connect(m_session, &ProjectSession::wheelsPlaced, this, [this] {
        m_viewport->setWheelPlacements(m_session->wheels().placements(),
                                       project().wheels().spec.alignToCenter);
        updateChrome(); // which refreshes the status line too
    });
    // The status line shows the part count, and Undo and Redo say what they
    // would do: both follow.
    connect(m_session, &ProjectSession::resolved, this, &MainWindow::updateChrome);
    // The configuration is refilled after the parts are resolved, and it is
    // what says which of their segments run from the car to itself.
    connect(m_session, &ProjectSession::resolved, this, &MainWindow::syncGroundedPoints);
    connect(m_session, &ProjectSession::historyChanged, this, &MainWindow::updateChrome);
}

SimulationRequest MainWindow::simulationRequest() const
{
    SimulationRequest request;
    request.axle = m_analysisPanel->axle();
    request.sweptAxles = m_analysisPanel->sweptAxles();
    request.spec = m_analysisPanel->spec();
    request.position = m_analysisPanel->position();
    request.moveAllAxles = m_analysisPanel->movesAllAxles();
    request.simulating = m_analysisPanel->simulating();
    // A sweep is the most expensive thing this window does, and there is nothing
    // to draw a curve on while the dock is shut. It is run again when it opens.
    request.sweepWanted = m_analysisDock->isVisible();
    return request;
}

void MainWindow::showAxles()
{
    // What the panel puts in its axle box. The label is a display matter, so
    // the fallback for an axle the template did not name is chosen here rather
    // than in the core.
    const Simulation& simulation = m_session->simulation().simulation();
    QList<AxleEntry> entries;
    entries.reserve(static_cast<int>(simulation.axles().size()));
    for (const AxleSolver& axle : simulation.axles()) {
        entries.append(AxleEntry{ axle.cornerToken(),
                                  axle.label().isEmpty() ? tr("Suspension") : axle.label(),
                                  axle.isSteered() });
    }
    m_analysisPanel->setAxles(entries);
}

void MainWindow::showSweep()
{
    const SimulationRunner& runner = m_session->simulation();
    m_analysisPanel->setResults(runner.sweeps());
    m_analysisPanel->setStatus(runner.status().join(QStringLiteral("\n")));
}

void MainWindow::syncGroundedPoints()
{
    m_viewport->setGroundedPoints(groundedPoints(hardpoints()->table(), hardpoints()->config()));
}

void MainWindow::showPose()
{
    const SimulationPose& pose = m_session->simulation().currentPose();
    if (pose.isEmpty())
        m_analysisPanel->clearReadout();
    else
        m_analysisPanel->setReadout(pose.samples.front(), m_analysisPanel->spec().kind);

    // Posed markers are not where the table has them, so the arrows come off:
    // a drag would be writing a design coordinate read off a simulated one.
    m_viewport->setPointEditingEnabled(pose.isEmpty());
    if (!pose.isEmpty()) m_pointEditing->dismiss();

    m_viewport->setMeshTransform(pose.bodyMotion ? pose.bodyMotion->toMatrix() : QMatrix4x4());

    // The table itself never moves. What the viewport is given is a copy of it
    // with the solved positions laid over the points the mechanism owns, so
    // nothing here can reach the edits file or a workbook.
    const HardpointTable table = pose.layOver(hardpoints()->table());
    std::vector<QVector3D> positions;
    positions.reserve(table.points.size());
    for (const Hardpoint& point : table.points) positions.push_back(point.toVector());
    m_viewport->setHardpointPositions(positions);
}

void MainWindow::buildCommands()
{
    // The window does not know what the commands are. Each feature registers
    // its own, says where on the ribbon it goes and when it can be used, and
    // the ribbon is built from what they asked for -- so adding a command, or
    // a whole area of the application, touches no file that already exists.
    m_features = FeatureRegistry::createAll(*this);
    for (const std::unique_ptr<Feature>& feature : m_features)
        feature->registerCommands(m_commands);
}

void MainWindow::buildRibbon()
{
    QMenu* fileMenu = buildFileMenu(this, m_commands, project().manifestPath(),
                                    [this](const QString& manifestPath) {
                                        saveProject();
                                        requestProject(manifestPath);
                                    });
    m_ribbon = suspkin::buildRibbon(this, fileMenu, m_commands);

    // After the pages are in, so building them -- the first tab becoming
    // current -- is not taken for the user choosing it. What the chevron says
    // is PanelsFeature's, which owns it.
    connect(m_ribbon, &Ribbon::currentPageChanged, this, [this] { markDirty(); });
    connect(m_ribbon, &Ribbon::collapsedChanged, this, [this] { markDirty(); });
}

// ---------------------------------------------------------------------------
// Opening the project
// ---------------------------------------------------------------------------

void MainWindow::openProjectContents()
{
    m_session->setLoading(true);
    const WindowState& saved = project().window();

    if (!saved.geometry.isEmpty()) restoreGeometry(saved.geometry);
    if (!saved.dockState.isEmpty()) restoreState(saved.dockState);
    // The ribbon is window layout too. A page the project names that this
    // build does not have -- a tab renamed since -- is the first page, and an
    // older project that names none opens there, expanded, with its menu bar.
    m_ribbon->setCurrentPage(saved.ribbonPage);
    m_ribbon->setCollapsed(saved.ribbonCollapsed);

    const bool hasGeometry = openChassis();
    const bool hasHardpoints = openHardpoints();
    // After the points, because it is resolved against them, and unconditional
    // because a project should come back with its own template even when the
    // workbook it belongs to has gone missing.
    openLinkageTemplate();
    // Also after the points: the wheels are pinned to four of them.
    const bool hasWheels = openWheels();

    applyViewState();
    // Nothing to restore a view onto, or nothing was saved: frame whatever the
    // project turned out to hold.
    if ((hasGeometry || hasHardpoints || hasWheels) && !project().view().cameraValid)
        m_viewport->fitToView();

    // restoreState() has already put the dock back where the user left it,
    // including closed -- so the only cases left are the two it cannot know
    // about: no hardpoints to show, and a project that has never been laid out.
    if (!hasHardpoints)
        m_hardpointDock->hide();
    else if (saved.dockState.isEmpty())
        m_hardpointDock->show();

    // After everything that fills the configuration in, so the state nothing
    // undoes past is the project as it opened -- not half of it. Restarting it
    // puts the status line and the title in step too.
    m_session->restartHistory();

    m_session->setLoading(false);
    RecentProjects::remember(project().manifestPath(), project().name());
}

bool MainWindow::openChassis()
{
    SessionMessage problem;
    std::optional<ChassisModel> model = m_session->chassis().open(&problem);
    showProblem(problem);
    if (!model) return false;

    m_viewport->setMesh(std::move(model->mesh), model->edges);
    updateChrome();
    return true;
}

bool MainWindow::openHardpoints()
{
    SessionMessage problem;
    std::optional<HardpointTable> table = m_session->hardpoints().open(&problem);
    showProblem(problem);
    if (!table) return false;

    setHardpointTable(std::move(*table), false);
    return true;
}

bool MainWindow::openLinkageTemplate()
{
    SessionMessage problem;
    const bool loaded = m_session->linkage().load(&problem);
    showProblem(problem);
    return loaded;
}

bool MainWindow::openWheels()
{
    SessionMessage problem;
    std::optional<WheelModels> models = m_session->wheels().open(&problem);
    showProblem(problem);
    if (!models) {
        m_viewport->clearWheels();
        return false;
    }

    m_viewport->setWheelModels(std::move(models->tyre), std::move(models->tyreEdges),
                               std::move(models->rim), std::move(models->rimEdges));
    m_session->placeWheels();
    return !m_session->wheels().placements().empty();
}

void MainWindow::applyViewState()
{
    // A copy: restoring can resolve the project again, and nothing it sets off
    // may leave this dangling.
    const ViewState view = project().view();
    for (const std::unique_ptr<Feature>& feature : m_features) feature->applyViewState(view);
}

// ---------------------------------------------------------------------------
// Saving the project
// ---------------------------------------------------------------------------

void MainWindow::collectViewState()
{
    ViewState view;
    for (const std::unique_ptr<Feature>& feature : m_features) feature->collectViewState(view);
    project().setView(view);

    WindowState window;
    window.geometry = saveGeometry();
    window.dockState = saveState();
    window.ribbonPage = m_ribbon->currentPage();
    window.ribbonCollapsed = m_ribbon->collapsed();
    project().setWindow(window);
}

bool MainWindow::saveProject()
{
    collectViewState();

    QString error;
    if (m_session->save(&error)) return true;

    QMessageBox::warning(this, tr("Cannot save the project"),
                         tr("The project could not be written.\n\n%1\n\nThe work is still "
                            "here; fix the problem and save again.")
                             .arg(error));
    return false;
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    // No "do you want to save?" -- there is nothing that is not already in the
    // project, and asking would imply otherwise.
    saveProject();
    QMainWindow::closeEvent(event);
    if (event->isAccepted()) emit closed();
}

// ---------------------------------------------------------------------------
// What a feature is offered
// ---------------------------------------------------------------------------

void MainWindow::requestProject(const QString& manifestPath)
{
    emit openProjectRequested(manifestPath);
}

void MainWindow::requestProjectList()
{
    emit projectListRequested();
}

void MainWindow::showStatus(const QString& text, int milliseconds)
{
    statusBar()->showMessage(text, milliseconds);
}

void MainWindow::showProblem(const SessionMessage& problem)
{
    if (!problem.isEmpty()) QMessageBox::warning(this, problem.title, problem.text);
}

void MainWindow::loadFile(const QString& path)
{
    importChassisFile(*this, path);
}

void MainWindow::loadHardpointFile(const QString& path)
{
    importHardpointFile(*this, path);
}

void MainWindow::setHardpointTable(const HardpointTable& table, bool refit)
{
    hardpoints()->setTable(table);
    syncTableToViewport(refit);
}

void MainWindow::syncTableToViewport(bool refit)
{
    m_viewport->setHardpoints(hardpoints()->table());
    // setHardpoints() drops the parts, because they are indices into the table
    // that has just been replaced. Resolving them again is what puts them back.
    // Which rebinds the mechanism and places the wheels as well: all three are
    // resolved against the table that has just been replaced.
    m_session->resolveFromTable();
    // Only reframe when the hardpoints are all there is. With a mesh on screen
    // the user has already chosen a view, and moving it would be rude.
    if (refit) m_viewport->fitToView();
    updateChrome(); // which refreshes the status line and the title too
}

void MainWindow::selectPoints(const QList<int>& rows, int current)
{
    m_viewport->setSelectedHardpoints(rows, current);
    m_hardpointPanel->setSelectedRows(m_viewport->selectedHardpoints(),
                                      m_viewport->selectedHardpoint());
    updateChrome();
}

// ---------------------------------------------------------------------------
// Chrome
// ---------------------------------------------------------------------------

void MainWindow::setDisplayMode(DisplayMode mode)
{
    // The command that shows this mode follows the viewport rather than
    // being set here: ViewFeature listens for displayModeChanged.
    m_viewport->setDisplayMode(mode);
}

QImage MainWindow::captureViewport()
{
    return m_viewport->grabFramebuffer();
}

QImage MainWindow::captureWindow()
{
    // The viewport is rendered on its own and laid into its place: grab() on
    // the window can come back with a viewport that has not drawn a frame yet.
    const QImage frame = m_viewport->grabFramebuffer();
    QImage image = grab().toImage();
    if (!frame.isNull() && m_viewport->isVisible()) {
        QPainter painter(&image);
        painter.drawImage(QRect(m_viewport->mapTo(this, QPoint(0, 0)), m_viewport->size()), frame);
    }
    return image;
}

void MainWindow::updateWindowTitle(const HardpointEdits& pending)
{
    QStringList parts;
    parts << project().name();
    if (!project().geometry().isEmpty())
        parts << QFileInfo(project().geometry().relativePath).fileName();
    if (!project().hardpoints().isEmpty()) {
        parts << QFileInfo(project().hardpoints().workbook.relativePath).fileName()
                     + (pending.isEmpty() ? QString() : QStringLiteral("*"));
    }
    parts << tr("SuspensionKinematics");
    setWindowTitle(parts.join(QStringLiteral(" - ")));
}

void MainWindow::updateHardpointStatus(const HardpointEdits& pending)
{
    const int count = hardpoints()->rowCount();
    if (count == 0) {
        m_hardpointLabel->clear();
        return;
    }

    const Linkage& linkage = m_session->linkage().parts();
    const std::vector<WheelPlacement>& wheels = m_session->wheels().placements();
    QString text = project().hardpoints().sheetName.isEmpty()
                       ? tr("%1 hardpoints").arg(count)
                       : tr("%1 hardpoints - %2").arg(count).arg(project().hardpoints().sheetName);
    if (!linkage.isEmpty())
        text += tr("  -  %1 parts").arg(linkage.parts.size());
    if (!wheels.empty())
        text += tr("  -  %1 wheels").arg(wheels.size());
    if (!pending.isEmpty())
        text += tr("  -  %1 not in the workbook").arg(pending.count());
    m_hardpointLabel->setText(text);
}

void MainWindow::updateChrome()
{
    // Each command decides for itself whether it can be used, and the few whose
    // name follows the state what they are called; this asks them all. Cheap
    // -- a predicate each -- so anything that changes the window's state can
    // call it rather than working out which commands it touched.
    m_commands.refreshEnabled();
    m_commands.refreshText();
    const QString& chassis = m_session->chassis().summary();
    m_meshLabel->setText(chassis.isEmpty() ? tr("No chassis imported") : chassis);
    // Worked out once for both: this runs on every frame of an animation, and
    // the diff is the most expensive thing in it.
    const HardpointEdits pending = m_session->hardpoints().pendingEdits();
    updateHardpointStatus(pending);
    updateWindowTitle(pending);
}

} // namespace suspkin
