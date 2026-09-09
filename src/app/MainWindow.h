#pragma once

#include "io/LinkageTemplate.h"
#include "io/XlsxHardpoints.h"
#include "model/Sweep.h"
#include "project/Project.h"
#include "render/Camera.h"
#include "render/ViewportWidget.h"

#include <QMainWindow>
#include <QString>

class QAction;
class QActionGroup;
class QDockWidget;
class QLabel;
class QMenu;
class QTimer;

namespace suspkin {

class AnalysisPanel;
class HardpointModel;
class HardpointPanel;

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
    void exportSweepCsv();
    void newProject();
    void openProject();

private:
    void buildActions();
    void buildMenus();
    void buildHardpointDock();
    void buildAnalysisDock();
    void refreshRecentProjectsMenu();

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
    /// Give a template that predates the steering role an answer to the question
    /// "which axle has a rack", when the file is recognisably the built-in one.
    /// Sets @ref m_steeringNote either way; writes nothing to a template that is
    /// somebody's own work.
    void adoptTemplateSteering();
    /// Let the user say which axle the rack drives, and write it into the
    /// project's own template.
    void steeringDialog();
    /// Resolve the template against the current table and hand the result to
    /// the viewport. Cheap enough to redo whenever either one changes.
    void rebuildLinkage();

    /// Hand the configuration table the bodies its Part columns may name, and
    /// fill in what the template implies for any point that has none yet. Both
    /// come from the linkage template, so this follows the template and the
    /// points wherever either changes.
    void refreshHardpointConfig();
    /// Fold what the table holds back into the project, which is what is saved.
    /// Called from the edit path, so a configuration is never only in a widget.
    void captureHardpointConfig();

    /// Bind the template's mechanism against the current table, once per axle.
    /// Cheap -- it resolves names and measures link lengths -- so anything that
    /// changes a coordinate can call it.
    void rebuildSolvers();
    /// Run the sweep the panel is asking for and hand the curve over.
    void refreshSweep();
    /// Put the mechanism where the panel says, or back at the coordinates the
    /// table holds. Nothing here touches the table itself.
    void applySimulation();
    /// The table as the viewport should draw it: the design coordinates, or
    /// those coordinates with the solved pose laid over them by name.
    HardpointTable posedTable() const;
    /// How far each posed upright has turned, by the name of its wheel centre.
    /// Empty when nothing is being simulated, which leaves every wheel model at
    /// the attitude its CAD file drew it in.
    WheelRotations wheelRotations() const;
    /// The axle the panel has selected, or nothing when none can be solved.
    const AxleSolver* currentAxle() const;

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
    QAction* m_steeringAction = nullptr;
    QAction* m_addWheelsAction = nullptr;
    QAction* m_removeWheelsAction = nullptr;
    QAction* m_wheelsAction = nullptr;
    QActionGroup* m_modeGroup = nullptr;
    QMenu* m_recentProjectsMenu = nullptr;

    HardpointModel* m_hardpointModel = nullptr;
    HardpointPanel* m_hardpointPanel = nullptr;
    QDockWidget* m_hardpointDock = nullptr;

    AnalysisPanel* m_analysisPanel = nullptr;
    QDockWidget* m_analysisDock = nullptr;

    /// One per corner the template names, whether or not the table holds it.
    /// Rebuilt whenever the table or the template changes, because both of them
    /// are what a solver is bound to.
    std::vector<AxleSolver> m_axles;
    /// The curve on the plot.
    SweepResult m_sweep;
    /// Why there is no curve, when there is none: a template that does not name
    /// the mechanism, a table with no complete axle in it.
    QString m_solverNote;
    /// Where the mechanism is standing, one entry per axle being posed -- the
    /// selected one first, then the others when they are along for the ride.
    /// Empty unless the panel says it is simulating; the table is never changed
    /// to match any of it.
    std::vector<AxleSample> m_poses;

    /// The workbook the points came from, kept whole so saving can rewrite the
    /// value cells and copy every other byte through unchanged.
    XlsxHardpointSource m_hardpointSource;
    /// The rule for turning the table into parts, and the parts it produced.
    /// The template is the project's copy, not the built-in one, unless the
    /// project had none and was given one.
    LinkageTemplate m_linkageTemplate;
    /// What to say about steering in the status line: that it was filled in, or
    /// that the template says nothing and every axle is therefore steerable.
    QString m_steeringNote;
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
