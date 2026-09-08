#include "app/MainWindow.h"

#include "app/HardpointModel.h"
#include "app/HardpointPanel.h"
#include "geom/MeshTopology.h"
#include "io/MeshImport.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QDir>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
#include <QLocale>
#include <QMenuBar>
#include <QMessageBox>
#include <QImage>
#include <QPushButton>
#include <QSettings>
#include <QStandardPaths>
#include <QStatusBar>

#include <utility>

namespace suspkin {
namespace {

constexpr int kMaxRecentFiles = 8;

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

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent)
{
    m_viewport = new ViewportWidget(this);
    setCentralWidget(m_viewport);

    m_meshLabel = new QLabel(tr("No mesh loaded"), this);
    m_hardpointLabel = new QLabel(this);
    m_glLabel = new QLabel(this);
    statusBar()->addWidget(m_meshLabel, 1);
    statusBar()->addPermanentWidget(m_hardpointLabel);
    statusBar()->addPermanentWidget(m_glLabel);

    connect(m_viewport, &ViewportWidget::contextReady, m_glLabel, &QLabel::setText);

    buildActions();
    buildHardpointDock();
    buildMenus();
    restoreSession();
    updateHardpointStatus();

    // The overlay buttons and the menu entries are two views of one mode.
    connect(m_viewport, &ViewportWidget::displayModeChanged, this, [this](DisplayMode mode) {
        (mode == DisplayMode::Solid ? m_solidAction : m_trianglesAction)->setChecked(true);
    });

    updateWindowTitle();
    resize(1280, 820);
}

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
        m_hardpointsDirty = true;
        updateWindowTitle();
    });
}

void MainWindow::buildActions()
{
    m_importAction = new QAction(tr("&Import Geometry..."), this);
    m_importAction->setShortcut(QKeySequence::Open);
    connect(m_importAction, &QAction::triggered, this, &MainWindow::importFileDialog);

    m_closeAction = new QAction(tr("&Close Model"), this);
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

    m_saveHardpointsAction = new QAction(tr("&Save Hardpoints"), this);
    m_saveHardpointsAction->setShortcut(QKeySequence::Save);
    m_saveHardpointsAction->setEnabled(false);
    connect(m_saveHardpointsAction, &QAction::triggered, this, &MainWindow::saveHardpoints);

    m_saveHardpointsAsAction = new QAction(tr("Save Hardpoints &As..."), this);
    m_saveHardpointsAsAction->setShortcut(QKeySequence::SaveAs);
    m_saveHardpointsAsAction->setEnabled(false);
    connect(m_saveHardpointsAsAction, &QAction::triggered, this, &MainWindow::saveHardpointsAs);

    m_closeHardpointsAction = new QAction(tr("&Close Hardpoints"), this);
    m_closeHardpointsAction->setEnabled(false);
    connect(m_closeHardpointsAction, &QAction::triggered, this, &MainWindow::closeHardpoints);

    m_labelsAction = new QAction(tr("Show Hardpoint &Labels"), this);
    m_labelsAction->setCheckable(true);
    m_labelsAction->setChecked(true);
    m_labelsAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+L")));
    connect(m_labelsAction, &QAction::toggled, this,
            [this](bool on) { m_viewport->setHardpointLabelsVisible(on); });
    addAction(m_labelsAction);
}

void MainWindow::setDisplayMode(DisplayMode mode)
{
    (mode == DisplayMode::Solid ? m_solidAction : m_trianglesAction)->setChecked(true);
    m_viewport->setDisplayMode(mode);
}

QImage MainWindow::captureViewport()
{
    return m_viewport->grabFramebuffer();
}

void MainWindow::buildMenus()
{
    QMenu* fileMenu = menuBar()->addMenu(tr("&File"));
    fileMenu->addAction(m_importAction);
    m_recentMenu = fileMenu->addMenu(tr("Import &Recent"));
    fileMenu->addAction(m_closeAction);
    fileMenu->addSeparator();
    // Display mode also lives here so everything is reachable from the menu bar,
    // not only from the in-viewport selector.
    fileMenu->addAction(m_solidAction);
    fileMenu->addAction(m_trianglesAction);
    fileMenu->addSeparator();
    fileMenu->addAction(m_quitAction);

    QMenu* hardpointMenu = menuBar()->addMenu(tr("&Hardpoints"));
    hardpointMenu->addAction(m_importHardpointsAction);
    hardpointMenu->addSeparator();
    hardpointMenu->addAction(m_saveHardpointsAction);
    hardpointMenu->addAction(m_saveHardpointsAsAction);
    hardpointMenu->addSeparator();
    hardpointMenu->addAction(m_closeHardpointsAction);

    QMenu* viewMenu = menuBar()->addMenu(tr("&View"));
    viewMenu->addAction(m_fitAction);
    viewMenu->addSeparator();
    viewMenu->addAction(m_labelsAction);
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
}

void MainWindow::restoreSession()
{
    QSettings settings;
    restoreGeometry(settings.value(QStringLiteral("window/geometry")).toByteArray());
    restoreState(settings.value(QStringLiteral("window/state")).toByteArray());
    m_lastDirectory = settings.value(QStringLiteral("io/lastDirectory")).toString();
    m_lastHardpointDirectory = settings.value(QStringLiteral("io/lastHardpointDirectory")).toString();
    m_recentFiles = settings.value(QStringLiteral("io/recentFiles")).toStringList();
    refreshRecentMenu();

    // restoreState() re-shows any dock that was open when the window closed,
    // and an empty hardpoint table is not worth a panel.
    if (m_hardpointDock && m_hardpointModel->rowCount() == 0) m_hardpointDock->hide();
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    if (!confirmDiscardHardpoints()) {
        event->ignore();
        return;
    }

    QSettings settings;
    settings.setValue(QStringLiteral("window/geometry"), saveGeometry());
    settings.setValue(QStringLiteral("window/state"), saveState());
    settings.setValue(QStringLiteral("io/lastDirectory"), m_lastDirectory);
    settings.setValue(QStringLiteral("io/lastHardpointDirectory"), m_lastHardpointDirectory);
    settings.setValue(QStringLiteral("io/recentFiles"), m_recentFiles);
    QMainWindow::closeEvent(event);
}

void MainWindow::rememberRecentFile(const QString& path)
{
    m_recentFiles.removeAll(path);
    m_recentFiles.prepend(path);
    while (m_recentFiles.size() > kMaxRecentFiles) m_recentFiles.removeLast();
    refreshRecentMenu();
}

void MainWindow::refreshRecentMenu()
{
    if (!m_recentMenu) return;
    m_recentMenu->clear();
    m_recentMenu->setEnabled(!m_recentFiles.isEmpty());
    for (const QString& path : m_recentFiles) {
        auto* action = m_recentMenu->addAction(QDir::toNativeSeparators(path));
        connect(action, &QAction::triggered, this, [this, path] { loadFile(path); });
    }
}

void MainWindow::closeModel()
{
    m_viewport->clearMesh();
    m_closeAction->setEnabled(false);
    m_meshLabel->setText(tr("No mesh loaded"));
    m_meshPath.clear();
    updateWindowTitle();
}

void MainWindow::updateWindowTitle()
{
    QStringList parts;
    if (!m_meshPath.isEmpty()) parts << QFileInfo(m_meshPath).fileName();
    if (!m_hardpointPath.isEmpty()) {
        parts << QFileInfo(m_hardpointPath).fileName()
                     + (m_hardpointsDirty ? QStringLiteral("*") : QString());
    }
    parts << tr("SuspensionKinematics");
    setWindowTitle(parts.join(QStringLiteral(" - ")));
}

void MainWindow::updateHardpointStatus()
{
    const int count = m_hardpointModel->rowCount();
    const bool loaded = count > 0;

    m_saveHardpointsAction->setEnabled(loaded);
    m_saveHardpointsAsAction->setEnabled(loaded);
    m_closeHardpointsAction->setEnabled(loaded);

    if (!loaded) {
        m_hardpointLabel->clear();
        return;
    }
    m_hardpointLabel->setText(m_hardpointSource.sheetName.isEmpty()
                                  ? tr("%1 hardpoints").arg(count)
                                  : tr("%1 hardpoints - %2")
                                        .arg(count)
                                        .arg(m_hardpointSource.sheetName));
}

void MainWindow::importFileDialog()
{
    QString dir = m_lastDirectory;
    if (dir.isEmpty())
        dir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);

    const QString path =
        QFileDialog::getOpenFileName(this, tr("Import geometry"), dir, importFileFilter());
    if (!path.isEmpty()) loadFile(path);
}

void MainWindow::loadFile(const QString& path)
{
    QApplication::setOverrideCursor(Qt::WaitCursor);
    MeshLoadResult result = importMeshFile(path);
    QApplication::restoreOverrideCursor();

    if (!result.ok()) {
        QMessageBox::warning(this, tr("Cannot import file"),
                             tr("Failed to load:\n%1\n\n%2")
                                 .arg(QDir::toNativeSeparators(path), result.error));
        return; // whatever was on screen stays there
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
    m_meshPath = path;
    updateWindowTitle();
    m_lastDirectory = QFileInfo(path).absolutePath();
    rememberRecentFile(path);
}

// ---------------------------------------------------------------------------
// Hardpoints
// ---------------------------------------------------------------------------

void MainWindow::importHardpointsDialog()
{
    QString directory = m_lastHardpointDirectory;
    if (directory.isEmpty()) directory = m_lastDirectory;
    if (directory.isEmpty())
        directory = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);

    const QString path = QFileDialog::getOpenFileName(this, tr("Import hardpoints"), directory,
                                                      hardpointFileFilter());
    if (!path.isEmpty()) loadHardpointFile(path);
}

void MainWindow::loadHardpointFile(const QString& path)
{
    if (!confirmDiscardHardpoints()) return;

    QApplication::setOverrideCursor(Qt::WaitCursor);
    HardpointLoadResult result = readHardpointsXlsx(path);
    QApplication::restoreOverrideCursor();

    if (!result.ok()) {
        QMessageBox::warning(this, tr("Cannot import hardpoints"),
                             tr("Failed to read:\n%1\n\n%2")
                                 .arg(QDir::toNativeSeparators(path), result.error));
        return; // whatever was loaded stays loaded
    }

    m_hardpointModel->setTable(*result.table);
    m_hardpointSource = std::move(result.source);
    m_hardpointPath = path;
    m_hardpointsDirty = false;
    m_lastHardpointDirectory = QFileInfo(path).absolutePath();

    m_viewport->setHardpoints(m_hardpointModel->table());
    // Only reframe when the hardpoints are all there is. With a mesh on screen
    // the user has already chosen a view, and moving it would be rude.
    if (!m_viewport->hasMesh()) m_viewport->fitToView();

    m_hardpointDock->show();
    m_hardpointDock->raise();
    updateHardpointStatus();
    updateWindowTitle();

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

bool MainWindow::saveHardpoints()
{
    if (m_hardpointPath.isEmpty()) return saveHardpointsAs();

    // The user asked for this file specifically, so the dialog says exactly what
    // is about to happen to it rather than asking a generic question.
    QMessageBox box(this);
    box.setIcon(QMessageBox::Warning);
    box.setWindowTitle(tr("Overwrite workbook?"));
    box.setText(tr("Overwrite %1?").arg(QFileInfo(m_hardpointPath).fileName()));
    box.setInformativeText(
        tr("%1\n\nThe hardpoint cells are rewritten in place; everything else in the "
           "workbook -- other sheets, formatting and formulas -- is kept as it is. "
           "The file on disk is replaced and this cannot be undone.")
            .arg(QDir::toNativeSeparators(m_hardpointPath)));
    QPushButton* overwrite = box.addButton(tr("Overwrite"), QMessageBox::AcceptRole);
    box.addButton(QMessageBox::Cancel);
    box.setDefaultButton(overwrite);
    box.exec();
    if (box.clickedButton() != overwrite) return false;

    return writeHardpointsTo(m_hardpointPath);
}

bool MainWindow::saveHardpointsAs()
{
    QString suggestion = m_hardpointPath;
    if (suggestion.isEmpty()) {
        const QString directory = m_lastHardpointDirectory.isEmpty()
                                      ? QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)
                                      : m_lastHardpointDirectory;
        suggestion = QDir(directory).filePath(QStringLiteral("hardpoints.xlsx"));
    }

    QString path = QFileDialog::getSaveFileName(this, tr("Save hardpoints as"), suggestion,
                                                hardpointFileFilter());
    if (path.isEmpty()) return false;
    if (QFileInfo(path).suffix().isEmpty()) path += QStringLiteral(".xlsx");

    return writeHardpointsTo(path);
}

bool MainWindow::writeHardpointsTo(const QString& path)
{
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const QString error = writeHardpointsXlsx(path, m_hardpointModel->table(), m_hardpointSource);
    QApplication::restoreOverrideCursor();

    if (!error.isEmpty()) {
        QMessageBox::warning(this, tr("Cannot save hardpoints"),
                             tr("Failed to write:\n%1\n\n%2")
                                 .arg(QDir::toNativeSeparators(path), error));
        return false;
    }

    m_hardpointPath = path;
    m_hardpointsDirty = false;
    m_lastHardpointDirectory = QFileInfo(path).absolutePath();
    updateWindowTitle();
    statusBar()->showMessage(tr("Wrote %1 hardpoints to %2")
                                 .arg(m_hardpointModel->rowCount())
                                 .arg(QFileInfo(path).fileName()),
                             6000);
    return true;
}

void MainWindow::closeHardpoints()
{
    if (!confirmDiscardHardpoints()) return;

    m_hardpointModel->clear();
    m_hardpointSource = XlsxHardpointSource{};
    m_hardpointPath.clear();
    m_hardpointsDirty = false;

    m_viewport->clearHardpoints();
    m_hardpointDock->hide();
    updateHardpointStatus();
    updateWindowTitle();
}

bool MainWindow::confirmDiscardHardpoints()
{
    if (!m_hardpointsDirty) return true;

    const QMessageBox::StandardButton answer = QMessageBox::question(
        this, tr("Unsaved hardpoints"),
        tr("The hardpoints have been edited since they were last saved.\n\n"
           "Save them now?"),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);

    if (answer == QMessageBox::Cancel) return false;
    if (answer == QMessageBox::Save) return saveHardpoints();
    return true;
}

} // namespace suspkin
