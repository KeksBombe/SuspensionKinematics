#pragma once

#include "io/LinkageTemplate.h"
#include "io/XlsxHardpoints.h"
#include "project/Project.h"
#include "render/Camera.h"
#include "render/ViewportWidget.h"
#include "update/UpdateManifest.h"

#include <QMainWindow>
#include <QString>

class QAction;
class QActionGroup;
class QDockWidget;
class QLabel;
class QMenu;
class QTimer;

namespace suspkin {

class HardpointModel;
class HardpointPanel;
class UpdateChecker;

/// The application window, which always has exactly one project open.
///
/// Nothing here is transient. Every import, every edit, every change of view
/// belongs to the project and is written back to it -- see saveProject(), which
/// a debounced timer calls after anything at all changes, and which closing the
/// window calls one last time. There is no "unsaved document" state to lose,
/// and so no dialog asking about one.
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(Project project, QWidget* parent = nullptr);
    ~MainWindow() override;

    /// Import geometry from anywhere on disk. The file is copied into the
    /// project, and it is the copy that is loaded from then on.
    void loadFile(const QString& path);

    /// Import hardpoints from an .xlsx workbook. The workbook is copied into the
    /// project and becomes the baseline that edits are measured against.
    void loadHardpointFile(const QString& path);

    /// Set the display mode and keep the menu and toolbar in step.
    void setDisplayMode(DisplayMode mode);

    /// Render one frame and return it. Used by --screenshot to verify the
    /// renderer without a human looking at the window.
    QImage captureViewport();

    const Project& project() const { return m_project; }

    /// Write the project out now: the manifest, and the pending hardpoint edits.
    bool saveProject();

signals:
    /// The user asked for a different project. The window is finished with by
    /// the time this is emitted, so whoever handles it may close it.
    void openProjectRequested(const QString& manifestPath);
    /// The user asked to go back to the project list.
    void projectListRequested();
    /// The window has closed and its project is written out. Whoever owns the
    /// window decides what that means -- quitting, or making way for the next
    /// project.
    void closed();

protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    void importFileDialog();
    void closeModel();
    void importHardpointsDialog();
    void mirrorHardpointsDialog();
    bool overwriteWorkbook();
    bool exportWorkbookAs();
    void closeHardpoints();
    void importLinkageTemplateDialog();
    void resetLinkageTemplate();
    void addWheelsDialog();
    void removeWheels();
    void newProject();
    void openProject();

private:
    void buildActions();
    void buildMenus();
    void buildHardpointDock();
    void refreshRecentProjectsMenu();

    /// Wire up the update checker and, unless the user has turned it off, ask
    /// GitHub once shortly after the window is up. Does nothing for a portable
    /// or developer build -- see UpdateChecker.
    void setUpdateCheckerUp();
    /// Offer the release to the user and, if they accept, download and install
    /// it. `userAsked` distinguishes the startup check from the menu item.
    void offerUpdate(const UpdateRelease& release, bool userAsked);

    /// Load what the project already holds: its geometry copy, its workbook copy
    /// and edits file, its view and its window layout.
    void openProjectContents();
    bool loadGeometryFromProject();
    bool loadHardpointsFromProject();
    /// Read the project's linkage template, writing the built-in one into the
    /// project first if it does not have one yet. A project that holds
    /// hardpoints holds the rule for connecting them too.
    bool loadLinkageTemplateFromProject();
    /// Write the built-in template into the project and point the project at it.
    bool installBuiltinLinkageTemplate();
    /// Resolve the template against the current table and hand the result to
    /// the viewport. Cheap enough to redo whenever either one changes.
    void rebuildLinkage();

    /// Read the wheel and rim models the project holds and hand them to the
    /// viewport, then place them. The expensive half of the two.
    bool loadWheelsFromProject();
    /// Resolve the wheel centres against the current table and place the models
    /// on them. A handful of matrices, so anything that moves a hardpoint can
    /// call it.
    void rebuildWheels();
    /// Take what the wheel dialog came back with: import whichever models
    /// changed, drop whichever were cleared, and keep the rest.
    void applyWheels(const WheelSpec& spec, const QString& wheelPath, const QString& rimPath);

    /// Note that something worth persisting changed, and schedule a save.
    void markDirty();
    void collectViewState();
    void applyViewState();

    void setHardpointTable(HardpointTable table, bool refit);
    /// Stamp each point with where it was mirrored from, out of the project's
    /// own record. A workbook cannot carry that, so it is restored here after
    /// anything that comes back out of one.
    void applyMirrorProvenance(HardpointTable& table) const;
    /// The inverse: fold the table's provenance back into the project.
    void captureMirrorProvenance();
    void updateWindowTitle();
    void updateHardpointStatus();
    void updateActionState();

    /// Write the current table to @p path, using the imported workbook as the
    /// template. Nothing else is touched.
    bool writeHardpointsTo(const QString& path);
    /// The edits the project is holding that are not in the workbook yet.
    HardpointEdits pendingEdits() const;

    QString geometryDialogDirectory() const;
    QString hardpointDialogDirectory() const;

    Project m_project;

    ViewportWidget* m_viewport = nullptr;
    QLabel* m_meshLabel = nullptr;
    QLabel* m_hardpointLabel = nullptr;
    QLabel* m_glLabel = nullptr;

    QAction* m_newProjectAction = nullptr;
    QAction* m_openProjectAction = nullptr;
    QAction* m_saveProjectAction = nullptr;
    QAction* m_projectListAction = nullptr;
    QAction* m_importAction = nullptr;
    QAction* m_closeAction = nullptr;
    QAction* m_quitAction = nullptr;
    QAction* m_solidAction = nullptr;
    QAction* m_trianglesAction = nullptr;
    QAction* m_fitAction = nullptr;
    QAction* m_importHardpointsAction = nullptr;
    QAction* m_mirrorAction = nullptr;
    QAction* m_overwriteWorkbookAction = nullptr;
    QAction* m_exportWorkbookAction = nullptr;
    QAction* m_closeHardpointsAction = nullptr;
    QAction* m_labelsAction = nullptr;
    QAction* m_linksAction = nullptr;
    QAction* m_importLinkageAction = nullptr;
    QAction* m_resetLinkageAction = nullptr;
    QAction* m_addWheelsAction = nullptr;
    QAction* m_removeWheelsAction = nullptr;
    QAction* m_wheelsAction = nullptr;
    QActionGroup* m_modeGroup = nullptr;
    QMenu* m_recentProjectsMenu = nullptr;
    QAction* m_checkUpdatesAction = nullptr;
    QAction* m_autoUpdateAction = nullptr;
    UpdateChecker* m_updates = nullptr;

    HardpointModel* m_hardpointModel = nullptr;
    HardpointPanel* m_hardpointPanel = nullptr;
    QDockWidget* m_hardpointDock = nullptr;

    /// The workbook the points came from, kept whole so saving can rewrite the
    /// value cells and copy every other byte through unchanged.
    XlsxHardpointSource m_hardpointSource;
    /// The rule for turning the table into parts, and the parts it produced.
    /// The template is the project's copy, not the built-in one, unless the
    /// project had none and was given one.
    LinkageTemplate m_linkageTemplate;
    Linkage m_linkage;

    /// Where the wheel models are drawn, resolved from the project's own spec
    /// against the table as it stands now. Kept here as well as in the viewport
    /// because it is what the status line counts.
    std::vector<WheelPlacement> m_wheelPlacements;

    /// The table exactly as the workbook holds it. Everything the user changes
    /// is a difference from this, and that difference is what the project keeps
    /// in its edits file until the workbook is overwritten or exported.
    HardpointTable m_baseline;

    /// Set while the project is being loaded, so restoring a saved view does not
    /// immediately look like a change the user made.
    bool m_loading = false;
    QTimer* m_saveTimer = nullptr;
};

} // namespace suspkin
