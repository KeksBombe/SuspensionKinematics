#include "app/MainWindow.h"

#include "geom/MeshTopology.h"
#include "io/MeshImport.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
#include <QLocale>
#include <QMenuBar>
#include <QMessageBox>
#include <QImage>
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
    m_glLabel = new QLabel(this);
    statusBar()->addWidget(m_meshLabel, 1);
    statusBar()->addPermanentWidget(m_glLabel);

    connect(m_viewport, &ViewportWidget::contextReady, m_glLabel, &QLabel::setText);

    buildActions();
    buildMenus();
    restoreSession();

    // The overlay buttons and the menu entries are two views of one mode.
    connect(m_viewport, &ViewportWidget::displayModeChanged, this, [this](DisplayMode mode) {
        (mode == DisplayMode::Solid ? m_solidAction : m_trianglesAction)->setChecked(true);
    });

    setWindowTitle(tr("SuspensionKinematics"));
    resize(1200, 800);
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

    QMenu* viewMenu = menuBar()->addMenu(tr("&View"));
    viewMenu->addAction(m_fitAction);
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
    m_recentFiles = settings.value(QStringLiteral("io/recentFiles")).toStringList();
    refreshRecentMenu();
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    QSettings settings;
    settings.setValue(QStringLiteral("window/geometry"), saveGeometry());
    settings.setValue(QStringLiteral("window/state"), saveState());
    settings.setValue(QStringLiteral("io/lastDirectory"), m_lastDirectory);
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
    setWindowTitle(tr("SuspensionKinematics"));
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
    setWindowTitle(tr("%1 - SuspensionKinematics").arg(QFileInfo(path).fileName()));
    m_lastDirectory = QFileInfo(path).absolutePath();
    rememberRecentFile(path);
}

} // namespace suspkin
