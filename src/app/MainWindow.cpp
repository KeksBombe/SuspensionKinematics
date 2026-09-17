#include "app/MainWindow.h"

#include "app/AnalysisPanel.h"
#include "app/CoordinateEntry.h"
#include "app/GenerateDialog.h"
#include "app/HardpointModel.h"
#include "app/HardpointPanel.h"
#include "app/Icons.h"
#include "app/MirrorDialog.h"
#include "app/PanelAction.h"
#include "app/PartDialogs.h"
#include "app/PointDialog.h"
#include "app/ProjectLauncher.h"
#include "app/RecentProjects.h"
#include "app/Ribbon.h"
#include "app/framework/FeatureRegistry.h"
#include "app/framework/RibbonPages.h"
#include "app/StaticAnglesDialog.h"
#include "app/SteeringDialog.h"
#include "app/WheelDialog.h"
#include "io/LinkageTemplate.h"
#include "io/MeshImport.h"
#include "model/HardpointGenerator.h"
#include "render/MoveGizmo.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QDir>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QImage>
#include <QInputDialog>
#include <QLabel>
#include <QLocale>
#include <QMatrix4x4>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QStandardPaths>
#include <QStatusBar>
#include <QSaveFile>
#include <QUrl>

#include <algorithm>
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
    buildPointEditing();
    buildCommands();
    buildFileMenu();
    buildRibbon();
    finishCommands();

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
                selectRows(selection.isEmpty() ? QList<int>{ row } : selection, row);
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
        markDirty();
        const HardpointTable& table = hardpoints()->table();
        if (row >= 0 && row < static_cast<int>(table.points.size()))
            m_session->recordEdit(
                tr("Configure %1").arg(table.points[static_cast<std::size_t>(row)].name));
    });
}

void MainWindow::buildPointEditing()
{
    // A child of the viewport: it opens over the marker it is about, and it is
    // gone as soon as the viewport is.
    m_coordinateEntry = new CoordinateEntry(m_viewport);

    connect(m_viewport, &ViewportWidget::coordinateEntryRequested, this,
            &MainWindow::openCoordinateEntry);
    connect(m_coordinateEntry, &CoordinateEntry::committed, this,
            [this](int axis, double value) { moveHardpointCoordinate(m_entryRow, axis, value); });
    connect(m_coordinateEntry, &CoordinateEntry::closed, this, [this] {
        m_entryRow = -1;
        // The viewport takes the keyboard back, so the next X, Y or Z lands
        // where the first one did rather than nowhere.
        m_viewport->setFocus(Qt::OtherFocusReason);
    });

    // Nothing on the ribbon says that a selected point can be dragged or typed
    // at, so picking one in the viewport says it. The table's own selection
    // does not: there the coordinate columns are already in front of the user.
    connect(m_viewport, &ViewportWidget::hardpointSelectionEdited, this,
            [this](const QList<int>&, int current) {
                if (current < 0 || !m_viewport->pointEditingEnabled()) return;
                const HardpointTable& table = hardpoints()->table();
                if (current >= static_cast<int>(table.points.size())) return;
                statusBar()->showMessage(
                    tr("%1 — drag an arrow to move it, or press X, Y or Z to type a coordinate")
                        .arg(table.points[static_cast<std::size_t>(current)].name),
                    6000);
            });

    // A drag says where it has got to the whole way along and what it came to
    // at the end. Only the end is an edit; the rest is a readout.
    connect(m_viewport, &ViewportWidget::hardpointDragging, this, &MainWindow::showDragPosition);
    connect(m_viewport, &ViewportWidget::hardpointMoved, this,
            [this](int row, int axis, double distance) {
                const HardpointTable& table = hardpoints()->table();
                if (row < 0 || row >= static_cast<int>(table.points.size())) return;
                // Added to the table's own double, not read back off the
                // marker: the marker is a float, and the two coordinates the
                // drag did not touch have to come through it unchanged.
                moveHardpointCoordinate(
                    row, axis, table.points[static_cast<std::size_t>(row)].coord[axis] + distance);
            });
}

void MainWindow::openCoordinateEntry(int row, int axis)
{
    const HardpointTable& table = hardpoints()->table();
    if (row < 0 || row >= static_cast<int>(table.points.size())) return;

    QPointF anchor;
    if (!m_viewport->markerPosition(row, &anchor)) return; // off screen: nothing to open beside

    const Hardpoint& point = table.points[static_cast<std::size_t>(row)];
    m_entryRow = row;
    m_coordinateEntry->openAt(anchor, point.name, axis, point.coord[axis]);
}

void MainWindow::moveHardpointCoordinate(int row, int axis, double value)
{
    if (row < 0 || row >= hardpoints()->rowCount()) return;
    if (axis < 0 || axis >= 3) return;

    // Through the model, exactly as the table's own cell does it: what follows
    // -- the parts, the solve, the wheels, the edits file -- hangs off the
    // coordinateChanged() that this produces.
    const QModelIndex index = hardpoints()->index(row, HardpointModel::XColumn + axis);
    hardpoints()->setData(index, value, Qt::EditRole);
}

void MainWindow::showDragPosition(int row, int axis, double distance)
{
    const HardpointTable& table = hardpoints()->table();
    if (row < 0 || row >= static_cast<int>(table.points.size())) return;

    const Hardpoint& point = table.points[static_cast<std::size_t>(row)];
    const QLocale locale;
    statusBar()->showMessage(tr("%1  %2 %3 mm  (%4 mm)")
                                 .arg(point.name, MoveGizmo::axisLabel(axis),
                                      locale.toString(point.coord[axis] + distance, 'f', 3),
                                      locale.toString(distance, 'f', 3)));
}

// ---------------------------------------------------------------------------
// Undo and redo
// ---------------------------------------------------------------------------

void MainWindow::restoreEditState(const EditState& from, const EditState& to)
{
    // Taken before the rows move under it.
    const QStringList selected = selectedPointNames();

    // The field was typing into a point that may be about to move or go.
    m_coordinateEntry->dismiss();
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
    selectRows(rows, rows.isEmpty() ? -1 : rows.front());
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
    connect(m_analysisPanel, &AnalysisPanel::measuresChanged, this, [this] { markDirty(); });
    connect(m_analysisPanel, &AnalysisPanel::sidesChanged, this, [this] { markDirty(); });
    // How fast the animation runs and whether the parameters window is open
    // change nothing about the curve, but they are still the user's arrangement
    // and the project keeps them.
    connect(m_analysisPanel, &AnalysisPanel::playbackChanged, this, [this] { markDirty(); });
    connect(m_analysisPanel, &AnalysisPanel::exportCsvRequested, this,
            &MainWindow::exportSweepCsv);

    // The sweep is skipped while the dock is shut, so opening it is what asks
    // for one. Reopening a project restores the dock, and this catches that too.
    connect(m_analysisDock, &QDockWidget::visibilityChanged, this, [this](bool visible) {
        if (visible && m_session->simulation().sweep().isEmpty()) m_session->runSweep();
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
    connect(m_session, &ProjectSession::historyChanged, this, &MainWindow::updateChrome);
}

SimulationRequest MainWindow::simulationRequest() const
{
    SimulationRequest request;
    request.axle = m_analysisPanel->axle();
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
    m_analysisPanel->setResult(runner.sweep());
    m_analysisPanel->setStatus(runner.status().join(QStringLiteral("\n")));
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
    if (!pose.isEmpty()) m_coordinateEntry->dismiss();

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

void MainWindow::revealTemplateFile()
{
    const QString path = project().absolutePath(project().linkageTemplate().relativePath);
    if (path.isEmpty() || !QFileInfo::exists(path)) return;
    QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

void MainWindow::finishCommands()
{
    // A ribbon button shows its action's tooltip, and Qt adds neither the
    // shortcut nor the status tip to one by itself.
    for (QAction* action : m_commands.all()) action->setToolTip(commandToolTip(action));

    // On the window itself as well as wherever it is shown: a shortcut is live
    // only while some widget the action is on is visible, and with the menu
    // bar hidden and its button on a tab that is not showing, Ctrl+I would
    // otherwise do nothing at all.
    addActions(m_commands.all());
}

void MainWindow::buildFileMenu()
{
    // There is no menu bar, and this is the only menu: the ribbon's File
    // button opens it, because what is in it -- the project itself, and the
    // way out -- is not a tab's worth of commands. Everything else, Help
    // included, is a tab. The mnemonic still works: Alt+F opens this.
    //
    // By id rather than by name: these commands belong to a feature the window
    // knows nothing else about.
    m_fileMenu = new QMenu(tr("&File"), this);
    m_fileMenu->addAction(m_commands.action(QStringLiteral("project.new")));
    m_fileMenu->addAction(m_commands.action(QStringLiteral("project.open")));
    m_recentProjectsMenu = m_fileMenu->addMenu(tr("Open &Recent"));
    m_recentProjectsMenu->setIcon(Icons::get(Icon::History));
    m_fileMenu->addSeparator();
    m_fileMenu->addAction(m_commands.action(QStringLiteral("project.save")));
    m_fileMenu->addAction(m_commands.action(QStringLiteral("project.list")));
    m_fileMenu->addAction(m_commands.action(QStringLiteral("project.reveal")));
    m_fileMenu->addSeparator();
    m_fileMenu->addAction(m_commands.action(QStringLiteral("project.quit")));
    connect(m_fileMenu, &QMenu::aboutToShow, this, &MainWindow::refreshRecentProjectsMenu);
    refreshRecentProjectsMenu();
}

void MainWindow::buildRibbon()
{
    m_ribbon = new Ribbon;
    m_ribbon->setApplicationMenu(m_fileMenu, tr("&File"));
    buildRibbonFrom(m_ribbon, m_commands);

    // The ribbon is the whole of the window's chrome: there is no menu bar
    // above it, because the tabs said the same words the menus did.
    //
    // From here on QMainWindow::menuBar() must never be called. On a window
    // whose menu widget is not a QMenuBar it makes one and installs it through
    // setMenuWidget(), which deleteLater()s what was there -- the ribbon.
    setMenuWidget(m_ribbon);

    // After the pages are in, so building them -- the first tab becoming
    // current -- is not taken for the user choosing it. What the chevron says
    // is PanelsFeature's, which owns it.
    connect(m_ribbon, &Ribbon::currentPageChanged, this, [this] { markDirty(); });
    connect(m_ribbon, &Ribbon::collapsedChanged, this, [this] { markDirty(); });
}

void MainWindow::resetPanelLayout()
{
    const bool hardpointsOpen = !m_hardpointDock->isHidden();
    const bool analysisOpen = !m_analysisDock->isHidden();

    restoreState(m_defaultDockState);

    // The default has every dock closed, because a new project has nothing to
    // put in them. What was open stays open, docked again; and the table is
    // open whenever there are points -- the rule a project follows the first
    // time it is opened.
    if (hardpointsOpen || hardpoints()->rowCount() > 0) m_hardpointDock->show();
    if (analysisOpen) m_analysisDock->show();
    markDirty();
    statusBar()->showMessage(tr("Panels docked where a new project has them."), 4000);
}

void MainWindow::refreshRecentProjectsMenu()
{
    if (!m_recentProjectsMenu) return;
    m_recentProjectsMenu->clear();

    const QString current = QFileInfo(project().manifestPath()).absoluteFilePath();
    int shown = 0;
    for (const RecentProject& entry : RecentProjects::load()) {
        if (QFileInfo(entry.manifestPath).absoluteFilePath() == current) continue;
        if (!entry.exists()) continue;
        auto* action = m_recentProjectsMenu->addAction(
            QStringLiteral("%1  -  %2").arg(entry.name, QDir::toNativeSeparators(entry.directory())));
        const QString path = entry.manifestPath;
        connect(action, &QAction::triggered, this, [this, path] {
            saveProject();
            emit openProjectRequested(path);
        });
        ++shown;
    }
    m_recentProjectsMenu->setEnabled(shown > 0);
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
    warn(problem);
    if (!model) return false;

    m_viewport->setMesh(std::move(model->mesh), model->edges);
    updateChrome();
    return true;
}

bool MainWindow::openHardpoints()
{
    SessionMessage problem;
    std::optional<HardpointTable> table = m_session->hardpoints().open(&problem);
    warn(problem);
    if (!table) return false;

    setHardpointTable(std::move(*table), false);
    return true;
}

bool MainWindow::openLinkageTemplate()
{
    SessionMessage problem;
    const bool loaded = m_session->linkage().load(&problem);
    warn(problem);
    return loaded;
}

bool MainWindow::openWheels()
{
    SessionMessage problem;
    std::optional<WheelModels> models = m_session->wheels().open(&problem);
    warn(problem);
    if (!models) {
        m_viewport->clearWheels();
        return false;
    }

    m_viewport->setWheelModels(std::move(models->tyre), std::move(models->tyreEdges),
                               std::move(models->rim), std::move(models->rimEdges));
    m_session->placeWheels();
    return !m_session->wheels().placements().empty();
}

void MainWindow::warn(const SessionMessage& problem)
{
    if (!problem.isEmpty()) QMessageBox::warning(this, problem.title, problem.text);
}

void MainWindow::applyViewState()
{
    const ViewState& view = project().view();
    if (view.cameraValid) m_viewport->setCameraState(view.camera);

    setDisplayMode(view.displayMode);
    m_commands.action(QStringLiteral("hardpoints.showLabels"))->setChecked(view.labelsVisible);
    m_viewport->setHardpointLabelsVisible(view.labelsVisible);
    m_commands.action(QStringLiteral("linkage.showParts"))->setChecked(view.linksVisible);
    m_viewport->setLinkageVisible(view.linksVisible);
    m_commands.action(QStringLiteral("wheels.show"))->setChecked(view.wheelsVisible);
    m_viewport->setWheelsVisible(view.wheelsVisible);

    // The whole selection, in the order it was picked -- a chain half-picked
    // for a new part comes back half-picked.
    QList<int> selection;
    for (const int row : view.selection)
        if (row >= 0 && row < hardpoints()->rowCount()) selection.append(row);
    if (!selection.isEmpty()) selectRows(selection, view.selectedHardpoint);

    // The travel and the kind have to be set before the position, because
    // between them they decide the range the position is allowed to take.
    const SimulationState& simulation = view.simulation;
    m_analysisPanel->setSettings(simulation.sweep);
    m_analysisPanel->setKind(simulation.kind);
    if (!simulation.axle.isEmpty()) m_analysisPanel->setAxle(simulation.axle);
    QList<SweepMeasure> measures;
    for (const QString& key : simulation.measures) {
        // A curve this build does not know -- one a later release added -- is
        // left out rather than read as the fallback, which would put a plot on
        // screen that nobody asked for.
        const SweepMeasure measure = sweepMeasureFromKey(key);
        if (sweepMeasureKey(measure) == key) measures << measure;
    }
    if (!measures.isEmpty()) m_analysisPanel->setMeasures(measures);
    m_analysisPanel->setSides(simulation.sides);
    m_analysisPanel->setPosition(simulation.position);
    m_analysisPanel->setMovesAllAxles(simulation.allAxles);
    m_analysisPanel->setAnimationSeconds(simulation.animationSeconds);
    m_analysisPanel->setParametersVisible(simulation.parametersOpen);
    m_analysisPanel->setSimulating(simulation.active);
    m_session->resolveSimulation();
    // Last, because starting it turns the simulation on and moves the model,
    // and both of those have to be settled first.
    m_analysisPanel->setAnimating(simulation.animating && simulation.active);
}

// ---------------------------------------------------------------------------
// Saving the project
// ---------------------------------------------------------------------------

void MainWindow::collectViewState()
{
    ViewState view;
    view.camera = m_viewport->cameraState();
    view.cameraValid = true;
    view.displayMode = m_viewport->displayMode();
    view.labelsVisible = m_viewport->hardpointLabelsVisible();
    view.linksVisible = m_viewport->linkageVisible();
    view.wheelsVisible = m_viewport->wheelsVisible();
    view.selectedHardpoint = m_viewport->selectedHardpoint();
    view.selection = m_viewport->selectedHardpoints();

    SimulationState& simulation = view.simulation;
    simulation.active = m_analysisPanel->simulating();
    simulation.axle = m_analysisPanel->axle();
    simulation.kind = m_analysisPanel->kind();
    simulation.sweep = m_analysisPanel->settings();
    simulation.position = m_analysisPanel->position();
    for (const SweepMeasure measure : m_analysisPanel->measures())
        simulation.measures << sweepMeasureKey(measure);
    simulation.sides = m_analysisPanel->sides();
    simulation.animating = m_analysisPanel->animating();
    simulation.animationSeconds = m_analysisPanel->animationSeconds();
    simulation.allAxles = m_analysisPanel->movesAllAxles();
    simulation.parametersOpen = m_analysisPanel->parametersVisible();

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
// Projects
// ---------------------------------------------------------------------------

void MainWindow::newProject()
{
    const QString path = ProjectLauncher::runNewProjectDialog(this);
    if (path.isEmpty()) return;
    saveProject();
    emit openProjectRequested(path);
}

void MainWindow::openProject()
{
    const QString path = ProjectLauncher::runOpenProjectDialog(this);
    if (path.isEmpty()) return;
    if (QFileInfo(path).absoluteFilePath()
        == QFileInfo(project().manifestPath()).absoluteFilePath()) {
        return; // already open
    }
    saveProject();
    emit openProjectRequested(path);
}

void MainWindow::showProjectList()
{
    saveProject();
    emit projectListRequested();
}

void MainWindow::revealProjectFolder()
{
    QDesktopServices::openUrl(QUrl::fromLocalFile(project().rootPath()));
}

void MainWindow::showStatus(const QString& text, int milliseconds)
{
    statusBar()->showMessage(text, milliseconds);
}

// ---------------------------------------------------------------------------
// Geometry
// ---------------------------------------------------------------------------

QString MainWindow::geometryDialogDirectory() const
{
    if (!project().lastGeometryDirectory().isEmpty()) return project().lastGeometryDirectory();
    return QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
}

void MainWindow::importChassisDialog()
{
    const QString path = QFileDialog::getOpenFileName(this, tr("Import chassis"),
                                                      geometryDialogDirectory(),
                                                      importFileFilter());
    if (!path.isEmpty()) loadFile(path);
}

void MainWindow::loadFile(const QString& path)
{
    SessionMessage problem;
    std::optional<ChassisModel> model = m_session->chassis().import(path, &problem);
    if (!model) {
        warn(problem); // whatever was on screen stays there
        return;
    }

    m_viewport->setMesh(std::move(model->mesh), model->edges);
    m_viewport->fitToView();
    updateChrome();
}

void MainWindow::removeChassis()
{
    const AssetRef asset = project().geometry();
    if (!asset.isEmpty()) {
        const QMessageBox::StandardButton answer = QMessageBox::question(
            this, tr("Remove chassis"),
            tr("Remove %1 from this project?\n\nThe copy inside the project folder is deleted. "
               "The file it was imported from is not touched.")
                .arg(QFileInfo(asset.relativePath).fileName()),
            QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
        if (answer != QMessageBox::Yes) return;
    }

    m_session->chassis().remove();
    m_viewport->clearMesh();
    updateChrome();
}

// ---------------------------------------------------------------------------
// Hardpoints
// ---------------------------------------------------------------------------

QString MainWindow::hardpointDialogDirectory() const
{
    if (!project().lastHardpointDirectory().isEmpty()) return project().lastHardpointDirectory();
    if (!project().lastGeometryDirectory().isEmpty()) return project().lastGeometryDirectory();
    return QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
}

void MainWindow::importHardpointsDialog()
{
    const QString path = QFileDialog::getOpenFileName(this, tr("Import hardpoints"),
                                                      hardpointDialogDirectory(),
                                                      hardpointFileFilter());
    if (!path.isEmpty()) loadHardpointFile(path);
}

void MainWindow::loadHardpointFile(const QString& path)
{
    HardpointDocument& document = m_session->hardpoints();
    const HardpointEdits pending = document.pendingEdits();
    if (!pending.isEmpty()) {
        const QMessageBox::StandardButton answer = QMessageBox::question(
            this, tr("Replace the hardpoints?"),
            tr("This project is holding %1 hardpoint change(s) that are not in its workbook "
               "yet.\n\nImporting a different workbook discards them. Continue?")
                .arg(pending.count()),
            QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
        if (answer != QMessageBox::Yes) return;
    }

    SessionMessage problem;
    const std::optional<HardpointImport> imported = document.import(path, &problem);
    warn(problem);
    if (!imported) return; // whatever was loaded stays loaded

    setHardpointTable(document.baseline(), !m_viewport->hasMesh());
    // An import is not an edit: the steps that led to the old points lead
    // nowhere in these.
    m_session->restartHistory();
    m_hardpointDock->show();
    m_hardpointDock->raise();

    statusBar()->showMessage(tr("Imported %1 hardpoints from %2 in %3 ms")
                                 .arg(hardpoints()->rowCount())
                                 .arg(QFileInfo(path).fileName())
                                 .arg(imported->elapsedMs),
                             6000);

    if (!imported->warnings.isEmpty()) {
        QMessageBox::information(this, tr("Imported with warnings"),
                                 tr("%1 hardpoints were imported.\n\n%2")
                                     .arg(hardpoints()->rowCount())
                                     .arg(imported->warnings.join(QStringLiteral("\n\n"))));
    }
}

void MainWindow::setHardpointTable(HardpointTable table, bool refit)
{
    hardpoints()->setTable(std::move(table));
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

void MainWindow::mirrorHardpointsDialog()
{
    if (hardpoints()->rowCount() == 0) return;

    MirrorDialog dialog(hardpoints()->table(), m_viewport->selectedHardpoints(),
                        project().mirror(), this);
    if (dialog.exec() != QDialog::Accepted) return;

    const MirrorSpec spec = dialog.spec();
    const MirrorOutcome outcome = mirrorHardpoints(hardpoints()->table(), dialog.rows(), spec);

    // The rule is remembered whether or not it changed anything: it is the
    // user's convention, and they will want it again next time.
    project().setMirror(spec);

    if (outcome.added == 0 && outcome.updated == 0) {
        QMessageBox::information(this, tr("Nothing to mirror"),
                                 outcome.notes.isEmpty()
                                     ? tr("No hardpoints were mirrored.")
                                     : outcome.notes.join(QStringLiteral("\n\n")));
        markDirty();
        return;
    }

    setHardpointTable(outcome.table, false);
    m_session->hardpoints().captureMirrorProvenance();
    markDirty();
    m_session->recordEdit(tr("Mirror %n point(s)", "", outcome.added + outcome.updated));

    statusBar()->showMessage(tr("Mirrored %1 hardpoint(s), replaced %2")
                                 .arg(outcome.added)
                                 .arg(outcome.updated),
                             6000);

    if (!outcome.notes.isEmpty()) {
        QMessageBox::information(this, tr("Mirrored with notes"),
                                 outcome.notes.join(QStringLiteral("\n\n")));
    }
}

bool MainWindow::overwriteWorkbook()
{
    HardpointDocument& document = m_session->hardpoints();
    const QString path = document.workbookPath();
    if (path.isEmpty()) return false;

    // Every point deleted is a workbook with no table in it, which the reader
    // cannot open again -- and the project would lose its workbook with it.
    if (hardpoints()->rowCount() == 0) {
        QMessageBox::information(this, tr("Overwrite Workbook"),
                                 tr("Every point has been deleted, so the workbook would be left "
                                    "with nothing in it that can be read back.\n\nTo take the "
                                    "hardpoints out of the project, use Remove Hardpoints."));
        return false;
    }

    const HardpointEdits pending = document.pendingEdits();
    QMessageBox box(this);
    box.setIcon(QMessageBox::Warning);
    box.setWindowTitle(tr("Overwrite the project's workbook?"));
    box.setText(tr("Write %1 change(s) into %2?")
                    .arg(pending.count())
                    .arg(QFileInfo(path).fileName()));
    box.setInformativeText(
        tr("%1\n\nThe hardpoint cells are rewritten in place and mirrored points are added as new "
           "rows; everything else in the workbook -- other sheets, formatting and formulas -- is "
           "kept as it is.\n\nThis is the project's own copy. The file it was imported from is "
           "not touched; use Export Workbook As to write that one.")
            .arg(QDir::toNativeSeparators(path)));
    QPushButton* overwrite = box.addButton(tr("Overwrite"), QMessageBox::AcceptRole);
    box.addButton(QMessageBox::Cancel);
    box.setDefaultButton(overwrite);
    box.exec();
    if (box.clickedButton() != overwrite) return false;

    SessionMessage problem;
    if (!document.overwriteWorkbook(&problem)) {
        warn(problem);
        return false;
    }
    setHardpointTable(document.baseline(), false);

    saveProject(); // clears the edits file, now that they are in the workbook
    statusBar()->showMessage(tr("Wrote the hardpoints into %1").arg(QFileInfo(path).fileName()),
                             6000);
    return true;
}

bool MainWindow::exportWorkbookAs()
{
    // Default to where the workbook came from: exporting back over the original
    // is the common case, and this makes it one click without assuming it.
    QString suggestion = project().hardpoints().workbook.originalPath;
    if (suggestion.isEmpty()) {
        QString name = QFileInfo(project().hardpoints().workbook.relativePath).fileName();
        if (name.isEmpty()) name = QStringLiteral("hardpoints.xlsx");
        suggestion = QDir(hardpointDialogDirectory()).filePath(name);
    }

    QString path = QFileDialog::getSaveFileName(this, tr("Export hardpoint workbook"), suggestion,
                                                hardpointFileFilter());
    if (path.isEmpty()) return false;
    if (QFileInfo(path).suffix().isEmpty()) path += QStringLiteral(".xlsx");

    SessionMessage problem;
    if (!m_session->hardpoints().writeTo(path, &problem)) {
        warn(problem);
        return false;
    }

    project().setLastHardpointDirectory(QFileInfo(path).absolutePath());
    markDirty();
    statusBar()->showMessage(tr("Exported %1 hardpoints to %2")
                                 .arg(hardpoints()->rowCount())
                                 .arg(QFileInfo(path).fileName()),
                             6000);
    return true;
}

void MainWindow::removeHardpoints()
{
    const HardpointEdits pending = m_session->hardpoints().pendingEdits();
    const QString question =
        pending.isEmpty()
            ? tr("Remove the hardpoints from this project?\n\nThe workbook copy inside the "
                 "project folder is deleted. The file it was imported from is not touched.")
            : tr("Remove the hardpoints from this project?\n\n%1 change(s) have not been written "
                 "to a workbook and will be lost. The workbook copy inside the project folder is "
                 "deleted; the file it was imported from is not touched.")
                  .arg(pending.count());

    const QMessageBox::StandardButton answer =
        QMessageBox::question(this, tr("Remove hardpoints"), question,
                              QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
    if (answer != QMessageBox::Yes) return;

    m_session->hardpoints().remove();
    m_viewport->clearHardpoints();
    // The template stays: it describes a kind of car, not this workbook, and
    // the next import should find it already there.
    m_session->resolveFromTable();
    // So do the wheel models, for the same reason. With no points to pin them
    // to there is nowhere to draw them, which is what this leaves behind.
    m_session->placeWheels();
    m_hardpointDock->hide();
    // The workbook and the edits file are deleted, which no step can put back.
    m_session->restartHistory();
    markDirty();
}

void MainWindow::selectRows(const QList<int>& rows, int current)
{
    m_viewport->setSelectedHardpoints(rows, current);
    m_hardpointPanel->setSelectedRows(m_viewport->selectedHardpoints(),
                                      m_viewport->selectedHardpoint());
    updateChrome();
}

bool MainWindow::adoptNewWorkbook(const HardpointTable& table)
{
    SessionMessage problem;
    if (!m_session->hardpoints().adoptNewWorkbook(table, &problem)) {
        warn(problem);
        return false;
    }

    setHardpointTable(m_session->hardpoints().baseline(), !m_viewport->hasMesh());
    // The table begins here. Undoing past it would need the workbook that was
    // just made to be unmade, which is Remove Hardpoints, not a step.
    m_session->restartHistory();
    m_hardpointDock->show();
    m_hardpointDock->raise();
    markDirty();
    return true;
}

void MainWindow::addPointDialog()
{
    const HardpointTable& table = hardpoints()->table();
    const int current = m_viewport->selectedHardpoint();

    // Seeded from the selection, so the new point starts where the one being
    // worked on is -- with a name that is free.
    Hardpoint seed;
    seed.name = QStringLiteral("P1");
    if (current >= 0 && current < static_cast<int>(table.size())) {
        seed = table.points[static_cast<std::size_t>(current)];
        seed.mirrorOf.clear();
    }

    const bool creating = project().hardpoints().isEmpty();
    const QString note =
        creating ? tr("This project has no hardpoint workbook yet. Adding a point makes one inside "
                      "the project, hardpoints/hardpoints.xlsx, and the table grows from there.")
                 : QString();
    PointDialog dialog(table, seed, note, this);
    if (creating) dialog.setWindowTitle(tr("New Hardpoint Table"));
    if (dialog.exec() != QDialog::Accepted) return;
    const Hardpoint point = dialog.point();

    if (creating) {
        HardpointTable first;
        first.points.push_back(point);
        if (!adoptNewWorkbook(first)) return;
        selectRows({ 0 }, 0);
        statusBar()->showMessage(tr("Started a hardpoint table with %1").arg(point.name), 6000);
        return;
    }

    // Right under the one it was seeded from, where it belongs; the edits file
    // remembers the place, so it comes back there too.
    const int row = current >= 0 ? current + 1 : hardpoints()->rowCount();
    if (!hardpoints()->insertPoint(row, point)) return;
    syncTableToViewport(false);
    selectRows({ row }, row);
    markDirty();
    m_session->recordEdit(tr("Add %1").arg(point.name));
    statusBar()->showMessage(tr("Added %1").arg(point.name), 5000);
}

void MainWindow::deleteSelectedPoints()
{
    const QList<int> rows = m_viewport->selectedHardpoints();
    if (rows.isEmpty()) return;

    // Not asked about: Ctrl+Z brings them back. A point the workbook holds
    // stays in it until the workbook is overwritten, as the command says.
    const QString label =
        rows.size() == 1
            ? tr("Delete %1").arg(
                  hardpoints()->table().points[static_cast<std::size_t>(rows.front())].name)
            : tr("Delete %n point(s)", "", rows.size());

    const int first = *std::min_element(rows.begin(), rows.end());
    hardpoints()->removePoints(std::vector<int>(rows.begin(), rows.end()));
    // The model has dropped their configuration; the project, which the table
    // is refilled from, has to agree before anything is resolved again.
    m_session->hardpoints().captureConfig();
    m_session->hardpoints().captureMirrorProvenance();
    syncTableToViewport(false);

    const int next = std::min(first, hardpoints()->rowCount() - 1);
    if (next >= 0) selectRows({ next }, next);
    markDirty();
    m_session->recordEdit(label);
    statusBar()->showMessage(tr("Deleted %n point(s). Ctrl+Z brings them back.", "", rows.size()),
                             6000);
}

void MainWindow::renamePointDialog()
{
    const int row = m_viewport->selectedHardpoint();
    const HardpointTable& table = hardpoints()->table();
    if (row < 0 || row >= static_cast<int>(table.size())) return;
    const QString current = table.points[static_cast<std::size_t>(row)].name;

    QString proposed = current;
    for (;;) {
        bool ok = false;
        proposed = QInputDialog::getText(this, tr("Rename Point"), tr("New name for %1:").arg(current),
                                         QLineEdit::Normal, proposed, &ok)
                       .trimmed();
        if (!ok || proposed == current) return;
        // Asked here as well as in the model, so a refusal is a message the
        // user can act on and try again from, not an edit that silently fails.
        const QString problem = hardpointNameProblem(proposed, table, row);
        if (problem.isEmpty()) break;
        QMessageBox::information(this, tr("Rename Point"), problem);
    }
    // The model's pointRenamed does the rest.
    hardpoints()->renamePoint(row, proposed);
}

DesignParameters MainWindow::initialDesign() const
{
    DesignParameters design;
    const LinkageTemplate& templ = m_session->linkage().linkageTemplate();
    const std::vector<CornerSpec>& corners = templ.corners;

    // This project's own corners: the first is taken to be the front and the
    // second the rear, which is how every template written so far lists them.
    const bool declared = templ.steeringDeclared();
    for (const auto& [position, index] :
         { std::pair{ AxlePosition::Front, 0 }, std::pair{ AxlePosition::Rear, 1 } }) {
        AxleDesign& axle = design.axle(position);
        if (index >= static_cast<int>(corners.size())) {
            axle.generate = false;
            continue;
        }
        const CornerSpec& corner = corners[static_cast<std::size_t>(index)];
        axle.corner = corner.token;
        // Whatever the template says about this axle's rack now is the start.
        axle.steered = declared ? !corner.steeringRack.isEmpty() : true;
    }

    // The side the template's names are already on, when the table says.
    if (!corners.empty()) {
        const MechanismTemplate names = instantiateMechanism(templ.mechanism,
                                                             corners.front().token, false,
                                                             project().mirror());
        if (const Hardpoint* centre = m_session->hardpoints().model().table().find(names.wheelCenter))
            design.side = centre->y() < 0.0 ? DesignSide::Right : DesignSide::Left;
    }
    return design;
}

void MainWindow::generateFromDesignDialog()
{
    const LinkageTemplate& templ = m_session->linkage().linkageTemplate();
    if (!templ.canSimulate() || templ.corners.empty()) return;

    const DesignParameters seed = project().design().value_or(initialDesign());
    GenerateDialog dialog(
        seed, templ, project().mirror(), hardpoints()->table(), m_session->hardpoints().baseline(),
        m_viewport->hasMesh(), [this] { return m_session->chassis().query(m_viewport->mesh()); },
        this);
    const int answer = dialog.exec();

    // The targets are the user's work whether or not anything was generated
    // from them: reopening the dialog starts where they left it.
    if (!project().design() || *project().design() != dialog.parameters()) {
        project().setDesign(dialog.parameters());
        markDirty();
    }
    if (answer != QDialog::Accepted) return;

    const DesignPlan plan = dialog.plan();
    if (!plan.ok()) return;

    // An axle whose static angles the project states would have them win over
    // the wheel axis just generated, and the scrub, trail and roll centre built
    // off that camber would then be missed. So a generated axle that has stated
    // angles gets the ones it was generated with; one that has none keeps
    // reading them off the new points. Before the points go in, so the solve
    // they trigger already sees it.
    const DesignParameters& generated = dialog.parameters();
    QHash<QString, StaticAlignment> alignment = project().alignment();
    for (const AxleDesign* axle : { &generated.front, &generated.rear }) {
        if (axle->generate && alignment.contains(axle->corner))
            alignment.insert(axle->corner, StaticAlignment{ axle->camber, axle->toe });
    }
    project().setAlignment(alignment);

    // The points. A project with a workbook takes them as edits against it,
    // like any other change; one without gets a workbook made for them.
    if (project().hardpoints().isEmpty()) {
        if (!adoptNewWorkbook(plan.table)) return;
    } else {
        setHardpointTable(plan.table, false);
        m_session->hardpoints().captureMirrorProvenance();
    }

    // Which axle has a rack, into the template, patched like any other edit
    // of it. Read back afterwards, which resolves the parts and the solve
    // against the new points as well.
    if (plan.steeringChanged) {
        const std::vector<CornerSpec> steering = plan.steering;
        SessionMessage problem;
        m_session->linkage().patch(
            [&steering](const QByteArray& bytes, QString* error) {
                return setTemplateSteering(bytes, steering, error);
            },
            tr("The points were generated, but the steering could not be written into the "
               "linkage template."),
            &problem);
        warn(problem);
    }

    markDirty();
    // The points and the angles, one step. The steering written into the
    // template is not in it: the template is not an edit of the points. In a
    // project that had no workbook this records nothing -- the table began
    // with these points.
    m_session->recordEdit(tr("Generate from design"));
    // The advice goes on the status bar as well as in the dialog, and is not
    // applied: whether to move a chassis pivot is the designer's call.
    const QString done = tr("Generated %1 point(s): %2 new, %3 moved.")
                             .arg(plan.changes.size())
                             .arg(plan.count(DesignChange::Kind::Added))
                             .arg(plan.count(DesignChange::Kind::Moved));
    statusBar()->showMessage(plan.advice.isEmpty() ? done
                                                   : done + QLatin1Char(' ')
                                                         + plan.advice.join(QLatin1Char(' ')),
                             plan.advice.isEmpty() ? 8000 : 30000);
}

void MainWindow::newPartFromSelection()
{
    const QList<int> rows = m_viewport->selectedHardpoints();
    const LinkageTemplate& templ = m_session->linkage().linkageTemplate();
    if (rows.size() < 2 || templ.isEmpty()) return;

    // In the order they were picked: that is the order the chain is drawn in.
    const HardpointTable& table = hardpoints()->table();
    QStringList names;
    for (const int row : rows) names << table.points[static_cast<std::size_t>(row)].name;

    NewPartDialog dialog(templ, names, this);
    if (dialog.exec() != QDialog::Accepted) return;
    const PartTemplate part = dialog.part();

    SessionMessage problem;
    const bool written = m_session->linkage().patch(
        [&part](const QByteArray& bytes, QString* error) { return addTemplatePart(bytes, part, error); },
        tr("The part could not be added."), &problem);
    warn(problem);
    if (written) statusBar()->showMessage(tr("Added the part \"%1\"").arg(part.label), 5000);
}

void MainWindow::editPartsDialog()
{
    LinkageDocument& linkage = m_session->linkage();
    if (linkage.linkageTemplate().isEmpty()) return;
    const LinkageTemplate before = linkage.linkageTemplate();

    EditPartsDialog dialog(before, this);
    if (dialog.exec() != QDialog::Accepted) return;
    const QHash<QString, QString> relabelled = dialog.relabelled();
    const QStringList removed = dialog.removed();
    if (relabelled.isEmpty() && removed.isEmpty()) return;

    SessionMessage problem;
    const bool written = linkage.patch(
        [&](const QByteArray& bytes, QString* error) {
            QByteArray out = bytes;
            for (const QString& id : removed) {
                out = removeTemplatePart(out, id, error);
                if (out.isEmpty()) return out;
            }
            for (auto it = relabelled.constBegin(); it != relabelled.constEnd(); ++it) {
                out = setTemplatePartLabel(out, it.key(), it.value(), error);
                if (out.isEmpty()) return out;
            }
            return out;
        },
        tr("The parts could not be changed."), &problem);
    warn(problem);
    if (!written) return;

    // A part's label is also the name of the body the configuration table's
    // Part columns offer. A relabelled part takes the rows that named it along,
    // rather than leaving them all pointing at a body that is no longer there.
    HardpointConfigMap config = hardpoints()->config();
    const BodyCatalog catalog = bodyCatalog(linkage.linkageTemplate());
    int moved = 0;
    for (auto it = relabelled.constBegin(); it != relabelled.constEnd(); ++it) {
        for (const PartTemplate& part : before.parts) {
            if (part.id != it.key()) continue;
            PartTemplate renamed = part;
            renamed.label = it.value();
            const QString from = partBodyName(part);
            const QString to = partBodyName(renamed);
            if (from == to || catalog.contains(from)) continue;
            moved += renameBody(config, from, to);
            // True of every step, which all describe this part: an undo must
            // not put back rows naming a body the template no longer has.
            m_session->history().renameBody(from, to);
        }
    }
    if (moved > 0) {
        hardpoints()->setConfig(config);
        m_session->hardpoints().captureConfig();
    }
    statusBar()->showMessage(tr("Parts updated in the linkage template"), 5000);
}

// ---------------------------------------------------------------------------
// Linkage
// ---------------------------------------------------------------------------

void MainWindow::steeringDialog()
{
    const LinkageTemplate& templ = m_session->linkage().linkageTemplate();
    if (templ.isEmpty() || templ.corners.empty()) return;

    SteeringDialog dialog(templ, project().mirror(), hardpoints()->table(), this);
    if (dialog.exec() != QDialog::Accepted) return;

    const std::vector<CornerSpec> corners = dialog.corners();
    bool changed = corners.size() != templ.corners.size();
    for (std::size_t i = 0; !changed && i < corners.size(); ++i) {
        changed = corners[i].steeringRack != templ.corners[i].steeringRack
                  || corners[i].steeringStated != templ.corners[i].steeringStated;
    }
    if (!changed) return;

    SessionMessage problem;
    const bool written = m_session->linkage().patch(
        [&corners](const QByteArray& bytes, QString* error) {
            return setTemplateSteering(bytes, corners, error);
        },
        tr("The steering could not be saved."), &problem);
    warn(problem);
    if (written) statusBar()->showMessage(tr("Steering written to the linkage template."), 5000);
}

void MainWindow::staticAnglesDialog()
{
    const LinkageTemplate& linkage = m_session->linkage().linkageTemplate();
    const MechanismTemplate& mechanism = linkage.mechanism;
    const HardpointTable& table = hardpoints()->table();
    if (mechanism.isEmpty() || table.isEmpty()) return;

    std::vector<CornerSpec> axles = linkage.corners;
    if (axles.empty()) axles.push_back(CornerSpec{});
    const bool steeringDeclared = linkage.steeringDeclared();

    std::vector<StaticAnglesAxle> rows;
    for (const CornerSpec& corner : axles) {
        // Built without the project's angles, which is the only way to find out
        // what the hardpoints would say on their own -- the numbers an axle
        // goes back to, and the ones a newly ticked axle starts from.
        const AxleSolver bare =
            AxleSolver::build(mechanism, corner, table, project().mirror(), steeringDeclared);
        const std::optional<CornerSolver>& near = bare.left() ? bare.left() : bare.right();
        if (!near) continue;

        StaticAnglesAxle row;
        row.token = corner.token;
        row.label = bare.label().isEmpty() ? tr("Suspension") : bare.label();
        row.fromHardpoints = StaticAlignment{ near->designPose().camber, near->designPose().toe };
        row.source = near->wheelAttitude();
        row.sourcePoint = row.source == WheelAttitude::WheelAxis ? near->mechanism().wheelAxis
                                                                 : near->mechanism().contactPatch;
        row.stated = project().alignmentFor(corner.token);
        row.wheelCenter = near->designPose().wheelCenter;
        row.side = near->side();
        // The computed patch sits on the ground whatever the angles, so its
        // height is the ground's.
        row.groundZ = near->designPose().contactPatch.z;
        rows.push_back(row);
    }
    if (rows.empty()) return;

    StaticAnglesDialog dialog(rows, this);
    if (dialog.exec() != QDialog::Accepted) return;

    // Every axle the dialog showed is replaced; an axle it could not show --
    // one that does not solve today -- keeps whatever the project said about
    // it, rather than losing it to a table that is half way through an edit.
    QHash<QString, StaticAlignment> alignment = project().alignment();
    for (const StaticAnglesAxle& row : rows) alignment.remove(row.token);
    const QHash<QString, StaticAlignment> chosen = dialog.alignment();
    for (auto it = chosen.begin(); it != chosen.end(); ++it) alignment.insert(it.key(), it.value());
    if (alignment == project().alignment()) return;

    project().setAlignment(alignment);
    m_session->resolveMechanism();
    markDirty();
    // A step of its own: Generate from Design writes these too, so they are in
    // every state, and an angle changed without a step would be put back by
    // the next undo of anything.
    m_session->recordEdit(tr("Set static camber and toe"));
    statusBar()->showMessage(tr("Static camber and toe saved with the project."), 5000);
}

void MainWindow::importLinkageTemplateDialog()
{
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Import Linkage Template"), project().rootPath(), linkageTemplateFileFilter());
    if (path.isEmpty()) return;

    SessionMessage problem;
    const std::optional<QStringList> warnings = m_session->linkage().import(path, &problem);
    if (!warnings) {
        warn(problem);
        return;
    }

    statusBar()->showMessage(tr("%1 part(s) from %2")
                                 .arg(m_session->linkage().parts().parts.size())
                                 .arg(QFileInfo(path).fileName()),
                             6000);

    const QStringList notes = *warnings + m_session->linkage().parts().warnings;
    if (!notes.isEmpty()) {
        QMessageBox::information(this, tr("Imported with warnings"),
                                 notes.join(QStringLiteral("\n")));
    }
}

void MainWindow::resetLinkageTemplate()
{
    const QMessageBox::StandardButton answer = QMessageBox::question(
        this, tr("Reset the linkage template"),
        tr("Replace this project's linkage template with the one the application ships?\n\n"
           "Any changes made to the project's copy are lost."),
        QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
    if (answer != QMessageBox::Yes) return;

    SessionMessage problem;
    LinkageDocument& linkage = m_session->linkage();
    if (!linkage.installBuiltin(&problem) || !linkage.load(&problem)) {
        warn(problem);
        return;
    }
    statusBar()->showMessage(tr("Linkage template reset - %1 part(s)")
                                 .arg(m_session->linkage().parts().parts.size()),
                             6000);
}

// ---------------------------------------------------------------------------
// Wheels
// ---------------------------------------------------------------------------

void MainWindow::addWheelsDialog()
{
    if (hardpoints()->rowCount() == 0) return; // the action is disabled

    const WheelsRef& wheels = project().wheels();
    WheelDialog dialog(hardpoints()->table(), wheels.spec,
                       project().absolutePath(wheels.tyre.relativePath),
                       project().absolutePath(wheels.rim.relativePath),
                       geometryDialogDirectory(), this);
    if (dialog.exec() != QDialog::Accepted) return;

    const WheelSpec spec = dialog.spec();
    SessionMessage failure;
    QList<SessionMessage> problems;
    const bool applied =
        m_session->wheels().apply(spec, dialog.tyrePath(), dialog.rimPath(), &failure, &problems);
    for (const SessionMessage& problem : problems) warn(problem);
    if (!applied) {
        warn(failure);
        return;
    }

    // Read back out of the copies the project now holds, so what is on screen is
    // exactly what reopening it will show. That reads a file that was just read
    // to validate it, which a wheel is small enough for and which keeps one
    // function responsible for loading them.
    openWheels();

    QStringList warnings;
    const std::vector<WheelPlacement> placements =
        resolveWheels(spec, hardpoints()->table(), &warnings);
    statusBar()->showMessage(tr("%1 wheel(s) placed").arg(placements.size()), 6000);
    if (!warnings.isEmpty()) {
        QMessageBox::information(this, tr("Wheels added with warnings"),
                                 warnings.join(QStringLiteral("\n")));
    }
}

void MainWindow::removeWheels()
{
    if (project().wheels().isEmpty()) return;

    const QMessageBox::StandardButton answer = QMessageBox::question(
        this, tr("Remove wheels"),
        tr("Remove the wheels from this project?\n\nThe copies of the models inside the "
           "project folder are deleted. The files they were imported from are not touched."),
        QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
    if (answer != QMessageBox::Yes) return;

    m_session->wheels().remove();
    m_viewport->clearWheels();
    updateChrome();
}

// ---------------------------------------------------------------------------
// Analysis
// ---------------------------------------------------------------------------

void MainWindow::exportSweepCsv()
{
    const AxleSolver* axle = m_session->simulation().simulation().axleFor(m_analysisPanel->axle());
    if (!axle) {
        QMessageBox::information(this, tr("Export Sweep"),
                                 tr("There is no axle to sweep yet. Import hardpoints, and check "
                                    "that the linkage template names the mechanism."));
        return;
    }

    const SweepSpec spec = m_analysisPanel->spec();
    const SweepResult result = runSweep(*axle, spec);

    const QString suggested =
        QDir(project().rootPath())
            .filePath(QStringLiteral("%1-%2-%3.csv")
                          .arg(project().name().isEmpty() ? QStringLiteral("sweep")
                                                          : project().name(),
                               axle->label().isEmpty() ? axle->cornerToken() : axle->label(),
                               sweepKindToString(spec.kind)));
    const QString path = QFileDialog::getSaveFileName(this, tr("Export Sweep as CSV"), suggested,
                                                      tr("CSV files (*.csv);;All files (*)"));
    if (path.isEmpty()) return;

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate) || file.write(sweepToCsv(result)) < 0
        || !file.commit()) {
        QMessageBox::warning(this, tr("Export Sweep"),
                             tr("Could not write %1: %2")
                                 .arg(QDir::toNativeSeparators(path), file.errorString()));
        return;
    }
    statusBar()->showMessage(tr("Sweep written to %1").arg(QDir::toNativeSeparators(path)), 5000);
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

void MainWindow::updateWindowTitle()
{
    QStringList parts;
    parts << project().name();
    if (!project().geometry().isEmpty())
        parts << QFileInfo(project().geometry().relativePath).fileName();
    if (!project().hardpoints().isEmpty()) {
        const HardpointEdits pending = m_session->hardpoints().pendingEdits();
        parts << QFileInfo(project().hardpoints().workbook.relativePath).fileName()
                     + (pending.isEmpty() ? QString() : QStringLiteral("*"));
    }
    parts << tr("SuspensionKinematics");
    setWindowTitle(parts.join(QStringLiteral(" - ")));
}

void MainWindow::updateHardpointStatus()
{
    const int count = hardpoints()->rowCount();
    if (count == 0) {
        m_hardpointLabel->clear();
        return;
    }

    const HardpointEdits pending = m_session->hardpoints().pendingEdits();
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
    updateHardpointStatus();
    updateWindowTitle();
}

} // namespace suspkin
