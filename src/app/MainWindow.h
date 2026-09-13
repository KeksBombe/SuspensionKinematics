#pragma once

#include "io/LinkageTemplate.h"
#include "io/XlsxHardpoints.h"
#include "model/Sweep.h"
#include "project/Project.h"
#include "render/Camera.h"
#include "render/ViewportWidget.h"
#include "update/UpdateManifest.h"

#include <QMainWindow>
#include <QString>

#include <functional>
#include <memory>

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
class MeshQuery;
class PanelAction;
class Ribbon;
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
    /// The whole window -- menu bar, ribbon, docks and viewport -- as it would
    /// be on screen. Used by --screenshot-window, the same way.
    QImage captureWindow();

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
    /// A point of the user's own, next to the selected one. In a project with
    /// no workbook yet this is also what makes one: New Hardpoint Table.
    void addPointDialog();
    void deleteSelectedPoints();
    void renamePointDialog();
    /// The targets, the preview, and -- if the user says so -- the points.
    void generateFromDesignDialog();
    void importLinkageTemplateDialog();
    void resetLinkageTemplate();
    /// A part drawn through the selected points, written into the template.
    void newPartFromSelection();
    void editPartsDialog();
    void addWheelsDialog();
    void removeWheels();
    void exportSweepCsv();
    void newProject();
    void openProject();

private:
    /// Every command, each one a QAction that the menus and the ribbon both
    /// show. Built after the docks, because the panel toggles are actions on
    /// them.
    void buildActions();
    /// The tooltip each command's ribbon button shows, and every command put
    /// on the window itself, so its shortcut works whichever tab is showing.
    void finishActions();
    /// The File menu, which the ribbon's accent button opens. It is the only
    /// menu the window has: everything else is a tab.
    void buildFileMenu();
    /// The ribbon, as the window's menu widget. There is no menu bar -- see
    /// the definition.
    void buildRibbon();
    void buildHardpointDock();
    void buildAnalysisDock();
    void refreshRecentProjectsMenu();
    /// The collapse chevron says what clicking it will do, so its text, its
    /// icon and its tooltip follow the ribbon.
    void updateCollapseAction();
    /// Put every panel back where a new project has it, docked, and keep open
    /// the ones that were open. What rescues a panel floated onto a monitor
    /// that is not plugged in today.
    void resetPanelLayout();

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
    /// Give a template that predates the steering role an answer to the question
    /// "which axle has a rack", when the file is recognisably the built-in one.
    /// Sets @ref m_steeringNote either way; writes nothing to a template that is
    /// somebody's own work.
    void adoptTemplateSteering();
    /// Let the user say which axle the rack drives, and write it into the
    /// project's own template.
    void steeringDialog();
    /// Let the user state each axle's static camber and toe -- or hand an axle
    /// back to its hardpoints -- and solve again with them.
    void staticAnglesDialog();
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
    /// those coordinates with the solved pose laid over them by name -- and,
    /// while the body is rolled, every point of it moved with the body.
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
    void applyWheels(const WheelSpec& spec, const QString& tyrePath, const QString& rimPath);

    /// Note that something worth persisting changed, and schedule a save.
    void markDirty();
    void collectViewState();
    void applyViewState();

    void setHardpointTable(HardpointTable table, bool refit);
    /// Everything that is resolved against the table, resolved again: the
    /// markers, the parts, the solve, the wheels. The other half of
    /// setHardpointTable(), for when the model has already been changed a row
    /// at a time. The parts are indices into the table, so any add, delete or
    /// rename that skipped this would draw them between the wrong points.
    void syncTableToViewport(bool refit);
    /// Select @p rows in the viewport and the table together.
    void selectRows(const QList<int>& rows, int current);
    /// Give a project with no workbook one of its own, filled with @p table,
    /// and read it back as the baseline -- so from here on it is an ordinary
    /// project with an ordinary workbook, not a special case.
    bool adoptNewWorkbook(const HardpointTable& table);
    /// Patch the project's linkage template with @p patch and read it back.
    /// @p failure titles the message when that goes wrong.
    bool patchLinkageTemplate(const std::function<QByteArray(const QByteArray&, QString*)>& patch,
                              const QString& failure);
    /// The project's geometry as something rays can be cast at, built the first
    /// time it is asked for and dropped whenever the geometry changes.
    const MeshQuery* chassisQuery();
    /// The targets a project that has never opened the generator starts from:
    /// the 2025 car, pointed at this project's own corners and side.
    DesignParameters initialDesign() const;
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
    QAction* m_revealProjectAction = nullptr;
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
    QAction* m_newTableAction = nullptr;
    QAction* m_addPointAction = nullptr;
    QAction* m_deletePointAction = nullptr;
    QAction* m_renamePointAction = nullptr;
    QAction* m_generateAction = nullptr;
    QAction* m_newPartAction = nullptr;
    QAction* m_editPartsAction = nullptr;
    QAction* m_labelsAction = nullptr;
    QAction* m_linksAction = nullptr;
    QAction* m_importLinkageAction = nullptr;
    QAction* m_resetLinkageAction = nullptr;
    QAction* m_revealTemplateAction = nullptr;
    QAction* m_steeringAction = nullptr;
    QAction* m_staticAnglesAction = nullptr;
    QAction* m_addWheelsAction = nullptr;
    QAction* m_removeWheelsAction = nullptr;
    QAction* m_wheelsAction = nullptr;
    QAction* m_exportSweepAction = nullptr;
    /// Front, Rear, Left, Right, Top, Bottom, Isometric, in that order.
    QList<QAction*> m_presetActions;
    QActionGroup* m_modeGroup = nullptr;
    QMenu* m_recentProjectsMenu = nullptr;
    QAction* m_checkUpdatesAction = nullptr;
    QAction* m_autoUpdateAction = nullptr;
    QAction* m_aboutAction = nullptr;
    QAction* m_licensesAction = nullptr;
    QAction* m_aboutQtAction = nullptr;
    UpdateChecker* m_updates = nullptr;

    /// Open a panel, bring it forward when it is behind another, or close it
    /// when it is in front. In the menus and on every ribbon tab.
    PanelAction* m_hardpointsPanelAction = nullptr;
    PanelAction* m_analysisPanelAction = nullptr;
    PanelAction* m_parametersPanelAction = nullptr;
    QAction* m_collapseRibbonAction = nullptr;
    QAction* m_resetLayoutAction = nullptr;

    /// The only menu there is: File, from the ribbon's accent button.
    /// Everything else is a ribbon tab.
    QMenu* m_fileMenu = nullptr;
    /// The view presets, for the ribbon's Views button.
    QMenu* m_viewsMenu = nullptr;
    Ribbon* m_ribbon = nullptr;
    /// Where the docks sit in a project that has never been laid out: taken
    /// before the project's own layout is restored, and what Reset Panel
    /// Layout goes back to.
    QByteArray m_defaultDockState;

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
    /// Where the body is, seen from the road, while it rolls: turned about the
    /// roll axis, taking the geometry, the points and the wheels with it.
    /// Nothing the rest of the time, which is the body where the table has it.
    /// Like the poses it is derived, from them and the panel, so there is
    /// nothing of it to save.
    std::optional<Rigid> m_bodyMotion;

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

    /// The geometry, ready for the generator to cast rays at. Built on first
    /// use -- a million-triangle chassis takes a moment -- and reset whenever
    /// the geometry is replaced or removed.
    std::unique_ptr<MeshQuery> m_chassisQuery;

    /// Set while the project is being loaded, so restoring a saved view does not
    /// immediately look like a change the user made.
    bool m_loading = false;
    QTimer* m_saveTimer = nullptr;
};

} // namespace suspkin
