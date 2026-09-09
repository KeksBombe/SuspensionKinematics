#include "app/MainWindow.h"

#include "app/HardpointModel.h"
#include "app/HardpointPanel.h"
#include "app/MirrorDialog.h"
#include "app/ProjectLauncher.h"
#include "app/RecentProjects.h"
#include "app/UpdateChecker.h"
#include "app/WheelDialog.h"
#include "geom/MeshTopology.h"
#include "io/LinkageTemplate.h"
#include "io/MeshImport.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QDir>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QImage>
#include <QLabel>
#include <QLocale>
#include <QMenuBar>
#include <QMessageBox>
#include <QProgressDialog>
#include <QPushButton>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTimer>
#include <QUrl>

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
/// The wheel and rim models. They share a directory, so they are copied in under
/// fixed names rather than their own: two files called "wheel.step" would
/// otherwise be one file.
const char kWheelSubdirectory[] = "wheels";
const char kWheelStem[] = "wheel";
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

    m_meshLabel = new QLabel(tr("No geometry imported"), this);
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
    // was just told, so the two cannot chase each other.
    connect(m_viewport, &ViewportWidget::hardpointClicked, m_hardpointPanel,
            &HardpointPanel::setSelectedRow);
    connect(m_hardpointPanel, &HardpointPanel::rowSelected, m_viewport,
            &ViewportWidget::setSelectedHardpoint);

    connect(m_hardpointModel, &HardpointModel::coordinateChanged, this, [this](int row) {
        const HardpointTable& table = m_hardpointModel->table();
        if (row < 0 || row >= static_cast<int>(table.points.size())) return;
        m_viewport->moveHardpoint(row, table.points[static_cast<std::size_t>(row)].toVector());
        // A wheel centre that is being typed takes its wheel with it.
        rebuildWheels();
        markDirty();
        updateWindowTitle();
        updateActionState();
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

    m_importAction = new QAction(tr("&Import Geometry..."), this);
    m_importAction->setShortcut(QKeySequence::Open);
    connect(m_importAction, &QAction::triggered, this, &MainWindow::importFileDialog);

    m_closeAction = new QAction(tr("&Remove Geometry"), this);
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

    m_addWheelsAction = new QAction(tr("Add &Wheels..."), this);
    m_addWheelsAction->setEnabled(false);
    m_addWheelsAction->setStatusTip(
        tr("Draw a wheel and a rim model at the four wheel centres."));
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

    QMenu* hardpointMenu = menuBar()->addMenu(tr("&Hardpoints"));
    hardpointMenu->addAction(m_importHardpointsAction);
    hardpointMenu->addAction(m_mirrorAction);
    hardpointMenu->addSeparator();
    hardpointMenu->addAction(m_overwriteWorkbookAction);
    hardpointMenu->addAction(m_exportWorkbookAction);
    hardpointMenu->addSeparator();
    hardpointMenu->addAction(m_closeHardpointsAction);

    QMenu* partsMenu = menuBar()->addMenu(tr("&Parts"));
    partsMenu->addAction(m_linksAction);
    partsMenu->addSeparator();
    partsMenu->addAction(m_importLinkageAction);
    partsMenu->addAction(m_resetLinkageAction);
    auto* revealTemplateAction = partsMenu->addAction(tr("Show &Template File"));
    revealTemplateAction->setStatusTip(
        tr("Open the project's template in whatever edits JSON on this machine."));
    connect(revealTemplateAction, &QAction::triggered, this, [this] {
        const QString path = m_project.absolutePath(m_project.linkageTemplate().relativePath);
        if (!path.isEmpty() && QFileInfo::exists(path))
            QDesktopServices::openUrl(QUrl::fromLocalFile(path));
    });

    QMenu* viewMenu = menuBar()->addMenu(tr("&View"));
    viewMenu->addAction(m_fitAction);
    viewMenu->addSeparator();
    viewMenu->addAction(m_labelsAction);
    viewMenu->addAction(m_linksAction);
    viewMenu->addAction(m_wheelsAction);
    viewMenu->addAction(m_hardpointDock->toggleViewAction());
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
        QMessageBox::warning(this, tr("Geometry missing"),
                             tr("This project's geometry file is not where the project says it "
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
        QMessageBox::warning(this, tr("Cannot open the project's geometry"),
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
                                "Fix the file, or use Parts > Reset to Built-in Template.")
                                 .arg(result.error));
        m_linkageTemplate = LinkageTemplate{};
        rebuildLinkage();
        return false;
    }

    m_linkageTemplate = *result.templ;
    rebuildLinkage();
    return true;
}

void MainWindow::rebuildLinkage()
{
    m_linkage = buildLinkage(m_linkageTemplate, m_hardpointModel->table(), m_project.mirror());
    m_viewport->setLinkage(m_linkage);
    // updateActionState() ends by refreshing the status line, which is where the
    // part count is shown.
    updateActionState();
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

    if (view.selectedHardpoint >= 0 && view.selectedHardpoint < m_hardpointModel->rowCount()) {
        m_viewport->setSelectedHardpoint(view.selectedHardpoint);
        m_hardpointPanel->setSelectedRow(view.selectedHardpoint);
    }
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
    const QString path = QFileDialog::getOpenFileName(this, tr("Import geometry"),
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
                             tr("The geometry loaded, but could not be copied into the "
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
            this, tr("Remove geometry"),
            tr("Remove %1 from this project?\n\nThe copy inside the project folder is deleted. "
               "The file it was imported from is not touched.")
                .arg(QFileInfo(asset.relativePath).fileName()),
            QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
        if (answer != QMessageBox::Yes) return;
        QFile::remove(m_project.absolutePath(asset.relativePath));
    }

    m_viewport->clearMesh();
    m_closeAction->setEnabled(false);
    m_meshLabel->setText(tr("No geometry imported"));
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
    m_viewport->setHardpoints(m_hardpointModel->table());
    // setHardpoints() drops the parts, because they are indices into the table
    // that has just been replaced. Resolving them again is what puts them back.
    rebuildLinkage();
    // The wheels are pinned to points by name, so a new table can move them,
    // take them away, or bring them back.
    rebuildWheels();
    // Only reframe when the hardpoints are all there is. With a mesh on screen
    // the user has already chosen a view, and moving it would be rude.
    if (refit) m_viewport->fitToView();
    updateActionState(); // which refreshes the status line too
    updateWindowTitle();
}

void MainWindow::mirrorHardpointsDialog()
{
    if (m_hardpointModel->rowCount() == 0) return;

    MirrorDialog dialog(m_hardpointModel->table(), m_viewport->selectedHardpoint(),
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
    Slot models[2] = { { &wheels.wheel, {}, {} }, { &wheels.rim, {}, {} } };

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

void MainWindow::rebuildWheels()
{
    const WheelsRef& wheels = m_project.wheels();
    m_wheelPlacements = wheels.isEmpty()
                            ? std::vector<WheelPlacement>{}
                            : resolveWheels(wheels.spec, m_hardpointModel->table());
    m_viewport->setWheelPlacements(m_wheelPlacements, wheels.spec.alignToCenter);
    updateActionState(); // which refreshes the status line too
}

void MainWindow::addWheelsDialog()
{
    if (m_hardpointModel->rowCount() == 0) return; // the action is disabled

    const WheelsRef& wheels = m_project.wheels();
    WheelDialog dialog(m_hardpointModel->table(), wheels.spec,
                       m_project.absolutePath(wheels.wheel.relativePath),
                       m_project.absolutePath(wheels.rim.relativePath),
                       geometryDialogDirectory(), this);
    if (dialog.exec() != QDialog::Accepted) return;

    applyWheels(dialog.spec(), dialog.wheelPath(), dialog.rimPath());
}

void MainWindow::applyWheels(const WheelSpec& spec, const QString& wheelPath,
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
    Slot models[2] = { { &wheels.wheel, wheelPath, kWheelStem, false },
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
        QMessageBox::warning(this, tr("Cannot import the wheel model"),
                             tr("Failed to load:\n%1\n\n%2\n\nNothing was changed.")
                                 .arg(QDir::toNativeSeparators(failedPath), failure));
        return;
    }

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
    }

    m_project.setWheels(wheels);
    m_project.setLastGeometryDirectory(
        QFileInfo(wheelPath.isEmpty() ? rimPath : wheelPath).absolutePath());
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

    for (const AssetRef* asset : { &wheels.wheel, &wheels.rim }) {
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
    m_wheelsAction->setEnabled(!m_wheelPlacements.empty());
    m_addWheelsAction->setEnabled(loaded);
    m_removeWheelsAction->setEnabled(!m_project.wheels().isEmpty());
    m_mirrorAction->setEnabled(loaded);
    m_closeHardpointsAction->setEnabled(loaded);
    m_exportWorkbookAction->setEnabled(loaded && m_hardpointSource.isValid());
    m_overwriteWorkbookAction->setEnabled(loaded && m_hardpointSource.isValid()
                                          && !m_project.hardpoints().isEmpty());
    updateHardpointStatus();
}

} // namespace suspkin
