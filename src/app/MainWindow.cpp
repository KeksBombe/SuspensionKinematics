#include "app/MainWindow.h"

#include "app/AnalysisPanel.h"
#include "app/CoordinateEntry.h"
#include "app/GenerateDialog.h"
#include "app/HardpointModel.h"
#include "app/HardpointPanel.h"
#include "app/Icons.h"
#include "app/LicensesDialog.h"
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
#include "app/UpdateChecker.h"
#include "app/WheelDialog.h"
#include "geom/MeshQuery.h"
#include "geom/MeshTopology.h"
#include "io/LinkageTemplate.h"
#include "io/MeshImport.h"
#include "model/HardpointGenerator.h"
#include "render/MoveGizmo.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QDir>
#include <QDockWidget>
#include <QFile>
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
#include <QProgressDialog>
#include <QPushButton>
#include <QQuaternion>
#include <QStandardPaths>
#include <QStatusBar>
#include <QSaveFile>
#include <QTextStream>
#include <QTimer>
#include <QUrl>

#include <algorithm>
#include <utility>

namespace suspkin {
namespace {

/// Where imported files are copied to inside a project.
const char kGeometrySubdirectory[] = "geometry";
const char kHardpointSubdirectory[] = "hardpoints";
/// Hardpoint edits live beside the workbook they modify, so a project folder
/// reads as what it is without a manifest to explain it.
const char kEditsRelativePath[] = "hardpoints/edits.json";
/// Where an imported template is copied to, next to everything else the project
/// owns a copy of.
const char kLinkageSubdirectory[] = "linkage";
/// The tyre and rim models, which together are a wheel. They share a directory,
/// so they are copied in under fixed names rather than their own: two files that
/// happen to be called the same would otherwise be one file.
const char kWheelSubdirectory[] = "wheels";
const char kTyreStem[] = "tyre";
const char kRimStem[] = "rim";

/// How long after the last change the project is written. Long enough that an
/// orbit drag is one save rather than two hundred, short enough that closing the
/// lid on a laptop a second later loses nothing.
constexpr int kAutoSaveDelayMs = 1200;
// Long enough that the window is up and the project loaded before the
// network is touched, short enough that the answer arrives while the user
// is still at the start of a session.
constexpr int kUpdateCheckDelayMs = 2500;

struct PresetSpec {
    ViewPreset preset;
    const char* label;
    const char* shortcut;
};

// Numbers 1-7 for the view presets; Ctrl+1..3 for display modes, so the two sets
// never collide.
constexpr PresetSpec kPresets[] = {
    { ViewPreset::Front,     "&Front",      "1" },
    { ViewPreset::Rear,      "&Rear",       "2" },
    { ViewPreset::Left,      "&Left",       "3" },
    { ViewPreset::Right,     "Rig&ht",      "4" },
    { ViewPreset::Top,       "&Top",        "5" },
    { ViewPreset::Bottom,    "&Bottom",     "6" },
    { ViewPreset::Isometric, "&Isometric",  "7" },
};

} // namespace

MainWindow::MainWindow(Project project, QWidget* parent)
    : QMainWindow(parent), m_project(std::move(project))
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

    // One timer for the whole window: everything that changes calls markDirty()
    // and the project is written once, shortly after the user stops.
    m_saveTimer = new QTimer(this);
    m_saveTimer->setSingleShot(true);
    m_saveTimer->setInterval(kAutoSaveDelayMs);
    connect(m_saveTimer, &QTimer::timeout, this, [this] { saveProject(); });

    // The docks before the actions: the panel toggles are actions on them.
    buildHardpointDock();
    buildAnalysisDock();
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
        if (m_saveTimer->isActive()) saveProject();
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
    m_hardpointModel = new HardpointModel(this);
    m_hardpointPanel = new HardpointPanel(m_hardpointModel, this);

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
    connect(m_hardpointModel, &HardpointModel::pointRenamed, this,
            [this](int row, const QString& from, const QString& to) {
                moveWheelsToRenamedPoints({ { from, to } });

                // Into the project before anything is resolved again: the
                // configuration table is refilled from the project's copy, and
                // that copy has to have the point under its new name already.
                captureHardpointConfig();
                captureMirrorProvenance();

                const QList<int> selection = m_viewport->selectedHardpoints();
                syncTableToViewport(false);
                selectRows(selection.isEmpty() ? QList<int>{ row } : selection, row);
                markDirty();
                recordEdit(tr("Rename %1 to %2").arg(from, to));
                statusBar()->showMessage(tr("Renamed %1 to %2").arg(from, to), 5000);
            });

    connect(m_hardpointModel, &HardpointModel::coordinateChanged, this, [this](int row) {
        const HardpointTable& table = m_hardpointModel->table();
        if (row < 0 || row >= static_cast<int>(table.points.size())) return;
        m_viewport->moveHardpoint(row, table.points[static_cast<std::size_t>(row)].toVector());
        // A coordinate is a link length, so the mechanism has to be measured
        // again. applySimulation() re-poses it and takes the wheels with it.
        rebuildSolvers();
        refreshSweep();
        applySimulation();
        markDirty();
        updateWindowTitle();
        // The table's cell, the arrows and the X, Y and Z field all arrive
        // here, so each of them is one step back.
        recordEdit(tr("Move %1").arg(table.points[static_cast<std::size_t>(row)].name));
    });

    // What a point is for is project state like everything else, so an accepted
    // edit is in the project before this lambda returns. The model has already
    // refused anything that could not mean something.
    connect(m_hardpointModel, &HardpointModel::configChanged, this, [this](int row) {
        captureHardpointConfig();
        // A point that has just become -- or stopped being -- a chassis pivot
        // changes what is drawn through it.
        syncGroundedPoints();
        markDirty();
        const HardpointTable& table = m_hardpointModel->table();
        if (row >= 0 && row < static_cast<int>(table.points.size()))
            recordEdit(tr("Configure %1").arg(table.points[static_cast<std::size_t>(row)].name));
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
                const HardpointTable& table = m_hardpointModel->table();
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
                const HardpointTable& table = m_hardpointModel->table();
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
    const HardpointTable& table = m_hardpointModel->table();
    if (row < 0 || row >= static_cast<int>(table.points.size())) return;

    QPointF anchor;
    if (!m_viewport->markerPosition(row, &anchor)) return; // off screen: nothing to open beside

    const Hardpoint& point = table.points[static_cast<std::size_t>(row)];
    m_entryRow = row;
    m_coordinateEntry->openAt(anchor, point.name, axis, point.coord[axis]);
}

void MainWindow::moveHardpointCoordinate(int row, int axis, double value)
{
    if (row < 0 || row >= m_hardpointModel->rowCount()) return;
    if (axis < 0 || axis >= 3) return;

    // Through the model, exactly as the table's own cell does it: what follows
    // -- the parts, the solve, the wheels, the edits file -- hangs off the
    // coordinateChanged() that this produces.
    const QModelIndex index = m_hardpointModel->index(row, HardpointModel::XColumn + axis);
    m_hardpointModel->setData(index, value, Qt::EditRole);
}

void MainWindow::showDragPosition(int row, int axis, double distance)
{
    const HardpointTable& table = m_hardpointModel->table();
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

EditState MainWindow::currentEditState() const
{
    return EditState{ m_hardpointModel->table(), m_hardpointModel->config(),
                      m_project.alignment() };
}

void MainWindow::recordEdit(const QString& label)
{
    m_history.record(label, currentEditState());
    // Undo and Redo say what they would do, so they follow every step.
    updateChrome();
}

void MainWindow::restartEditHistory()
{
    m_history.reset(currentEditState());
    updateChrome();
}

void MainWindow::restoreEditState(const EditState& from, const EditState& to)
{
    // Taken before the rows move under it.
    const QStringList selected = selectedPointNames();
    applyEditState(to);
    // What the step touched, so the user sees what came back. A step whose
    // points have all gone again -- an undone Add -- leaves the selection that
    // was there, less what went.
    const QStringList touched = touchedPoints(from, to);
    selectPointsNamed(touched.isEmpty() ? selected : touched);
}

void MainWindow::applyEditState(const EditState& state)
{
    // The field was typing into a point that may be about to move or go.
    m_coordinateEntry->dismiss();
    // Read off the table as it is on screen, which is what the wheels name.
    const QHash<QString, QString> renamed = renamedPoints(m_hardpointModel->table(), state.table);

    m_hardpointModel->setTable(state.table);
    m_hardpointModel->setConfig(state.config);
    m_project.setAlignment(state.alignment);
    moveWheelsToRenamedPoints(renamed);
    // Into the project before anything is resolved again: the configuration is
    // refilled from the project's copy, and a step that renamed or deleted a
    // point would otherwise be taken back by that refill.
    captureHardpointConfig();
    captureMirrorProvenance();
    syncTableToViewport(false);
    markDirty();
}

void MainWindow::moveWheelsToRenamedPoints(const QHash<QString, QString>& renamed)
{
    if (renamed.isEmpty()) return;

    WheelsRef wheels = m_project.wheels();
    bool moved = false;
    for (const WheelCorner corner : kWheelCorners) {
        const auto to = renamed.constFind(wheels.spec.point(corner));
        if (to == renamed.constEnd()) continue;
        wheels.spec.setPoint(corner, *to);
        moved = true;
    }
    if (moved) m_project.setWheels(wheels);
}

QStringList MainWindow::selectedPointNames() const
{
    const HardpointTable& table = m_hardpointModel->table();
    QStringList names;
    for (const int row : m_viewport->selectedHardpoints())
        if (row >= 0 && row < static_cast<int>(table.size()))
            names << table.points[static_cast<std::size_t>(row)].name;
    return names;
}

void MainWindow::selectPointsNamed(const QStringList& names)
{
    const HardpointTable& table = m_hardpointModel->table();
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
        refreshSweep();
        applySimulation();
        markDirty();
    });
    connect(m_analysisPanel, &AnalysisPanel::specChanged, this, [this] {
        refreshSweep();
        applySimulation();
        markDirty();
    });
    connect(m_analysisPanel, &AnalysisPanel::positionChanged, this, [this] {
        applySimulation();
        // A position that is moving thirty times a second is not worth writing
        // out thirty times a second. Stopping is what saves where it stopped.
        if (!m_analysisPanel->animating()) markDirty();
    });
    connect(m_analysisPanel, &AnalysisPanel::animatingChanged, this, [this] { markDirty(); });
    connect(m_analysisPanel, &AnalysisPanel::simulatingChanged, this, [this] {
        applySimulation();
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
        if (visible && m_sweep.isEmpty()) refreshSweep();
    });
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
    const QString path = m_project.absolutePath(m_project.linkageTemplate().relativePath);
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
    if (hardpointsOpen || m_hardpointModel->rowCount() > 0) m_hardpointDock->show();
    if (analysisOpen) m_analysisDock->show();
    markDirty();
    statusBar()->showMessage(tr("Panels docked where a new project has them."), 4000);
}

void MainWindow::refreshRecentProjectsMenu()
{
    if (!m_recentProjectsMenu) return;
    m_recentProjectsMenu->clear();

    const QString current = QFileInfo(m_project.manifestPath()).absoluteFilePath();
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
    m_loading = true;

    if (!m_project.window().geometry.isEmpty()) restoreGeometry(m_project.window().geometry);
    if (!m_project.window().dockState.isEmpty()) restoreState(m_project.window().dockState);
    // The ribbon is window layout too. A page the project names that this
    // build does not have -- a tab renamed since -- is the first page, and an
    // older project that names none opens there, expanded, with its menu bar.
    m_ribbon->setCurrentPage(m_project.window().ribbonPage);
    m_ribbon->setCollapsed(m_project.window().ribbonCollapsed);

    const bool hasGeometry = loadGeometryFromProject();
    const bool hasHardpoints = loadHardpointsFromProject();
    // After the points, because it is resolved against them, and unconditional
    // because a project should come back with its own template even when the
    // workbook it belongs to has gone missing.
    loadLinkageTemplateFromProject();
    // Also after the points: the wheels are pinned to four of them.
    const bool hasWheels = loadWheelsFromProject();

    applyViewState();
    // Nothing to restore a view onto, or nothing was saved: frame whatever the
    // project turned out to hold.
    if ((hasGeometry || hasHardpoints || hasWheels) && !m_project.view().cameraValid)
        m_viewport->fitToView();

    // restoreState() has already put the dock back where the user left it,
    // including closed -- so the only cases left are the two it cannot know
    // about: no hardpoints to show, and a project that has never been laid out.
    if (!hasHardpoints)
        m_hardpointDock->hide();
    else if (m_project.window().dockState.isEmpty())
        m_hardpointDock->show();

    updateHardpointStatus();
    updateWindowTitle();
    // After everything that fills the configuration in, so the state nothing
    // undoes past is the project as it opened -- not half of it.
    restartEditHistory();

    m_loading = false;
    RecentProjects::remember(m_project.manifestPath(), m_project.name());
}

bool MainWindow::loadGeometryFromProject()
{
    const AssetRef& asset = m_project.geometry();
    if (asset.isEmpty()) return false;

    const QString path = m_project.absolutePath(asset.relativePath);
    if (!QFileInfo::exists(path)) {
        // The project says it has geometry and the copy is gone: say so rather
        // than opening a window that quietly shows nothing.
        QMessageBox::warning(this, tr("Chassis missing"),
                             tr("This project's chassis file is not where the project says it "
                                "is:\n\n%1\n\nImport it again to restore it.")
                                 .arg(QDir::toNativeSeparators(path)));
        m_project.clearGeometry();
        markDirty();
        return false;
    }

    QApplication::setOverrideCursor(Qt::WaitCursor);
    MeshLoadResult result = importMeshFile(path);
    QApplication::restoreOverrideCursor();

    if (!result.ok()) {
        QMessageBox::warning(this, tr("Cannot open the project's chassis"),
                             tr("Failed to load:\n%1\n\n%2")
                                 .arg(QDir::toNativeSeparators(path), result.error));
        return false;
    }

    const EdgeSet edges = buildEdges(*result.mesh);
    const QLocale locale;
    m_meshLabel->setText(tr("%1  -  %2 triangles, %3 vertices, %4 edges  -  %5")
                             .arg(QFileInfo(path).fileName(),
                                  locale.toString(qulonglong(result.mesh->triangleCount())),
                                  locale.toString(qulonglong(result.mesh->vertexCount())),
                                  locale.toString(qulonglong(edges.allCount())),
                                  result.formatName));
    m_viewport->setMesh(std::move(*result.mesh), edges);
    m_chassisQuery.reset();
    updateChrome();
    return true;
}

bool MainWindow::loadHardpointsFromProject()
{
    const HardpointRef& reference = m_project.hardpoints();
    if (reference.isEmpty()) return false;

    const QString path = m_project.absolutePath(reference.workbook.relativePath);
    if (!QFileInfo::exists(path)) {
        QMessageBox::warning(this, tr("Workbook missing"),
                             tr("This project's hardpoint workbook is not where the project says "
                                "it is:\n\n%1\n\nImport it again to restore it.")
                                 .arg(QDir::toNativeSeparators(path)));
        m_project.clearHardpoints();
        markDirty();
        return false;
    }

    HardpointLoadResult result = readHardpointsXlsx(path);
    if (!result.ok()) {
        QMessageBox::warning(this, tr("Cannot read the project's hardpoints"),
                             tr("Failed to read:\n%1\n\n%2")
                                 .arg(QDir::toNativeSeparators(path), result.error));
        return false;
    }

    m_baseline = *result.table;
    m_hardpointSource = std::move(result.source);

    // The workbook is the baseline; what the user has actually been working on
    // is that plus whatever the edits file holds.
    QString error;
    const std::optional<HardpointEdits> edits =
        readHardpointEdits(m_project.absolutePath(QLatin1String(kEditsRelativePath)), &error);
    if (!edits) {
        QMessageBox::warning(this, tr("Cannot read the hardpoint edits"),
                             tr("The workbook was loaded, but the edits saved alongside it could "
                                "not be:\n\n%1")
                                 .arg(error));
        applyMirrorProvenance(m_baseline);
        setHardpointTable(m_baseline, false);
        return true;
    }

    applyMirrorProvenance(m_baseline);
    HardpointTable table = applyHardpointEdits(m_baseline, *edits);
    applyMirrorProvenance(table);
    setHardpointTable(std::move(table), false);
    return true;
}

void MainWindow::applyMirrorProvenance(HardpointTable& table) const
{
    const QHash<QString, QString>& mirrored = m_project.hardpoints().mirrored;
    for (Hardpoint& point : table.points)
        point.mirrorOf = mirrored.value(point.name);
}

void MainWindow::captureMirrorProvenance()
{
    HardpointRef reference = m_project.hardpoints();
    reference.mirrored.clear();
    for (const Hardpoint& point : m_hardpointModel->table().points) {
        if (point.isMirrored()) reference.mirrored.insert(point.name, point.mirrorOf);
    }
    m_project.setHardpoints(reference);
    // The baseline has to agree, or every mirrored point would read as edited.
    applyMirrorProvenance(m_baseline);
}

bool MainWindow::installBuiltinLinkageTemplate()
{
    const QByteArray bytes = builtinLinkageTemplateBytes();
    if (bytes.isEmpty()) return false;

    const QString relative = linkageTemplateRelativePath();
    QString error;
    if (!m_project.writeFile(relative, bytes, &error)) {
        QMessageBox::warning(this, tr("Cannot write the linkage template"),
                             tr("The parts between the hardpoints could not be set up:\n\n%1")
                                 .arg(error));
        return false;
    }

    AssetRef asset;
    asset.relativePath = relative;
    // No importedFrom: it did not come from anywhere on this machine.
    asset.importedAt = QDateTime::currentDateTimeUtc();
    m_project.setLinkageTemplate(asset);
    markDirty();
    return true;
}

bool MainWindow::loadLinkageTemplateFromProject()
{
    QString path = m_project.absolutePath(m_project.linkageTemplate().relativePath);

    // A template dropped into the project by hand, without the manifest being
    // edited to match, is adopted rather than overwritten: it is a file the user
    // put there on purpose, and the manifest is ours to fix, not theirs.
    if (path.isEmpty() || !QFileInfo::exists(path)) {
        const QString conventional = m_project.absolutePath(linkageTemplateRelativePath());
        if (QFileInfo::exists(conventional)) {
            AssetRef asset;
            asset.relativePath = linkageTemplateRelativePath();
            m_project.setLinkageTemplate(asset);
            path = conventional;
        }
    }

    // A project made before templates existed, or one whose copy was deleted,
    // gets the built-in one written into it. That is the whole of "the template
    // is saved in the project": from here on it is an ordinary project file the
    // user can open and edit.
    if (path.isEmpty() || !QFileInfo::exists(path)) {
        if (!installBuiltinLinkageTemplate()) return false;
        path = m_project.absolutePath(m_project.linkageTemplate().relativePath);
    }

    const LinkageTemplateLoadResult result = readLinkageTemplateFile(path);
    if (!result.ok()) {
        // Deliberately not repaired by overwriting: the file is the user's, and
        // silently replacing an edit they made would be worse than not drawing.
        QMessageBox::warning(this, tr("Cannot read the linkage template"),
                             tr("The parts between the hardpoints cannot be drawn:\n\n%1\n\n"
                                "Fix the file, or use Linkage > Reset to Built-in Template.")
                                 .arg(result.error));
        m_linkageTemplate = LinkageTemplate{};
        rebuildLinkage();
        return false;
    }

    m_linkageTemplate = *result.templ;
    adoptTemplateSteering();
    rebuildLinkage();
    return true;
}

void MainWindow::steeringDialog()
{
    if (m_linkageTemplate.isEmpty() || m_linkageTemplate.corners.empty()) return;

    SteeringDialog dialog(m_linkageTemplate, m_project.mirror(), m_hardpointModel->table(), this);
    if (dialog.exec() != QDialog::Accepted) return;

    const std::vector<CornerSpec> corners = dialog.corners();
    bool changed = corners.size() != m_linkageTemplate.corners.size();
    for (std::size_t i = 0; !changed && i < corners.size(); ++i) {
        changed = corners[i].steeringRack != m_linkageTemplate.corners[i].steeringRack
                  || corners[i].steeringStated != m_linkageTemplate.corners[i].steeringStated;
    }
    if (!changed) return;

    const bool written = patchLinkageTemplate(
        [&corners](const QByteArray& bytes, QString* error) {
            return setTemplateSteering(bytes, corners, error);
        },
        tr("The steering could not be saved."));
    if (written) statusBar()->showMessage(tr("Steering written to the linkage template."), 5000);
}

void MainWindow::staticAnglesDialog()
{
    const MechanismTemplate& mechanism = m_linkageTemplate.mechanism;
    const HardpointTable& table = m_hardpointModel->table();
    if (mechanism.isEmpty() || table.isEmpty()) return;

    std::vector<CornerSpec> corners = m_linkageTemplate.corners;
    if (corners.empty()) corners.push_back(CornerSpec{});
    const bool steeringDeclared = m_linkageTemplate.steeringDeclared();

    std::vector<StaticAnglesAxle> rows;
    for (const CornerSpec& corner : corners) {
        // Built without the project's angles, which is the only way to find out
        // what the hardpoints would say on their own -- the numbers an axle
        // goes back to, and the ones a newly ticked axle starts from.
        const AxleSolver bare =
            AxleSolver::build(mechanism, corner, table, m_project.mirror(), steeringDeclared);
        const std::optional<CornerSolver>& near = bare.left() ? bare.left() : bare.right();
        if (!near) continue;

        StaticAnglesAxle row;
        row.token = corner.token;
        row.label = bare.label().isEmpty() ? tr("Suspension") : bare.label();
        row.fromHardpoints = StaticAlignment{ near->designPose().camber, near->designPose().toe };
        row.source = near->wheelAttitude();
        row.sourcePoint = row.source == WheelAttitude::WheelAxis ? near->mechanism().wheelAxis
                                                                 : near->mechanism().contactPatch;
        row.stated = m_project.alignmentFor(corner.token);
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
    QHash<QString, StaticAlignment> alignment = m_project.alignment();
    for (const StaticAnglesAxle& row : rows) alignment.remove(row.token);
    const QHash<QString, StaticAlignment> chosen = dialog.alignment();
    for (auto it = chosen.begin(); it != chosen.end(); ++it) alignment.insert(it.key(), it.value());
    if (alignment == m_project.alignment()) return;

    m_project.setAlignment(alignment);
    rebuildSolvers();
    refreshSweep();
    applySimulation();
    markDirty();
    // A step of its own: Generate from Design writes these too, so they are in
    // every state, and an angle changed without a step would be put back by
    // the next undo of anything.
    recordEdit(tr("Set static camber and toe"));
    statusBar()->showMessage(tr("Static camber and toe saved with the project."), 5000);
}

bool MainWindow::patchLinkageTemplate(
    const std::function<QByteArray(const QByteArray&, QString*)>& patch, const QString& failure)
{
    const QString relative = m_project.linkageTemplate().relativePath;
    QString error;
    QByteArray patched;
    // Patched rather than rewritten, the same way a workbook is: this file is
    // the user's, and anything in it this version does not model -- a note, a
    // part, a key from a later release -- has to come out the other side.
    // Read through readFile(), which has closed it again before the write
    // below tries to replace it: Windows will not replace an open file.
    if (const std::optional<QByteArray> bytes = m_project.readFile(relative, &error))
        patched = patch(*bytes, &error);
    if (patched.isEmpty() || !m_project.writeFile(relative, patched, &error)) {
        QMessageBox::warning(this, tr("Cannot write the linkage template"),
                             QStringLiteral("%1\n\n%2")
                                 .arg(failure, error.isEmpty() ? tr("The template could not be read.")
                                                               : error));
        return false;
    }

    // Read back rather than patched in memory, so what the solver sees is what
    // the file says -- the same re-read Overwrite Workbook does, and for the
    // same reason.
    loadLinkageTemplateFromProject();
    markDirty();
    return true;
}

void MainWindow::adoptTemplateSteering()
{
    m_steeringNote.clear();
    if (m_linkageTemplate.isEmpty() || m_linkageTemplate.steeringDeclared()) return;

    // A template written before steering was a role says nothing about it, and
    // every axle then steers -- which is how a rear toe link ends up being
    // dragged sideways by a rack the car has not got.
    //
    // If the file is recognisably the built-in template, the answer is known and
    // is written in. If it is somebody's own, nothing is touched: the same rule
    // the reader already follows for a template it cannot parse.
    const LinkageTemplate builtin = builtinLinkageTemplate();
    bool recognised = !builtin.corners.empty() && !builtin.mechanism.tieRodInboard.isEmpty()
                      && m_linkageTemplate.mechanism.tieRodInboard
                             == builtin.mechanism.tieRodInboard
                      && m_linkageTemplate.corners.size() == builtin.corners.size();
    std::vector<CornerSpec> corners = m_linkageTemplate.corners;
    if (recognised) {
        for (CornerSpec& corner : corners) {
            const auto match = std::find_if(builtin.corners.begin(), builtin.corners.end(),
                                            [&corner](const CornerSpec& known) {
                                                return known.token == corner.token;
                                            });
            if (match == builtin.corners.end()) {
                recognised = false;
                break;
            }
            corner.steeringRack = match->steeringRack;
        }
    }

    const QString unstated = tr("This project's template does not say where the steering rack is, "
                                "so every axle can be steered. Linkage > Steering Rack says which "
                                "axle has one.");
    if (!recognised) {
        m_steeringNote = unstated;
        return;
    }

    const QString relative = m_project.linkageTemplate().relativePath;
    QString error;
    QByteArray patched;
    // Through readFile(), which closes the file before the write replaces it.
    if (const std::optional<QByteArray> bytes = m_project.readFile(relative, &error))
        patched = setTemplateSteering(*bytes, corners, &error);
    if (patched.isEmpty() || !m_project.writeFile(relative, patched, &error)) {
        // Not worth a dialog: the project still works, it just still says
        // nothing about steering.
        m_steeringNote = unstated;
        return;
    }

    m_linkageTemplate.corners = corners;
    QStringList steered;
    for (const CornerSpec& corner : corners) {
        if (!corner.steeringRack.isEmpty())
            steered << (corner.label.isEmpty() ? corner.token : corner.label);
    }
    m_steeringNote = tr("This project's template did not say where the steering rack is, so the "
                        "built-in answer was written into it: %1. Linkage > Steering Rack "
                        "changes it.")
                         .arg(steered.isEmpty() ? tr("none") : steered.join(QStringLiteral(", ")));
    markDirty();
}

void MainWindow::rebuildLinkage()
{
    m_linkage = buildLinkage(m_linkageTemplate, m_hardpointModel->table(), m_project.mirror());
    m_viewport->setLinkage(m_linkage);

    // The configuration table is resolved against the same two things the parts
    // are, so whatever changed here changed that too.
    refreshHardpointConfig();
    // And it is what says which of those parts' segments run from the car to
    // itself, so it reaches the viewport in the same breath.
    syncGroundedPoints();

    // The mechanism is bound to the same two things the parts are -- this
    // template and this table -- so it is rebound in the same breath. Doing it
    // here rather than at each call site is what keeps it independent of the
    // order a project happens to load its pieces in: the points are read before
    // the template, so binding at the point the table arrives would bind against
    // a template that is not there yet.
    rebuildSolvers();
    refreshSweep();
    // Which also places the wheels, posed or not.
    applySimulation();

    // updateChrome() ends by refreshing the status line, which is where the
    // part count is shown.
    updateChrome();
}

void MainWindow::refreshHardpointConfig()
{
    // What the Part columns may name comes from the template: a project that
    // describes a different car offers that car's bodies.
    m_hardpointModel->setBodyCatalog(bodyCatalog(m_linkageTemplate));

    HardpointConfigMap config = m_project.hardpoints().config;
    const int filled = fillMissingConfig(
        config,
        inferHardpointConfig(m_hardpointModel->table(), m_linkageTemplate, m_project.mirror()));
    m_hardpointModel->setConfig(config);
    if (filled == 0) return;

    // What was inferred is the user's from the moment it is in front of them --
    // they are the ones who will correct it -- so it is saved like any other
    // edit rather than worked out again on every open.
    captureHardpointConfig();
    markDirty();
}

void MainWindow::syncGroundedPoints()
{
    m_viewport->setGroundedPoints(
        groundedPoints(m_hardpointModel->table(), m_hardpointModel->config()));
}

void MainWindow::captureHardpointConfig()
{
    HardpointRef reference = m_project.hardpoints();
    if (reference.config == m_hardpointModel->config()) return;
    reference.config = m_hardpointModel->config();
    m_project.setHardpoints(reference);
}

void MainWindow::importLinkageTemplateDialog()
{
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Import Linkage Template"), m_project.rootPath(), linkageTemplateFileFilter());
    if (path.isEmpty()) return;

    // Read before copying: a file that is not a template should not land in the
    // project and replace the one that works.
    const LinkageTemplateLoadResult result = readLinkageTemplateFile(path);
    if (!result.ok()) {
        QMessageBox::warning(this, tr("Cannot import the linkage template"),
                             tr("Failed to read:\n%1\n\n%2")
                                 .arg(QDir::toNativeSeparators(path), result.error));
        return;
    }

    QString error;
    const std::optional<AssetRef> asset =
        m_project.importAsset(path, QLatin1String(kLinkageSubdirectory), &error);
    if (!asset) {
        QMessageBox::warning(this, tr("Cannot copy into the project"),
                             tr("The template was read, but could not be copied into the "
                                "project:\n\n%1")
                                 .arg(error));
        return;
    }

    m_project.setLinkageTemplate(*asset);
    m_linkageTemplate = *result.templ;
    rebuildLinkage();
    markDirty();

    statusBar()->showMessage(tr("%1 part(s) from %2")
                                 .arg(m_linkage.parts.size())
                                 .arg(QFileInfo(path).fileName()),
                             6000);

    const QStringList notes = result.warnings + m_linkage.warnings;
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

    if (!installBuiltinLinkageTemplate()) return;
    if (!loadLinkageTemplateFromProject()) return;
    statusBar()->showMessage(tr("Linkage template reset - %1 part(s)")
                                 .arg(m_linkage.parts.size()),
                             6000);
}

void MainWindow::applyViewState()
{
    const ViewState& view = m_project.view();
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
        if (row >= 0 && row < m_hardpointModel->rowCount()) selection.append(row);
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
    refreshSweep();
    applySimulation();
    // Last, because starting it turns the simulation on and moves the model,
    // and both of those have to be settled first.
    m_analysisPanel->setAnimating(simulation.animating && simulation.active);
}

// ---------------------------------------------------------------------------
// Saving the project
// ---------------------------------------------------------------------------

void MainWindow::markDirty()
{
    if (m_loading) return;
    m_saveTimer->start();
}

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

    m_project.setView(view);

    WindowState window;
    window.geometry = saveGeometry();
    window.dockState = saveState();
    window.ribbonPage = m_ribbon->currentPage();
    window.ribbonCollapsed = m_ribbon->collapsed();
    m_project.setWindow(window);
}

HardpointEdits MainWindow::pendingEdits() const
{
    if (m_project.hardpoints().isEmpty()) return {};
    return diffHardpoints(m_baseline, m_hardpointModel->table());
}

bool MainWindow::saveProject()
{
    m_saveTimer->stop();
    collectViewState();

    QString error;
    bool ok = true;

    // The edits file first: if the manifest lands and the edits do not, the
    // project would come back claiming there is nothing to write to the workbook.
    if (!m_project.hardpoints().isEmpty()) {
        if (!writeHardpointEdits(m_project.absolutePath(QLatin1String(kEditsRelativePath)),
                                 pendingEdits(), &error)) {
            ok = false;
        }
    }
    if (ok && !m_project.save(&error)) ok = false;

    if (!ok) {
        QMessageBox::warning(this, tr("Cannot save the project"),
                             tr("The project could not be written.\n\n%1\n\nThe work is still "
                                "here; fix the problem and save again.")
                                 .arg(error));
    }
    return ok;
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
        == QFileInfo(m_project.manifestPath()).absoluteFilePath()) {
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
    QDesktopServices::openUrl(QUrl::fromLocalFile(m_project.rootPath()));
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
    if (!m_project.lastGeometryDirectory().isEmpty()) return m_project.lastGeometryDirectory();
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
    // Read it before copying it in: a file that cannot be loaded has no business
    // being written into the project.
    QApplication::setOverrideCursor(Qt::WaitCursor);
    MeshLoadResult result = importMeshFile(path);
    QApplication::restoreOverrideCursor();

    if (!result.ok()) {
        QMessageBox::warning(this, tr("Cannot import file"),
                             tr("Failed to load:\n%1\n\n%2")
                                 .arg(QDir::toNativeSeparators(path), result.error));
        return; // whatever was on screen stays there
    }

    QString error;
    const std::optional<AssetRef> asset =
        m_project.importAsset(path, QLatin1String(kGeometrySubdirectory), &error);
    if (!asset) {
        QMessageBox::warning(this, tr("Cannot copy into the project"),
                             tr("The chassis loaded, but could not be copied into the "
                                "project:\n\n%1")
                                 .arg(error));
        return;
    }

    const EdgeSet edges = buildEdges(*result.mesh);
    const QLocale locale;
    const QString summary =
        tr("%1  -  %2 triangles, %3 vertices, %4 edges  -  %5, %6 ms")
            .arg(QFileInfo(path).fileName(),
                 locale.toString(qulonglong(result.mesh->triangleCount())),
                 locale.toString(qulonglong(result.mesh->vertexCount())),
                 locale.toString(qulonglong(edges.allCount())),
                 result.formatName)
            .arg(result.elapsedMs);

    m_viewport->setMesh(std::move(*result.mesh), edges);
    m_chassisQuery.reset(); // it was a query of the geometry that has just gone
    m_viewport->fitToView();

    m_meshLabel->setText(result.skippedDegenerate > 0
                             ? tr("%1  [%2 degenerate skipped]")
                                   .arg(summary).arg(result.skippedDegenerate)
                             : summary);

    updateChrome();
    m_project.setGeometry(*asset);
    m_project.setLastGeometryDirectory(QFileInfo(path).absolutePath());
    updateWindowTitle();
    markDirty();
}

void MainWindow::removeChassis()
{
    const AssetRef asset = m_project.geometry();
    if (!asset.isEmpty()) {
        const QMessageBox::StandardButton answer = QMessageBox::question(
            this, tr("Remove chassis"),
            tr("Remove %1 from this project?\n\nThe copy inside the project folder is deleted. "
               "The file it was imported from is not touched.")
                .arg(QFileInfo(asset.relativePath).fileName()),
            QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
        if (answer != QMessageBox::Yes) return;
        QFile::remove(m_project.absolutePath(asset.relativePath));
    }

    m_viewport->clearMesh();
    m_chassisQuery.reset();
    updateChrome();
    m_meshLabel->setText(tr("No chassis imported"));
    m_project.clearGeometry();
    updateWindowTitle();
    markDirty();
}

// ---------------------------------------------------------------------------
// Hardpoints
// ---------------------------------------------------------------------------

QString MainWindow::hardpointDialogDirectory() const
{
    if (!m_project.lastHardpointDirectory().isEmpty()) return m_project.lastHardpointDirectory();
    if (!m_project.lastGeometryDirectory().isEmpty()) return m_project.lastGeometryDirectory();
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
    const HardpointEdits pending = pendingEdits();
    if (!pending.isEmpty()) {
        const QMessageBox::StandardButton answer = QMessageBox::question(
            this, tr("Replace the hardpoints?"),
            tr("This project is holding %1 hardpoint change(s) that are not in its workbook "
               "yet.\n\nImporting a different workbook discards them. Continue?")
                .arg(pending.count()),
            QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
        if (answer != QMessageBox::Yes) return;
    }

    QApplication::setOverrideCursor(Qt::WaitCursor);
    HardpointLoadResult result = readHardpointsXlsx(path);
    QApplication::restoreOverrideCursor();

    if (!result.ok()) {
        QMessageBox::warning(this, tr("Cannot import hardpoints"),
                             tr("Failed to read:\n%1\n\n%2")
                                 .arg(QDir::toNativeSeparators(path), result.error));
        return; // whatever was loaded stays loaded
    }

    QString error;
    const std::optional<AssetRef> asset =
        m_project.importAsset(path, QLatin1String(kHardpointSubdirectory), &error);
    if (!asset) {
        QMessageBox::warning(this, tr("Cannot copy into the project"),
                             tr("The workbook was read, but could not be copied into the "
                                "project:\n\n%1")
                                 .arg(error));
        return;
    }

    // A new workbook is a new set of points, so nothing about the old one --
    // including which of them were mirrors -- carries over.
    HardpointRef reference;
    reference.workbook = *asset;
    reference.sheetName = result.source.sheetName;
    m_project.setHardpoints(reference);
    m_project.setLastHardpointDirectory(QFileInfo(path).absolutePath());

    m_baseline = *result.table;
    m_hardpointSource = std::move(result.source);
    // A fresh import is its own baseline, so nothing is pending against it. A
    // stale edits file left behind here would be applied to the new workbook on
    // the next open, which is why the failure is worth reporting.
    if (!writeHardpointEdits(m_project.absolutePath(QLatin1String(kEditsRelativePath)),
                             HardpointEdits{}, &error)) {
        QMessageBox::warning(this, tr("Cannot clear the old edits"),
                             tr("The workbook was imported, but the previous edits file could "
                                "not be removed:\n\n%1")
                                 .arg(error));
    }

    setHardpointTable(m_baseline, !m_viewport->hasMesh());
    // An import is not an edit: the steps that led to the old points lead
    // nowhere in these.
    restartEditHistory();
    m_hardpointDock->show();
    m_hardpointDock->raise();
    markDirty();

    statusBar()->showMessage(tr("Imported %1 hardpoints from %2 in %3 ms")
                                 .arg(m_hardpointModel->rowCount())
                                 .arg(QFileInfo(path).fileName())
                                 .arg(result.elapsedMs),
                             6000);

    if (!result.warnings.isEmpty()) {
        QMessageBox::information(this, tr("Imported with warnings"),
                                 tr("%1 hardpoints were imported.\n\n%2")
                                     .arg(m_hardpointModel->rowCount())
                                     .arg(result.warnings.join(QStringLiteral("\n\n"))));
    }
}

void MainWindow::setHardpointTable(HardpointTable table, bool refit)
{
    m_hardpointModel->setTable(std::move(table));
    syncTableToViewport(refit);
}

void MainWindow::syncTableToViewport(bool refit)
{
    m_viewport->setHardpoints(m_hardpointModel->table());
    // setHardpoints() drops the parts, because they are indices into the table
    // that has just been replaced. Resolving them again is what puts them back.
    // Which rebinds the mechanism and places the wheels as well: all three are
    // resolved against the table that has just been replaced.
    rebuildLinkage();
    // Only reframe when the hardpoints are all there is. With a mesh on screen
    // the user has already chosen a view, and moving it would be rude.
    if (refit) m_viewport->fitToView();
    updateChrome(); // which refreshes the status line too
    updateWindowTitle();
}

void MainWindow::mirrorHardpointsDialog()
{
    if (m_hardpointModel->rowCount() == 0) return;

    MirrorDialog dialog(m_hardpointModel->table(), m_viewport->selectedHardpoints(),
                        m_project.mirror(), this);
    if (dialog.exec() != QDialog::Accepted) return;

    const MirrorSpec spec = dialog.spec();
    const MirrorOutcome outcome = mirrorHardpoints(m_hardpointModel->table(), dialog.rows(), spec);

    // The rule is remembered whether or not it changed anything: it is the
    // user's convention, and they will want it again next time.
    m_project.setMirror(spec);

    if (outcome.added == 0 && outcome.updated == 0) {
        QMessageBox::information(this, tr("Nothing to mirror"),
                                 outcome.notes.isEmpty()
                                     ? tr("No hardpoints were mirrored.")
                                     : outcome.notes.join(QStringLiteral("\n\n")));
        markDirty();
        return;
    }

    setHardpointTable(outcome.table, false);
    captureMirrorProvenance();
    markDirty();
    recordEdit(tr("Mirror %n point(s)", "", outcome.added + outcome.updated));

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
    const QString path = m_project.absolutePath(m_project.hardpoints().workbook.relativePath);
    if (path.isEmpty()) return false;

    // Every point deleted is a workbook with no table in it, which the reader
    // cannot open again -- and the project would lose its workbook with it.
    if (m_hardpointModel->rowCount() == 0) {
        QMessageBox::information(this, tr("Overwrite Workbook"),
                                 tr("Every point has been deleted, so the workbook would be left "
                                    "with nothing in it that can be read back.\n\nTo take the "
                                    "hardpoints out of the project, use Remove Hardpoints."));
        return false;
    }

    const HardpointEdits pending = pendingEdits();
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

    if (!writeHardpointsTo(path)) return false;

    // Re-read, so the workbook the project holds is the baseline again and the
    // cell map covers the rows that were just appended. Without this a second
    // overwrite would append the mirrored points a second time.
    HardpointLoadResult reloaded = readHardpointsXlsx(path);
    if (!reloaded.ok()) {
        // The write succeeded, so the user's work is on disk; what failed is
        // adopting it as the new baseline. Leaving the edits pending is the safe
        // half of that, and saying so is better than a silent inconsistency.
        QMessageBox::warning(this, tr("Workbook written, but not re-read"),
                             tr("%1 was written, but reading it back failed:\n\n%2\n\nThe "
                                "changes are still listed as pending. Reopen the project before "
                                "writing it again.")
                                 .arg(QFileInfo(path).fileName(), reloaded.error));
        return true;
    }

    m_baseline = *reloaded.table;
    m_hardpointSource = std::move(reloaded.source);
    HardpointRef reference = m_project.hardpoints();
    reference.sheetName = m_hardpointSource.sheetName;
    m_project.setHardpoints(reference);
    // The mirrored points are ordinary rows in the workbook now; only the
    // project remembers that is what they are.
    applyMirrorProvenance(m_baseline);
    setHardpointTable(m_baseline, false);

    saveProject(); // clears the edits file, now that they are in the workbook
    statusBar()->showMessage(tr("Wrote the hardpoints into %1").arg(QFileInfo(path).fileName()),
                             6000);
    return true;
}

bool MainWindow::exportWorkbookAs()
{
    // Default to where the workbook came from: exporting back over the original
    // is the common case, and this makes it one click without assuming it.
    QString suggestion = m_project.hardpoints().workbook.originalPath;
    if (suggestion.isEmpty()) {
        QString name = QFileInfo(m_project.hardpoints().workbook.relativePath).fileName();
        if (name.isEmpty()) name = QStringLiteral("hardpoints.xlsx");
        suggestion = QDir(hardpointDialogDirectory()).filePath(name);
    }

    QString path = QFileDialog::getSaveFileName(this, tr("Export hardpoint workbook"), suggestion,
                                                hardpointFileFilter());
    if (path.isEmpty()) return false;
    if (QFileInfo(path).suffix().isEmpty()) path += QStringLiteral(".xlsx");

    if (!writeHardpointsTo(path)) return false;

    m_project.setLastHardpointDirectory(QFileInfo(path).absolutePath());
    markDirty();
    statusBar()->showMessage(tr("Exported %1 hardpoints to %2")
                                 .arg(m_hardpointModel->rowCount())
                                 .arg(QFileInfo(path).fileName()),
                             6000);
    return true;
}

bool MainWindow::writeHardpointsTo(const QString& path)
{
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const QString error = writeHardpointsXlsx(path, m_hardpointModel->table(), m_hardpointSource);
    QApplication::restoreOverrideCursor();

    if (!error.isEmpty()) {
        QMessageBox::warning(this, tr("Cannot write the workbook"),
                             tr("Failed to write:\n%1\n\n%2")
                                 .arg(QDir::toNativeSeparators(path), error));
        return false;
    }
    return true;
}

void MainWindow::removeHardpoints()
{
    const HardpointEdits pending = pendingEdits();
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

    const QString workbook = m_project.absolutePath(m_project.hardpoints().workbook.relativePath);
    if (!workbook.isEmpty()) QFile::remove(workbook);
    QFile::remove(m_project.absolutePath(QLatin1String(kEditsRelativePath)));

    m_hardpointModel->clear();
    m_hardpointSource = XlsxHardpointSource{};
    m_baseline = HardpointTable{};
    m_project.clearHardpoints();

    m_viewport->clearHardpoints();
    // The template stays: it describes a kind of car, not this workbook, and
    // the next import should find it already there.
    rebuildLinkage();
    // So do the wheel models, for the same reason. With no points to pin them
    // to there is nowhere to draw them, which is what this leaves behind.
    rebuildWheels();
    m_hardpointDock->hide();
    updateHardpointStatus();
    // The workbook and the edits file are deleted, which no step can put back.
    restartEditHistory();
    updateWindowTitle();
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
    QString error;
    const std::optional<AssetRef> asset = m_project.createHardpointWorkbook(table, &error);
    if (!asset) {
        QMessageBox::warning(this, tr("Cannot make a workbook"),
                             tr("The points could not be written into a workbook inside the "
                                "project:\n\n%1")
                                 .arg(error));
        return false;
    }

    // Written, then read -- never the other way round. What comes back is the
    // baseline, and the cell map that lets the next overwrite patch these rows
    // rather than append them again.
    const QString path = m_project.absolutePath(asset->relativePath);
    HardpointLoadResult result = readHardpointsXlsx(path);
    if (!result.ok()) {
        QMessageBox::warning(this, tr("Cannot read the new workbook"),
                             tr("The workbook was written, but reading it back failed:\n\n%1")
                                 .arg(result.error));
        QFile::remove(path);
        return false;
    }

    HardpointRef reference;
    reference.workbook = *asset;
    reference.sheetName = result.source.sheetName;
    // The workbook cannot say which of its points are mirrors; the project
    // remembers, so a later mirror pass does not mirror them again.
    for (const Hardpoint& point : table.points)
        if (point.isMirrored()) reference.mirrored.insert(point.name, point.mirrorOf);
    m_project.setHardpoints(reference);

    m_baseline = *result.table;
    m_hardpointSource = std::move(result.source);
    // Nothing is pending against a workbook that was just written, and an edits
    // file left from an earlier workbook would be applied to this one.
    writeHardpointEdits(m_project.absolutePath(QLatin1String(kEditsRelativePath)), HardpointEdits{},
                        &error);

    applyMirrorProvenance(m_baseline);
    setHardpointTable(m_baseline, !m_viewport->hasMesh());
    // The table begins here. Undoing past it would need the workbook that was
    // just made to be unmade, which is Remove Hardpoints, not a step.
    restartEditHistory();
    m_hardpointDock->show();
    m_hardpointDock->raise();
    markDirty();
    return true;
}

void MainWindow::addPointDialog()
{
    const HardpointTable& table = m_hardpointModel->table();
    const int current = m_viewport->selectedHardpoint();

    // Seeded from the selection, so the new point starts where the one being
    // worked on is -- with a name that is free.
    Hardpoint seed;
    seed.name = QStringLiteral("P1");
    if (current >= 0 && current < static_cast<int>(table.size())) {
        seed = table.points[static_cast<std::size_t>(current)];
        seed.mirrorOf.clear();
    }

    const bool creating = m_project.hardpoints().isEmpty();
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
    const int row = current >= 0 ? current + 1 : m_hardpointModel->rowCount();
    if (!m_hardpointModel->insertPoint(row, point)) return;
    syncTableToViewport(false);
    selectRows({ row }, row);
    markDirty();
    updateWindowTitle();
    recordEdit(tr("Add %1").arg(point.name));
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
                  m_hardpointModel->table().points[static_cast<std::size_t>(rows.front())].name)
            : tr("Delete %n point(s)", "", rows.size());

    const int first = *std::min_element(rows.begin(), rows.end());
    m_hardpointModel->removePoints(std::vector<int>(rows.begin(), rows.end()));
    // The model has dropped their configuration; the project, which the table
    // is refilled from, has to agree before anything is resolved again.
    captureHardpointConfig();
    captureMirrorProvenance();
    syncTableToViewport(false);

    const int next = std::min(first, m_hardpointModel->rowCount() - 1);
    if (next >= 0) selectRows({ next }, next);
    markDirty();
    updateWindowTitle();
    recordEdit(label);
    statusBar()->showMessage(tr("Deleted %n point(s). Ctrl+Z brings them back.", "", rows.size()),
                             6000);
}

void MainWindow::renamePointDialog()
{
    const int row = m_viewport->selectedHardpoint();
    const HardpointTable& table = m_hardpointModel->table();
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
    m_hardpointModel->renamePoint(row, proposed);
}

const MeshQuery* MainWindow::chassisQuery()
{
    if (!m_viewport->hasMesh()) return nullptr;
    if (!m_chassisQuery) {
        QApplication::setOverrideCursor(Qt::WaitCursor);
        m_chassisQuery = std::make_unique<MeshQuery>(m_viewport->mesh());
        QApplication::restoreOverrideCursor();
    }
    return m_chassisQuery.get();
}

DesignParameters MainWindow::initialDesign() const
{
    DesignParameters design;
    const std::vector<CornerSpec>& corners = m_linkageTemplate.corners;

    // This project's own corners: the first is taken to be the front and the
    // second the rear, which is how every template written so far lists them.
    const bool declared = m_linkageTemplate.steeringDeclared();
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
        const MechanismTemplate names = instantiateMechanism(m_linkageTemplate.mechanism,
                                                             corners.front().token, false,
                                                             m_project.mirror());
        if (const Hardpoint* centre = m_hardpointModel->table().find(names.wheelCenter))
            design.side = centre->y() < 0.0 ? DesignSide::Right : DesignSide::Left;
    }
    return design;
}

void MainWindow::generateFromDesignDialog()
{
    if (!m_linkageTemplate.canSimulate() || m_linkageTemplate.corners.empty()) return;

    const DesignParameters seed = m_project.design().value_or(initialDesign());
    GenerateDialog dialog(seed, m_linkageTemplate, m_project.mirror(), m_hardpointModel->table(),
                          m_baseline, m_viewport->hasMesh(), [this] { return chassisQuery(); },
                          this);
    const int answer = dialog.exec();

    // The targets are the user's work whether or not anything was generated
    // from them: reopening the dialog starts where they left it.
    if (!m_project.design() || *m_project.design() != dialog.parameters()) {
        m_project.setDesign(dialog.parameters());
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
    QHash<QString, StaticAlignment> alignment = m_project.alignment();
    for (const AxleDesign* axle : { &generated.front, &generated.rear }) {
        if (axle->generate && alignment.contains(axle->corner))
            alignment.insert(axle->corner, StaticAlignment{ axle->camber, axle->toe });
    }
    m_project.setAlignment(alignment);

    // The points. A project with a workbook takes them as edits against it,
    // like any other change; one without gets a workbook made for them.
    if (m_project.hardpoints().isEmpty()) {
        if (!adoptNewWorkbook(plan.table)) return;
    } else {
        setHardpointTable(plan.table, false);
        captureMirrorProvenance();
    }

    // Which axle has a rack, into the template, patched like any other edit
    // of it. Read back afterwards, which resolves the parts and the solve
    // against the new points as well.
    if (plan.steeringChanged) {
        const std::vector<CornerSpec> steering = plan.steering;
        patchLinkageTemplate(
            [&steering](const QByteArray& bytes, QString* error) {
                return setTemplateSteering(bytes, steering, error);
            },
            tr("The points were generated, but the steering could not be written into the "
               "linkage template."));
    }

    markDirty();
    updateWindowTitle();
    // The points and the angles, one step. The steering written into the
    // template is not in it: the template is not an edit of the points. In a
    // project that had no workbook this records nothing -- the table began
    // with these points.
    recordEdit(tr("Generate from design"));
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
    if (rows.size() < 2 || m_linkageTemplate.isEmpty()) return;

    // In the order they were picked: that is the order the chain is drawn in.
    const HardpointTable& table = m_hardpointModel->table();
    QStringList names;
    for (const int row : rows) names << table.points[static_cast<std::size_t>(row)].name;

    NewPartDialog dialog(m_linkageTemplate, names, this);
    if (dialog.exec() != QDialog::Accepted) return;
    const PartTemplate part = dialog.part();

    const bool written = patchLinkageTemplate(
        [&part](const QByteArray& bytes, QString* error) { return addTemplatePart(bytes, part, error); },
        tr("The part could not be added."));
    if (written) statusBar()->showMessage(tr("Added the part \"%1\"").arg(part.label), 5000);
}

void MainWindow::editPartsDialog()
{
    if (m_linkageTemplate.isEmpty()) return;
    const LinkageTemplate before = m_linkageTemplate;

    EditPartsDialog dialog(before, this);
    if (dialog.exec() != QDialog::Accepted) return;
    const QHash<QString, QString> relabelled = dialog.relabelled();
    const QStringList removed = dialog.removed();
    if (relabelled.isEmpty() && removed.isEmpty()) return;

    const bool written = patchLinkageTemplate(
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
        tr("The parts could not be changed."));
    if (!written) return;

    // A part's label is also the name of the body the configuration table's
    // Part columns offer. A relabelled part takes the rows that named it along,
    // rather than leaving them all pointing at a body that is no longer there.
    HardpointConfigMap config = m_hardpointModel->config();
    const BodyCatalog catalog = bodyCatalog(m_linkageTemplate);
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
            m_history.renameBody(from, to);
        }
    }
    if (moved > 0) {
        m_hardpointModel->setConfig(config);
        captureHardpointConfig();
    }
    statusBar()->showMessage(tr("Parts updated in the linkage template"), 5000);
}

// ---------------------------------------------------------------------------
// Wheels
// ---------------------------------------------------------------------------

bool MainWindow::loadWheelsFromProject()
{
    WheelsRef wheels = m_project.wheels();
    if (wheels.isEmpty()) {
        m_viewport->clearWheels();
        m_wheelPlacements.clear();
        return false;
    }

    // The two models are read the same way, so they are read in a loop rather
    // than twice by hand.
    struct Slot {
        AssetRef* asset;
        TriMesh mesh;
        EdgeSet edges;
    };
    Slot models[2] = { { &wheels.tyre, {}, {} }, { &wheels.rim, {}, {} } };

    QStringList problems;
    bool referencesChanged = false;

    QApplication::setOverrideCursor(Qt::WaitCursor);
    for (Slot& slot : models) {
        if (slot.asset->isEmpty()) continue;

        const QString path = m_project.absolutePath(slot.asset->relativePath);
        if (!QFileInfo::exists(path)) {
            // The project claims a model whose copy has gone. Say so, and stop
            // claiming it, rather than opening a window that quietly shows less.
            problems << tr("%1 is not where the project says it is.")
                            .arg(QDir::toNativeSeparators(path));
            *slot.asset = AssetRef{};
            referencesChanged = true;
            continue;
        }

        MeshLoadResult result = importMeshFile(path);
        if (!result.ok()) {
            // The file is there and unreadable, which is a different problem:
            // the reference stays, so a fixed file comes back on the next open.
            problems << tr("%1: %2").arg(QFileInfo(path).fileName(), result.error);
            continue;
        }
        slot.edges = buildEdges(*result.mesh);
        slot.mesh = std::move(*result.mesh);
    }
    QApplication::restoreOverrideCursor();

    if (referencesChanged) {
        m_project.setWheels(wheels);
        markDirty();
    }
    if (!problems.isEmpty()) {
        QMessageBox::warning(this, tr("Cannot open the project's wheels"),
                             tr("The wheels could not be drawn as this project describes "
                                "them:\n\n%1\n\nUse Geometry > Add Wheels to set them up "
                                "again.")
                                 .arg(problems.join(QStringLiteral("\n"))));
    }

    m_viewport->setWheelModels(std::move(models[0].mesh), std::move(models[0].edges),
                               std::move(models[1].mesh), std::move(models[1].edges));
    rebuildWheels();
    return !m_wheelPlacements.empty();
}

void MainWindow::rebuildSolvers()
{
    if (!m_analysisPanel) {
        m_simulation = Simulation{};
        return;
    }

    m_simulation = Simulation::build(m_linkageTemplate, m_hardpointModel->table(),
                                     m_project.mirror(), m_project.alignment(), m_steeringNote);

    // What the panel puts in its axle box. The label is a display matter, so
    // the fallback for an axle the template did not name is chosen here rather
    // than in the core.
    QList<AxleEntry> entries;
    entries.reserve(static_cast<int>(m_simulation.axles().size()));
    for (const AxleSolver& axle : m_simulation.axles()) {
        entries.append(AxleEntry{ axle.cornerToken(),
                                  axle.label().isEmpty() ? tr("Suspension") : axle.label(),
                                  axle.isSteered() });
    }
    m_analysisPanel->setAxles(entries);
}

void MainWindow::refreshSweep()
{
    if (!m_analysisPanel) return;

    // A sweep is the most expensive thing this window does, and there is nothing
    // to draw a curve on while the dock is shut. It is run again when it opens.
    const bool wanted = !m_analysisDock || m_analysisDock->isVisible();
    const AxleSolver* axle = m_simulation.axleFor(m_analysisPanel->axle());
    m_sweep = (wanted && axle) ? runSweep(*axle, m_analysisPanel->spec()) : SweepResult{};
    m_analysisPanel->setResult(m_sweep);

    QStringList lines;
    if (!m_simulation.note().isEmpty()) lines << m_simulation.note();
    // runSweep already carries the axle's own warnings, so they are not added
    // here a second time.
    lines += m_sweep.warnings;
    m_analysisPanel->setStatus(lines.join(QStringLiteral("\n")));
}

void MainWindow::applySimulation()
{
    if (!m_analysisPanel) return;

    m_pose = SimulationPose{};
    if (m_analysisPanel->simulating()) {
        const SweepSpec spec = m_analysisPanel->spec();
        m_pose = m_simulation.poseAt(m_analysisPanel->axle(), spec.kind,
                                     m_analysisPanel->position(), spec.rackTravel,
                                     m_analysisPanel->movesAllAxles());
        if (!m_pose.isEmpty()) m_analysisPanel->setReadout(m_pose.samples.front(), spec.kind);
    }
    if (m_pose.isEmpty()) m_analysisPanel->clearReadout();

    // Posed markers are not where the table has them, so the arrows come off:
    // a drag would be writing a design coordinate read off a simulated one.
    m_viewport->setPointEditingEnabled(m_pose.isEmpty());
    if (!m_pose.isEmpty()) m_coordinateEntry->dismiss();

    m_viewport->setMeshTransform(m_pose.bodyMotion ? m_pose.bodyMotion->toMatrix() : QMatrix4x4());

    // The table itself never moves. What the viewport is given is a copy of it
    // with the solved positions laid over the points the mechanism owns, so
    // nothing here can reach the edits file or a workbook.
    const HardpointTable table = m_pose.layOver(m_hardpointModel->table());
    std::vector<QVector3D> positions;
    positions.reserve(table.points.size());
    for (const Hardpoint& point : table.points) positions.push_back(point.toVector());
    m_viewport->setHardpointPositions(positions);

    // A wheel centre that the solver moved takes its wheel with it.
    rebuildWheels();
}

void MainWindow::exportSweepCsv()
{
    const AxleSolver* axle = m_simulation.axleFor(m_analysisPanel->axle());
    if (!axle) {
        QMessageBox::information(this, tr("Export Sweep"),
                                 tr("There is no axle to sweep yet. Import hardpoints, and check "
                                    "that the linkage template names the mechanism."));
        return;
    }

    const SweepSpec spec = m_analysisPanel->spec();
    const SweepResult result = runSweep(*axle, spec);

    const QString suggested =
        QDir(m_project.rootPath())
            .filePath(QStringLiteral("%1-%2-%3.csv")
                          .arg(m_project.name().isEmpty() ? QStringLiteral("sweep")
                                                          : m_project.name(),
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

void MainWindow::rebuildWheels()
{
    const WheelsRef& wheels = m_project.wheels();
    m_wheelPlacements =
        wheels.isEmpty() ? std::vector<WheelPlacement>{}
                         : resolveWheels(wheels.spec, m_pose.layOver(m_hardpointModel->table()));
    // A wheel is bolted to its upright, so it goes where the upright goes and
    // turns the way the upright turns. Without this the models slide about the
    // car on steering lock without ever pointing anywhere.
    orientWheels(m_wheelPlacements, m_pose.wheelRotations());
    // And a rolled body leans all four with it. The upright's turn is measured
    // in the body, so it comes first and the body's after it. Their centres are
    // already where the body put them: they came out of the pose.
    if (m_pose.bodyMotion) {
        const QQuaternion body = m_pose.bodyMotion->toQuaternion();
        for (WheelPlacement& placement : m_wheelPlacements)
            placement.rotation = body * placement.rotation;
    }
    m_viewport->setWheelPlacements(m_wheelPlacements, wheels.spec.alignToCenter);
    updateChrome(); // which refreshes the status line too
}

void MainWindow::addWheelsDialog()
{
    if (m_hardpointModel->rowCount() == 0) return; // the action is disabled

    const WheelsRef& wheels = m_project.wheels();
    WheelDialog dialog(m_hardpointModel->table(), wheels.spec,
                       m_project.absolutePath(wheels.tyre.relativePath),
                       m_project.absolutePath(wheels.rim.relativePath),
                       geometryDialogDirectory(), this);
    if (dialog.exec() != QDialog::Accepted) return;

    applyWheels(dialog.spec(), dialog.tyrePath(), dialog.rimPath());
}

void MainWindow::applyWheels(const WheelSpec& spec, const QString& tyrePath,
                             const QString& rimPath)
{
    WheelsRef wheels = m_project.wheels();
    wheels.spec = spec;

    struct Slot {
        AssetRef* asset;
        QString chosen;   ///< empty for "no model here"
        const char* stem; ///< what the copy inside the project is called
        bool replace = false;
    };
    Slot models[2] = { { &wheels.tyre, tyrePath, kTyreStem, false },
                      { &wheels.rim, rimPath, kRimStem, false } };

    // Read whatever is new before anything is copied or deleted: a file that
    // cannot be loaded has no business being written into the project, and one
    // bad model should not half-apply the other.
    QApplication::setOverrideCursor(Qt::WaitCursor);
    QString failedPath;
    QString failure;
    for (Slot& slot : models) {
        if (slot.chosen.isEmpty()) continue;
        // A path handed back unchanged is the project's own copy: keep it, and
        // keep the note of where it was originally imported from.
        const QString current = m_project.absolutePath(slot.asset->relativePath);
        if (!current.isEmpty()
            && QFileInfo(slot.chosen).absoluteFilePath()
                   == QFileInfo(current).absoluteFilePath()) {
            continue;
        }
        const MeshLoadResult result = importMeshFile(slot.chosen);
        if (!result.ok()) {
            failedPath = slot.chosen;
            failure = result.error;
            break;
        }
        slot.replace = true;
    }
    QApplication::restoreOverrideCursor();

    if (!failure.isEmpty()) {
        QMessageBox::warning(this, tr("Cannot import the tyre or rim model"),
                             tr("Failed to load:\n%1\n\n%2\n\nNothing was changed.")
                                 .arg(QDir::toNativeSeparators(failedPath), failure));
        return;
    }

    QString importedFrom;
    for (Slot& slot : models) {
        if (slot.chosen.isEmpty()) {
            // Cleared in the dialog: the copy inside the project goes with it.
            if (!slot.asset->isEmpty())
                QFile::remove(m_project.absolutePath(slot.asset->relativePath));
            *slot.asset = AssetRef{};
            continue;
        }
        if (!slot.replace) continue;

        const QString suffix = QFileInfo(slot.chosen).suffix();
        const QString target = suffix.isEmpty()
                                   ? QString::fromLatin1(slot.stem)
                                   : QStringLiteral("%1.%2").arg(QLatin1String(slot.stem), suffix);
        QString error;
        const std::optional<AssetRef> asset =
            m_project.importAssetAs(slot.chosen, QLatin1String(kWheelSubdirectory), target, &error);
        if (!asset) {
            // The model read, so this is a disk or permission problem. Whatever
            // else worked is kept rather than rolled back.
            QMessageBox::warning(this, tr("Cannot copy into the project"),
                                 tr("The model was read, but could not be copied into the "
                                    "project:\n\n%1")
                                     .arg(error));
            continue;
        }
        // A model in another format supersedes the previous copy, which would
        // otherwise sit in the project forever under its own extension.
        if (!slot.asset->isEmpty() && slot.asset->relativePath != asset->relativePath)
            QFile::remove(m_project.absolutePath(slot.asset->relativePath));
        *slot.asset = *asset;
        importedFrom = slot.chosen;
    }

    m_project.setWheels(wheels);
    // Only when something actually came in from outside: keeping a model the
    // project already had says nothing about where the user keeps their CAD.
    if (!importedFrom.isEmpty())
        m_project.setLastGeometryDirectory(QFileInfo(importedFrom).absolutePath());
    markDirty();

    // Read back out of the copies the project now holds, so what is on screen is
    // exactly what reopening it will show. That reads a file that was just read
    // to validate it, which a wheel is small enough for and which keeps one
    // function responsible for loading them.
    loadWheelsFromProject();

    QStringList warnings;
    const std::vector<WheelPlacement> placements =
        resolveWheels(spec, m_hardpointModel->table(), &warnings);
    statusBar()->showMessage(tr("%1 wheel(s) placed").arg(placements.size()), 6000);
    if (!warnings.isEmpty()) {
        QMessageBox::information(this, tr("Wheels added with warnings"),
                                 warnings.join(QStringLiteral("\n")));
    }
}

void MainWindow::removeWheels()
{
    const WheelsRef wheels = m_project.wheels();
    if (wheels.isEmpty()) return;

    const QMessageBox::StandardButton answer = QMessageBox::question(
        this, tr("Remove wheels"),
        tr("Remove the wheels from this project?\n\nThe copies of the models inside the "
           "project folder are deleted. The files they were imported from are not touched."),
        QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
    if (answer != QMessageBox::Yes) return;

    for (const AssetRef* asset : { &wheels.tyre, &wheels.rim }) {
        if (!asset->isEmpty()) QFile::remove(m_project.absolutePath(asset->relativePath));
    }

    m_project.clearWheels();
    m_viewport->clearWheels();
    m_wheelPlacements.clear();
    updateChrome();
    markDirty();
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
    parts << m_project.name();
    if (!m_project.geometry().isEmpty())
        parts << QFileInfo(m_project.geometry().relativePath).fileName();
    if (!m_project.hardpoints().isEmpty()) {
        const HardpointEdits pending = pendingEdits();
        parts << QFileInfo(m_project.hardpoints().workbook.relativePath).fileName()
                     + (pending.isEmpty() ? QString() : QStringLiteral("*"));
    }
    parts << tr("SuspensionKinematics");
    setWindowTitle(parts.join(QStringLiteral(" - ")));
}

void MainWindow::updateHardpointStatus()
{
    const int count = m_hardpointModel->rowCount();
    if (count == 0) {
        m_hardpointLabel->clear();
        return;
    }

    const HardpointEdits pending = pendingEdits();
    QString text = m_project.hardpoints().sheetName.isEmpty()
                       ? tr("%1 hardpoints").arg(count)
                       : tr("%1 hardpoints - %2").arg(count).arg(m_project.hardpoints().sheetName);
    if (!m_linkage.isEmpty())
        text += tr("  -  %1 parts").arg(m_linkage.parts.size());
    if (!m_wheelPlacements.empty())
        text += tr("  -  %1 wheels").arg(m_wheelPlacements.size());
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
    updateHardpointStatus();
}

} // namespace suspkin
