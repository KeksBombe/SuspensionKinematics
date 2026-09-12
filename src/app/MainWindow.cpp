#include "app/MainWindow.h"

#include "app/AnalysisPanel.h"
#include "app/GenerateDialog.h"
#include "app/HardpointModel.h"
#include "app/HardpointPanel.h"
#include "app/MirrorDialog.h"
#include "app/PartDialogs.h"
#include "app/PointDialog.h"
#include "app/ProjectLauncher.h"
#include "app/RecentProjects.h"
#include "app/StaticAnglesDialog.h"
#include "app/SteeringDialog.h"
#include "app/UpdateChecker.h"
#include "app/WheelDialog.h"
#include "geom/MeshQuery.h"
#include "geom/MeshTopology.h"
#include "io/LinkageTemplate.h"
#include "io/MeshImport.h"
#include "model/HardpointGenerator.h"

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
#include <QMatrix3x3>
#include <QMatrix4x4>
#include <QMenuBar>
#include <QMessageBox>
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

/// The turn a rigid motion makes, for the renderer. Rigid keeps its rotation as
/// rows -- r[i] dotted with a point gives component i of the answer -- which is
/// a row-major matrix, and that is what QMatrix3x3 reads.
QQuaternion rotationOf(const Rigid& motion)
{
    const float values[9] = {
        static_cast<float>(motion.r[0].x), static_cast<float>(motion.r[0].y),
        static_cast<float>(motion.r[0].z), static_cast<float>(motion.r[1].x),
        static_cast<float>(motion.r[1].y), static_cast<float>(motion.r[1].z),
        static_cast<float>(motion.r[2].x), static_cast<float>(motion.r[2].y),
        static_cast<float>(motion.r[2].z),
    };
    return QQuaternion::fromRotationMatrix(QMatrix3x3(values));
}

/// The whole of it, turn and translation, as a model matrix. QMatrix4x4 takes
/// its values a row at a time as well.
QMatrix4x4 matrixOf(const Rigid& motion)
{
    return QMatrix4x4(
        static_cast<float>(motion.r[0].x), static_cast<float>(motion.r[0].y),
        static_cast<float>(motion.r[0].z), static_cast<float>(motion.t.x),
        static_cast<float>(motion.r[1].x), static_cast<float>(motion.r[1].y),
        static_cast<float>(motion.r[1].z), static_cast<float>(motion.t.y),
        static_cast<float>(motion.r[2].x), static_cast<float>(motion.r[2].y),
        static_cast<float>(motion.r[2].z), static_cast<float>(motion.t.z),
        0.0f, 0.0f, 0.0f, 1.0f);
}

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

    buildActions();
    buildHardpointDock();
    buildAnalysisDock();
    buildMenus();

    // The overlay buttons and the menu entries are two views of one mode.
    connect(m_viewport, &ViewportWidget::displayModeChanged, this, [this](DisplayMode mode) {
        (mode == DisplayMode::Solid ? m_solidAction : m_trianglesAction)->setChecked(true);
    });
    connect(m_viewport, &ViewportWidget::viewChanged, this, [this] { markDirty(); });

    // Closing the window is the usual way out, but not the only one: --screenshot
    // and a session manager both end the run without one. A pending debounced
    // save has to survive those too.
    connect(qApp, &QCoreApplication::aboutToQuit, this, [this] {
        if (m_saveTimer->isActive()) saveProject();
    });

    resize(1280, 820);
    openProjectContents();

    // After the project is in, so a slow or failing network never delays the
    // window being usable.
    setUpdateCheckerUp();
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
                updateActionState();
            });
    connect(m_hardpointPanel, &HardpointPanel::selectionChanged, this,
            [this](const QList<int>& rows, int current) {
                m_viewport->setSelectedHardpoints(rows, current);
                updateActionState();
            });

    // A rename changes what everything resolved by name finds: the parts, the
    // solve, the wheels. The model has already moved the point's configuration
    // and its mirrors' provenance; the project is told, and the rest resolved
    // again.
    connect(m_hardpointModel, &HardpointModel::pointRenamed, this,
            [this](int row, const QString& from, const QString& to) {
                WheelsRef wheels = m_project.wheels();
                bool wheelsChanged = false;
                for (const WheelCorner corner : kWheelCorners) {
                    if (wheels.spec.point(corner) != from) continue;
                    wheels.spec.setPoint(corner, to);
                    wheelsChanged = true;
                }
                if (wheelsChanged) m_project.setWheels(wheels);

                // Into the project before anything is resolved again: the
                // configuration table is refilled from the project's copy, and
                // that copy has to have the point under its new name already.
                captureHardpointConfig();
                captureMirrorProvenance();

                const QList<int> selection = m_viewport->selectedHardpoints();
                syncTableToViewport(false);
                selectRows(selection.isEmpty() ? QList<int>{ row } : selection, row);
                markDirty();
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
        updateActionState();
    });

    // What a point is for is project state like everything else, so an accepted
    // edit is in the project before this lambda returns. The model has already
    // refused anything that could not mean something.
    connect(m_hardpointModel, &HardpointModel::configChanged, this, [this](int) {
        captureHardpointConfig();
        markDirty();
    });
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

void MainWindow::buildActions()
{
    m_newProjectAction = new QAction(tr("&New Project..."), this);
    m_newProjectAction->setShortcut(QKeySequence::New);
    connect(m_newProjectAction, &QAction::triggered, this, &MainWindow::newProject);

    m_openProjectAction = new QAction(tr("&Open Project..."), this);
    m_openProjectAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+O")));
    connect(m_openProjectAction, &QAction::triggered, this, &MainWindow::openProject);

    m_saveProjectAction = new QAction(tr("&Save Project"), this);
    m_saveProjectAction->setShortcut(QKeySequence::Save);
    m_saveProjectAction->setStatusTip(
        tr("The project saves itself as you work; this writes it out now."));
    connect(m_saveProjectAction, &QAction::triggered, this, [this] {
        if (saveProject()) statusBar()->showMessage(tr("Project saved"), 3000);
    });

    m_projectListAction = new QAction(tr("&Project List..."), this);
    connect(m_projectListAction, &QAction::triggered, this, [this] {
        saveProject();
        emit projectListRequested();
    });

    // "Chassis" rather than "geometry": the wheels are geometry too, and have
    // their own entries below. This is the car the suspension is bolted to.
    m_importAction = new QAction(tr("&Import Chassis..."), this);
    m_importAction->setShortcut(QKeySequence::Open);
    m_importAction->setStatusTip(
        tr("Bring in the chassis or monocoque as STEP or STL. It is copied into the project."));
    connect(m_importAction, &QAction::triggered, this, &MainWindow::importFileDialog);

    m_closeAction = new QAction(tr("&Remove Chassis"), this);
    m_closeAction->setShortcut(QKeySequence::Close);
    m_closeAction->setEnabled(false);
    connect(m_closeAction, &QAction::triggered, this, &MainWindow::closeModel);

    m_quitAction = new QAction(tr("&Quit"), this);
    m_quitAction->setShortcut(QKeySequence::Quit);
    connect(m_quitAction, &QAction::triggered, this, &QWidget::close);

    m_modeGroup = new QActionGroup(this);
    m_modeGroup->setExclusive(true);

    const auto addMode = [this](const QString& text, const QString& shortcut, DisplayMode mode) {
        auto* action = new QAction(text, this);
        action->setCheckable(true);
        action->setShortcut(QKeySequence(shortcut));
        m_modeGroup->addAction(action);
        connect(action, &QAction::triggered, this,
                [this, mode] { m_viewport->setDisplayMode(mode); });
        return action;
    };
    m_solidAction     = addMode(tr("&Solid"),     QStringLiteral("Ctrl+1"), DisplayMode::Solid);
    m_trianglesAction = addMode(tr("&Triangles"), QStringLiteral("Ctrl+2"), DisplayMode::Triangles);
    m_solidAction->setChecked(true); // matches ViewportWidget's default

    m_fitAction = new QAction(tr("&Fit to view"), this);
    m_fitAction->setShortcut(QKeySequence(QStringLiteral("F")));
    connect(m_fitAction, &QAction::triggered, this, [this] { m_viewport->fitToView(); });

    m_importHardpointsAction = new QAction(tr("&Import Hardpoints..."), this);
    m_importHardpointsAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+I")));
    connect(m_importHardpointsAction, &QAction::triggered, this,
            &MainWindow::importHardpointsDialog);

    m_mirrorAction = new QAction(tr("&Mirror Hardpoints..."), this);
    m_mirrorAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+M")));
    m_mirrorAction->setEnabled(false);
    connect(m_mirrorAction, &QAction::triggered, this, &MainWindow::mirrorHardpointsDialog);

    m_overwriteWorkbookAction = new QAction(tr("&Overwrite Workbook"), this);
    m_overwriteWorkbookAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+S")));
    m_overwriteWorkbookAction->setEnabled(false);
    connect(m_overwriteWorkbookAction, &QAction::triggered, this, &MainWindow::overwriteWorkbook);

    m_exportWorkbookAction = new QAction(tr("&Export Workbook As..."), this);
    m_exportWorkbookAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+E")));
    m_exportWorkbookAction->setEnabled(false);
    connect(m_exportWorkbookAction, &QAction::triggered, this, &MainWindow::exportWorkbookAs);

    m_closeHardpointsAction = new QAction(tr("&Remove Hardpoints"), this);
    m_closeHardpointsAction->setEnabled(false);
    connect(m_closeHardpointsAction, &QAction::triggered, this, &MainWindow::closeHardpoints);

    m_newTableAction = new QAction(tr("&New Hardpoint Table..."), this);
    m_newTableAction->setStatusTip(
        tr("Start a table of points here rather than importing one. The project gets a workbook "
           "of its own to hold them."));
    connect(m_newTableAction, &QAction::triggered, this, &MainWindow::addPointDialog);

    m_addPointAction = new QAction(tr("&Add Point..."), this);
    m_addPointAction->setShortcut(QKeySequence(Qt::Key_Insert));
    m_addPointAction->setStatusTip(tr("Add a point next to the selected one."));
    connect(m_addPointAction, &QAction::triggered, this, &MainWindow::addPointDialog);
    addAction(m_addPointAction);

    m_deletePointAction = new QAction(tr("&Delete Point"), this);
    m_deletePointAction->setShortcut(QKeySequence::Delete);
    m_deletePointAction->setEnabled(false);
    m_deletePointAction->setStatusTip(
        tr("Delete the selected points. A point the workbook holds stays in it until the "
           "workbook is overwritten."));
    connect(m_deletePointAction, &QAction::triggered, this, &MainWindow::deleteSelectedPoints);
    addAction(m_deletePointAction);

    m_renamePointAction = new QAction(tr("Re&name Point..."), this);
    m_renamePointAction->setEnabled(false);
    m_renamePointAction->setStatusTip(tr("Give the selected point a different name."));
    connect(m_renamePointAction, &QAction::triggered, this, &MainWindow::renamePointDialog);

    m_generateAction = new QAction(tr("&Generate from Design..."), this);
    m_generateAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+G")));
    m_generateAction->setStatusTip(
        tr("Work out the wishbones, the upright and the steering from vehicle targets: track, "
           "caster, roll centre, anti-dive and the rest."));
    connect(m_generateAction, &QAction::triggered, this, &MainWindow::generateFromDesignDialog);

    m_newPartAction = new QAction(tr("&New Part from Selection..."), this);
    m_newPartAction->setEnabled(false);
    m_newPartAction->setStatusTip(
        tr("Draw a part through the selected points, in the order they were picked."));
    connect(m_newPartAction, &QAction::triggered, this, &MainWindow::newPartFromSelection);

    m_editPartsAction = new QAction(tr("&Edit Parts..."), this);
    m_editPartsAction->setStatusTip(tr("Rename or delete the parts the template draws."));
    connect(m_editPartsAction, &QAction::triggered, this, &MainWindow::editPartsDialog);

    m_labelsAction = new QAction(tr("Show Hardpoint &Labels"), this);
    m_labelsAction->setCheckable(true);
    m_labelsAction->setChecked(true);
    m_labelsAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+L")));
    connect(m_labelsAction, &QAction::toggled, this,
            [this](bool on) { m_viewport->setHardpointLabelsVisible(on); });
    addAction(m_labelsAction);

    m_linksAction = new QAction(tr("Show &Parts"), this);
    m_linksAction->setCheckable(true);
    m_linksAction->setChecked(true);
    m_linksAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+P")));
    m_linksAction->setStatusTip(
        tr("Draw the wishbones, rods and bodies the linkage template describes."));
    connect(m_linksAction, &QAction::toggled, this,
            [this](bool on) { m_viewport->setLinkageVisible(on); });
    addAction(m_linksAction);

    m_importLinkageAction = new QAction(tr("&Import Template..."), this);
    m_importLinkageAction->setStatusTip(
        tr("Replace the rule that says which hardpoints are joined by which part."));
    connect(m_importLinkageAction, &QAction::triggered, this,
            &MainWindow::importLinkageTemplateDialog);

    m_resetLinkageAction = new QAction(tr("&Reset to Built-in Template"), this);
    connect(m_resetLinkageAction, &QAction::triggered, this, &MainWindow::resetLinkageTemplate);

    m_steeringAction = new QAction(tr("Steering &Rack..."), this);
    m_steeringAction->setStatusTip(
        tr("Say where the steering rack is attached: which axle has one, and which points it "
           "moves. An axle without a rack is not offered a steer sweep."));
    connect(m_steeringAction, &QAction::triggered, this, &MainWindow::steeringDialog);

    m_staticAnglesAction = new QAction(tr("Static &Camber and Toe..."), this);
    m_staticAnglesAction->setStatusTip(
        tr("Set each axle's static camber and toe as numbers, the way Lotus's Set Static Angles "
           "does. The wheel axis and the contact patch are computed from them."));
    connect(m_staticAnglesAction, &QAction::triggered, this, &MainWindow::staticAnglesDialog);

    // A wheel is the tyre and the rim together, which is why the menu entry
    // says "wheels" and the dialog asks for a tyre model and a rim model.
    m_addWheelsAction = new QAction(tr("Add &Wheels..."), this);
    m_addWheelsAction->setEnabled(false);
    m_addWheelsAction->setStatusTip(
        tr("Draw a tyre and a rim model at the four wheel centres."));
    connect(m_addWheelsAction, &QAction::triggered, this, &MainWindow::addWheelsDialog);

    m_removeWheelsAction = new QAction(tr("Remove Wh&eels"), this);
    m_removeWheelsAction->setEnabled(false);
    connect(m_removeWheelsAction, &QAction::triggered, this, &MainWindow::removeWheels);

    m_wheelsAction = new QAction(tr("Show &Wheels"), this);
    m_wheelsAction->setCheckable(true);
    m_wheelsAction->setChecked(true);
    m_wheelsAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+W")));
    connect(m_wheelsAction, &QAction::toggled, this,
            [this](bool on) { m_viewport->setWheelsVisible(on); });
    addAction(m_wheelsAction);
}

void MainWindow::buildMenus()
{
    QMenu* fileMenu = menuBar()->addMenu(tr("&File"));
    fileMenu->addAction(m_newProjectAction);
    fileMenu->addAction(m_openProjectAction);
    m_recentProjectsMenu = fileMenu->addMenu(tr("Open &Recent"));
    fileMenu->addSeparator();
    fileMenu->addAction(m_saveProjectAction);
    fileMenu->addAction(m_projectListAction);
    auto* revealAction = fileMenu->addAction(tr("Show Project &Folder"));
    connect(revealAction, &QAction::triggered, this, [this] {
        QDesktopServices::openUrl(QUrl::fromLocalFile(m_project.rootPath()));
    });
    fileMenu->addSeparator();
    fileMenu->addAction(m_quitAction);
    connect(fileMenu, &QMenu::aboutToShow, this, &MainWindow::refreshRecentProjectsMenu);
    refreshRecentProjectsMenu();

    QMenu* geometryMenu = menuBar()->addMenu(tr("&Geometry"));
    geometryMenu->addAction(m_importAction);
    geometryMenu->addAction(m_closeAction);
    geometryMenu->addSeparator();
    geometryMenu->addAction(m_addWheelsAction);
    geometryMenu->addAction(m_removeWheelsAction);
    geometryMenu->addSeparator();
    geometryMenu->addAction(m_solidAction);
    geometryMenu->addAction(m_trianglesAction);

    // The table's own toggle, first in the menu it belongs to. With it only
    // under View -- and called just "Hardpoints" there -- a table closed by its
    // dock's X looked gone for good.
    QAction* hardpointTableAction = m_hardpointDock->toggleViewAction();
    hardpointTableAction->setText(tr("Show Hardpoint &Table"));
    hardpointTableAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+H")));
    hardpointTableAction->setStatusTip(tr("Open or close the table of hardpoints."));

    QMenu* hardpointMenu = menuBar()->addMenu(tr("&Hardpoints"));
    hardpointMenu->addAction(hardpointTableAction);
    hardpointMenu->addSeparator();
    hardpointMenu->addAction(m_importHardpointsAction);
    hardpointMenu->addAction(m_newTableAction);
    hardpointMenu->addAction(m_generateAction);
    hardpointMenu->addSeparator();
    hardpointMenu->addAction(m_addPointAction);
    hardpointMenu->addAction(m_renamePointAction);
    hardpointMenu->addAction(m_deletePointAction);
    hardpointMenu->addAction(m_mirrorAction);
    hardpointMenu->addSeparator();
    hardpointMenu->addAction(m_overwriteWorkbookAction);
    hardpointMenu->addAction(m_exportWorkbookAction);
    hardpointMenu->addSeparator();
    hardpointMenu->addAction(m_closeHardpointsAction);

    // "Linkage", not "Parts": this is what joins the hardpoints -- the parts the
    // template draws, the template itself, and where the steering rack is. The
    // car's own parts, chassis and wheels, are under Geometry.
    QMenu* linkageMenu = menuBar()->addMenu(tr("&Linkage"));
    linkageMenu->addAction(m_linksAction);
    linkageMenu->addSeparator();
    linkageMenu->addAction(m_newPartAction);
    linkageMenu->addAction(m_editPartsAction);
    linkageMenu->addSeparator();
    linkageMenu->addAction(m_steeringAction);
    linkageMenu->addAction(m_staticAnglesAction);
    linkageMenu->addSeparator();
    linkageMenu->addAction(m_importLinkageAction);
    linkageMenu->addAction(m_resetLinkageAction);
    auto* revealTemplateAction = linkageMenu->addAction(tr("Show &Template File"));
    revealTemplateAction->setStatusTip(
        tr("Open the project's template in whatever edits JSON on this machine."));
    connect(revealTemplateAction, &QAction::triggered, this, [this] {
        const QString path = m_project.absolutePath(m_project.linkageTemplate().relativePath);
        if (!path.isEmpty() && QFileInfo::exists(path))
            QDesktopServices::openUrl(QUrl::fromLocalFile(path));
    });

    QMenu* analysisMenu = menuBar()->addMenu(tr("&Analysis"));
    analysisMenu->addAction(m_analysisDock->toggleViewAction());
    m_analysisDock->toggleViewAction()->setText(tr("Show &Analysis"));
    m_analysisDock->toggleViewAction()->setShortcut(QKeySequence(QStringLiteral("Ctrl+K")));
    auto* parametersAction = analysisMenu->addAction(tr("Sweep &Parameters..."));
    parametersAction->setStatusTip(
        tr("How far each sweep travels and how finely it is solved."));
    parametersAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+P")));
    connect(parametersAction, &QAction::triggered, m_analysisPanel,
            &AnalysisPanel::showParameters);
    analysisMenu->addSeparator();
    auto* exportSweepAction = analysisMenu->addAction(tr("Export Sweep as &CSV..."));
    exportSweepAction->setStatusTip(
        tr("Write the sweep on screen out as a spreadsheet, one row per position."));
    connect(exportSweepAction, &QAction::triggered, this, &MainWindow::exportSweepCsv);

    QMenu* viewMenu = menuBar()->addMenu(tr("&View"));
    viewMenu->addAction(m_fitAction);
    viewMenu->addSeparator();
    viewMenu->addAction(m_labelsAction);
    viewMenu->addAction(m_linksAction);
    viewMenu->addAction(m_wheelsAction);
    viewMenu->addAction(m_hardpointDock->toggleViewAction());
    viewMenu->addAction(m_analysisDock->toggleViewAction());
    viewMenu->addSeparator();

    for (const PresetSpec& spec : kPresets) {
        auto* action = new QAction(tr(spec.label), this);
        action->setShortcut(QKeySequence(QLatin1String(spec.shortcut)));
        const ViewPreset preset = spec.preset;
        connect(action, &QAction::triggered, this, [this, preset] { m_viewport->applyPreset(preset); });
        viewMenu->addAction(action);
        addAction(action); // keep the shortcut live even when the menu is closed
    }

    QMenu* helpMenu = menuBar()->addMenu(tr("&Help"));

    m_checkUpdatesAction = new QAction(tr("Check for &Updates..."), this);
    // Disabled rather than hidden for a portable or developer build, so the
    // absence is visible and the tooltip can say why.
    m_checkUpdatesAction->setEnabled(UpdateChecker::updatesSupported());
    if (!UpdateChecker::updatesSupported()) {
        m_checkUpdatesAction->setToolTip(
            tr("Only a copy put here by the installer can update itself."));
    }
    connect(m_checkUpdatesAction, &QAction::triggered, this, [this] {
        if (m_updates) m_updates->check(/*userAsked=*/true);
    });
    helpMenu->addAction(m_checkUpdatesAction);

    m_autoUpdateAction = new QAction(tr("Check for Updates on Start&up"), this);
    m_autoUpdateAction->setCheckable(true);
    m_autoUpdateAction->setChecked(UpdateChecker::checkOnStartup());
    m_autoUpdateAction->setEnabled(UpdateChecker::updatesSupported());
    connect(m_autoUpdateAction, &QAction::toggled, this,
            [](bool on) { UpdateChecker::setCheckOnStartup(on); });
    helpMenu->addAction(m_autoUpdateAction);

    helpMenu->addSeparator();
    helpMenu->addAction(tr("&About SuspensionKinematics"), this, [this] {
        const int build = UpdateChecker::currentBuild();
        const QString version =
            build > 0 ? tr("Version %1 (build %2)")
                            .arg(QCoreApplication::applicationVersion())
                            .arg(build)
                      : tr("Version %1 (local build)")
                            .arg(QCoreApplication::applicationVersion());
        QMessageBox::about(
            this, tr("About SuspensionKinematics"),
            tr("<h3>SuspensionKinematics</h3><p>%1</p>"
               "<p>Suspension kinematics for Bremergy: CAD geometry and a hardpoint "
               "workbook in one 3D viewport.</p>")
                .arg(version));
    });
}

void MainWindow::setUpdateCheckerUp()
{
    if (!UpdateChecker::updatesSupported()) return;

    m_updates = new UpdateChecker(this);
    connect(m_updates, &UpdateChecker::updateAvailable, this, &MainWindow::offerUpdate);
    connect(m_updates, &UpdateChecker::upToDate, this, [this] {
        QMessageBox::information(this, tr("No update"),
                                 tr("This is the newest build."));
    });
    connect(m_updates, &UpdateChecker::failed, this,
            [this](const QString& message, bool userAsked) {
                // A background check on a machine that is offline, or behind a
                // proxy that blocks GitHub, must not put a dialog in front of
                // someone who never asked about updates.
                if (userAsked) QMessageBox::warning(this, tr("Update"), message);
                else qInfo("Update check: %s", qPrintable(message));
            });

    if (!UpdateChecker::checkOnStartup()) return;
    // Not during construction: the window should be up and usable first, and a
    // project still opening should not compete with the network for attention.
    QTimer::singleShot(kUpdateCheckDelayMs, this,
                       [this] { m_updates->check(/*userAsked=*/false); });
}

void MainWindow::offerUpdate(const UpdateRelease& release, bool userAsked)
{
    QMessageBox box(this);
    box.setIcon(QMessageBox::Question);
    box.setWindowTitle(tr("Update available"));
    box.setText(tr("<b>Build %1 is available.</b>").arg(release.build));
    box.setInformativeText(
        tr("This copy is build %1. The update is about %2 MB and installs itself; "
           "the application restarts when it is done.")
            .arg(UpdateChecker::currentBuild())
            .arg(QString::number(release.assetSize / (1024.0 * 1024.0), 'f', 1)));

    QPushButton* update = box.addButton(tr("Update Now"), QMessageBox::AcceptRole);
    box.addButton(tr("Not Now"), QMessageBox::RejectRole);
    // Only worth offering when the application raised this by itself; asking
    // from the menu and being told "never mind this one" would be odd.
    QPushButton* skip =
        userAsked ? nullptr : box.addButton(tr("Skip This Build"), QMessageBox::DestructiveRole);
    box.setDefaultButton(update);
    box.exec();

    if (box.clickedButton() == skip) {
        UpdateChecker::skipBuild(release.build);
        return;
    }
    if (box.clickedButton() != update) return;

    // The project is written out before anything replaces the binary, so an
    // update can never be what loses someone's work.
    saveProject();

    auto* progress = new QProgressDialog(tr("Downloading the update..."), tr("Cancel"), 0,
                                         100, this);
    progress->setWindowTitle(tr("Update"));
    progress->setWindowModality(Qt::WindowModal);
    progress->setMinimumDuration(0);
    progress->setAutoClose(false);
    progress->setAutoReset(false);
    progress->setValue(0);

    connect(m_updates, &UpdateChecker::downloadProgress, progress,
            [progress](qint64 received, qint64 total) {
                if (total <= 0) return;
                progress->setMaximum(100);
                progress->setValue(static_cast<int>(received * 100 / total));
            });
    connect(m_updates, &UpdateChecker::installerStarted, progress, [this, progress] {
        progress->close();
        progress->deleteLater();
        // The installer is waiting for this process to let go of its files.
        QCoreApplication::quit();
    });
    connect(m_updates, &UpdateChecker::failed, progress, [progress] {
        progress->close();
        progress->deleteLater();
    });
    connect(progress, &QProgressDialog::canceled, progress, &QProgressDialog::close);

    m_updates->downloadAndInstall(release);
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
    updateActionState();
    updateWindowTitle();

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
    m_closeAction->setEnabled(true);
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

    // updateActionState() ends by refreshing the status line, which is where the
    // part count is shown.
    updateActionState();
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
    m_labelsAction->setChecked(view.labelsVisible);
    m_viewport->setHardpointLabelsVisible(view.labelsVisible);
    m_linksAction->setChecked(view.linksVisible);
    m_viewport->setLinkageVisible(view.linksVisible);
    m_wheelsAction->setChecked(view.wheelsVisible);
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

// ---------------------------------------------------------------------------
// Geometry
// ---------------------------------------------------------------------------

QString MainWindow::geometryDialogDirectory() const
{
    if (!m_project.lastGeometryDirectory().isEmpty()) return m_project.lastGeometryDirectory();
    return QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
}

void MainWindow::importFileDialog()
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

    m_closeAction->setEnabled(true);
    m_project.setGeometry(*asset);
    m_project.setLastGeometryDirectory(QFileInfo(path).absolutePath());
    updateWindowTitle();
    markDirty();
}

void MainWindow::closeModel()
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
    m_closeAction->setEnabled(false);
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
    updateActionState(); // which refreshes the status line too
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

void MainWindow::closeHardpoints()
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
    updateActionState();
    updateWindowTitle();
    markDirty();
}

void MainWindow::selectRows(const QList<int>& rows, int current)
{
    m_viewport->setSelectedHardpoints(rows, current);
    m_hardpointPanel->setSelectedRows(m_viewport->selectedHardpoints(),
                                      m_viewport->selectedHardpoint());
    updateActionState();
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
    statusBar()->showMessage(tr("Added %1").arg(point.name), 5000);
}

void MainWindow::deleteSelectedPoints()
{
    const QList<int> rows = m_viewport->selectedHardpoints();
    if (rows.isEmpty()) return;

    const HardpointTable& table = m_hardpointModel->table();
    QStringList names;
    for (const int row : rows) names << table.points[static_cast<std::size_t>(row)].name;
    std::sort(names.begin(), names.end());
    QString list = names.mid(0, 12).join(QStringLiteral(", "));
    if (names.size() > 12) list += tr(" and %1 more").arg(names.size() - 12);

    // There is no undo in this application, so a delete is asked about -- and
    // the answer says where a point the workbook holds can still be found.
    const QMessageBox::StandardButton answer = QMessageBox::question(
        this, tr("Delete points"),
        tr("Delete %n point(s)?\n\n%1\n\nA point the workbook holds stays in it until the "
           "workbook is overwritten; until then the project remembers it as deleted.",
           "", names.size())
            .arg(list),
        QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
    if (answer != QMessageBox::Yes) return;

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
    statusBar()->showMessage(tr("Deleted %n point(s)", "", names.size()), 5000);
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
            if (from != to && !catalog.contains(from)) moved += renameBody(config, from, to);
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
    m_axles.clear();
    m_solverNote.clear();
    if (!m_analysisPanel) return;

    const HardpointTable& table = m_hardpointModel->table();

    // A template written before the solver existed still draws perfectly well;
    // it simply does not say which point plays which role. The reader fills that
    // in from the built-in block so that everything reading a template sees the
    // same roles -- the solve here, and what each hardpoint is for in the
    // configuration table. All that is left to do is say so.
    const MechanismTemplate& mechanism = m_linkageTemplate.mechanism;
    if (m_linkageTemplate.mechanismAssumed) {
        m_solverNote = tr("This project's template does not say which hardpoint plays which role, "
                          "so the built-in mechanism is being used, here and in the hardpoint "
                          "table. Linkage > Reset to Built-in Template writes it into the file.");
    }
    if (!m_steeringNote.isEmpty()) {
        m_solverNote = m_solverNote.isEmpty()
                           ? m_steeringNote
                           : m_solverNote + QLatin1Char('\n') + m_steeringNote;
    }

    QList<AxleEntry> axles;
    if (!mechanism.isEmpty() && !table.isEmpty()) {
        std::vector<CornerSpec> corners = m_linkageTemplate.corners;
        // A template with no corners spells its point names out in full, which
        // is one axle rather than none.
        if (corners.empty()) corners.push_back(CornerSpec{});
        // Whether the template says anything at all about steering. It is asked
        // once, for the whole file: a template that says nothing leaves every
        // axle steered, which is what every project made before the role existed
        // has always done.
        const bool steeringDeclared = m_linkageTemplate.steeringDeclared();
        for (const CornerSpec& corner : corners) {
            // Static camber and toe the project states win over whatever the
            // table's wheel axis or contact patch would have said.
            AxleSolver axle = AxleSolver::build(mechanism, corner, table, m_project.mirror(),
                                                steeringDeclared,
                                                m_project.alignmentFor(corner.token));
            if (axle.isEmpty()) continue;
            const QString label = axle.label().isEmpty() ? tr("Suspension") : axle.label();
            axles.append(AxleEntry{ axle.cornerToken(), label, axle.isSteered() });
            m_axles.push_back(std::move(axle));
        }
        // Ackermann is measured against the far axle, which only the whole list
        // of them knows about.
        assignWheelbases(m_axles);
    }

    m_analysisPanel->setAxles(axles);
    if (axles.isEmpty())
        m_solverNote = table.isEmpty()
                           ? tr("Import hardpoints to simulate.")
                           : tr("No axle in this table resolves to a complete mechanism. The "
                                "solver needs both wishbones, the tie rod and a wheel centre.");
}

const AxleSolver* MainWindow::currentAxle() const
{
    if (m_axles.empty()) return nullptr;
    const QString token = m_analysisPanel ? m_analysisPanel->axle() : QString();
    for (const AxleSolver& axle : m_axles)
        if (axle.cornerToken() == token) return &axle;
    return &m_axles.front();
}

void MainWindow::refreshSweep()
{
    if (!m_analysisPanel) return;

    // A sweep is the most expensive thing this window does, and there is nothing
    // to draw a curve on while the dock is shut. It is run again when it opens.
    const bool wanted = !m_analysisDock || m_analysisDock->isVisible();
    const AxleSolver* axle = currentAxle();
    m_sweep = (wanted && axle) ? runSweep(*axle, m_analysisPanel->spec()) : SweepResult{};
    m_analysisPanel->setResult(m_sweep);

    QStringList lines;
    if (!m_solverNote.isEmpty()) lines << m_solverNote;
    // runSweep already carries the axle's own warnings, so they are not added
    // here a second time.
    lines += m_sweep.warnings;
    m_analysisPanel->setStatus(lines.join(QStringLiteral("\n")));
}

void MainWindow::applySimulation()
{
    if (!m_analysisPanel) return;

    m_poses.clear();
    m_bodyMotion.reset();

    const AxleSolver* selected = currentAxle();
    if (m_analysisPanel->simulating() && selected) {
        const SweepSpec spec = m_analysisPanel->spec();
        const double input = m_analysisPanel->position();
        m_poses.push_back(sampleAxleAt(*selected, spec.kind, input, spec.rackTravel));
        m_analysisPanel->setReadout(m_poses.front(), spec.kind);

        // The other axles ride along, so the car heaves and rolls as a car
        // rather than as one axle with the rest of it left behind. Steering is
        // the exception: a rack belongs to one axle, and pushing a rear toe link
        // with it would be inventing a rear-steer this car has not got.
        if (m_analysisPanel->movesAllAxles() && spec.kind != SweepKind::Steer) {
            for (const AxleSolver& axle : m_axles) {
                if (&axle == selected) continue;
                m_poses.push_back(sampleAxleAt(axle, spec.kind, input, spec.rackTravel));
            }
        }

        // The sweep has the road tilting under a car that stays put, which is
        // right for its numbers and wrong to look at: a car in a corner rolls
        // on a level road. So the whole of it -- monocoque, every point, every
        // wheel -- is drawn turned about the roll axis, which is what keeps
        // the tyres on the road where they were. Only when every axle is
        // following, though: a body cannot roll with an axle left behind, and
        // drawing it would put that axle's wheels through the road.
        if (spec.kind == SweepKind::Roll && m_poses.size() == m_axles.size())
            m_bodyMotion = bodyRollMotion(rollAxisThrough(m_axles), input);
    } else {
        m_analysisPanel->clearReadout();
    }
    m_viewport->setMeshTransform(m_bodyMotion ? matrixOf(*m_bodyMotion) : QMatrix4x4());

    // The table itself never moves. What the viewport is given is a copy of it
    // with the solved positions laid over the points the mechanism owns, so
    // nothing here can reach the edits file or a workbook.
    const HardpointTable table = posedTable();
    std::vector<QVector3D> positions;
    positions.reserve(table.points.size());
    for (const Hardpoint& point : table.points) positions.push_back(point.toVector());
    m_viewport->setHardpointPositions(positions);

    // A wheel centre that the solver moved takes its wheel with it.
    rebuildWheels();
}

HardpointTable MainWindow::posedTable() const
{
    HardpointTable table = m_hardpointModel->table();
    for (const AxleSample& sample : m_poses)
    for (const CornerPose* pose : { &sample.left, &sample.right }) {
        if (!pose->valid) continue;
        for (const PosedPoint& posed : pose->points) {
            const int index = table.indexOf(posed.name);
            if (index < 0) continue;
            Hardpoint& point = table.points[static_cast<std::size_t>(index)];
            point.coord[0] = posed.position.x;
            point.coord[1] = posed.position.y;
            point.coord[2] = posed.position.z;
        }
    }
    // Everything, not only what the solve moved: the chassis pickups are on the
    // body too, and the parts are drawn between the two.
    if (m_bodyMotion) {
        for (Hardpoint& point : table.points) {
            const Vec3 moved =
                m_bodyMotion->map(Vec3(point.coord[0], point.coord[1], point.coord[2]));
            point.coord[0] = moved.x;
            point.coord[1] = moved.y;
            point.coord[2] = moved.z;
        }
    }
    return table;
}

void MainWindow::exportSweepCsv()
{
    const AxleSolver* axle = currentAxle();
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

WheelRotations MainWindow::wheelRotations() const
{
    WheelRotations rotations;
    for (const AxleSample& sample : m_poses)
        for (const CornerPose* pose : { &sample.left, &sample.right }) {
            if (!pose->valid || pose->wheelCenterName.isEmpty()) continue;
            rotations.insert(pose->wheelCenterName, rotationOf(pose->uprightMotion));
        }
    return rotations;
}

void MainWindow::rebuildWheels()
{
    const WheelsRef& wheels = m_project.wheels();
    m_wheelPlacements = wheels.isEmpty() ? std::vector<WheelPlacement>{}
                                         : resolveWheels(wheels.spec, posedTable());
    // A wheel is bolted to its upright, so it goes where the upright goes and
    // turns the way the upright turns. Without this the models slide about the
    // car on steering lock without ever pointing anywhere.
    orientWheels(m_wheelPlacements, wheelRotations());
    // And a rolled body leans all four with it. The upright's turn is measured
    // in the body, so it comes first and the body's after it. Their centres are
    // already where the body put them: they came out of posedTable().
    if (m_bodyMotion) {
        const QQuaternion body = rotationOf(*m_bodyMotion);
        for (WheelPlacement& placement : m_wheelPlacements)
            placement.rotation = body * placement.rotation;
    }
    m_viewport->setWheelPlacements(m_wheelPlacements, wheels.spec.alignToCenter);
    updateActionState(); // which refreshes the status line too
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
    updateActionState();
    markDirty();
}

// ---------------------------------------------------------------------------
// Chrome
// ---------------------------------------------------------------------------

void MainWindow::setDisplayMode(DisplayMode mode)
{
    (mode == DisplayMode::Solid ? m_solidAction : m_trianglesAction)->setChecked(true);
    m_viewport->setDisplayMode(mode);
}

QImage MainWindow::captureViewport()
{
    return m_viewport->grabFramebuffer();
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

void MainWindow::updateActionState()
{
    const bool loaded = m_hardpointModel->rowCount() > 0;
    m_linksAction->setEnabled(!m_linkage.isEmpty());
    // A template with no corners has no axle to ask about, and one that failed
    // to load has nothing to write into.
    m_steeringAction->setEnabled(!m_linkageTemplate.isEmpty()
                                 && !m_linkageTemplate.corners.empty());
    // Angles are set on a wheel, so there has to be an axle that solves.
    m_staticAnglesAction->setEnabled(!m_axles.empty());
    m_wheelsAction->setEnabled(!m_wheelPlacements.empty());
    m_addWheelsAction->setEnabled(loaded);
    m_removeWheelsAction->setEnabled(!m_project.wheels().isEmpty());
    m_mirrorAction->setEnabled(loaded);
    m_closeHardpointsAction->setEnabled(loaded);

    // A project with no workbook starts one; one that has one adds to it.
    const bool hasWorkbook = !m_project.hardpoints().isEmpty();
    m_newTableAction->setEnabled(!hasWorkbook);
    const int selected = static_cast<int>(m_viewport->selectedHardpoints().size());
    m_deletePointAction->setEnabled(selected > 0);
    m_renamePointAction->setEnabled(m_viewport->selectedHardpoint() >= 0);
    // The generator names what it makes through the template's roles, and a
    // part is written into the template: both need one that loaded.
    m_generateAction->setEnabled(m_linkageTemplate.canSimulate()
                                 && !m_linkageTemplate.corners.empty());
    m_newPartAction->setEnabled(!m_linkageTemplate.isEmpty() && selected >= 2);
    m_editPartsAction->setEnabled(!m_linkageTemplate.isEmpty());
    m_exportWorkbookAction->setEnabled(loaded && m_hardpointSource.isValid());
    m_overwriteWorkbookAction->setEnabled(loaded && m_hardpointSource.isValid()
                                          && !m_project.hardpoints().isEmpty());
    updateHardpointStatus();
}

} // namespace suspkin
