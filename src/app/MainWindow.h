#pragma once

#include "app/framework/AppContext.h"
#include "app/framework/CommandRegistry.h"
#include "app/framework/Feature.h"
#include "app/framework/WindowActions.h"
#include "io/LinkageTemplate.h"
#include "io/XlsxHardpoints.h"
#include "model/EditHistory.h"
#include "model/Simulation.h"
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
class CoordinateEntry;
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
class MainWindow : public QMainWindow, public AppContext, public WindowActions {
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
    bool saveProject() override;

    // --- AppContext: the services a feature may use -------------------------
    // Nearly all of it is already here; what the interface adds is that a
    // feature can reach it without knowing this class exists.
    QWidget* window() override { return this; }
    WindowActions* windowActions() override { return this; }
    Project& project() override { return m_project; }
    void markDirty() override;
    ViewportWidget* viewport() override { return m_viewport; }
    HardpointModel* hardpoints() override { return m_hardpointModel; }
    void showStatus(const QString& text, int milliseconds) override;
    QDockWidget* hardpointDock() override { return m_hardpointDock; }
    QDockWidget* analysisDock() override { return m_analysisDock; }
    AnalysisPanel* analysisPanel() override { return m_analysisPanel; }
    Ribbon* ribbon() override { return m_ribbon; }
    const LinkageTemplate& linkageTemplate() const override { return m_linkageTemplate; }
    const Linkage& linkage() const override { return m_linkage; }
    const Simulation& simulation() const override { return m_simulation; }
    const std::vector<WheelPlacement>& wheelPlacements() const override
    {
        return m_wheelPlacements;
    }
    bool workbookWritable() const override { return m_hardpointSource.isValid(); }
    void syncTableToViewport(bool refit) override;
    void refreshCommands() override { updateChrome(); }
    EditHistory& editHistory() override { return m_history; }
    void restoreEditState(const EditState& from, const EditState& to) override;

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

    // --- WindowActions: command bodies that have not moved into a feature yet
    // Each of these belongs in the feature that registers the command for it;
    // see WindowActions, which is the list of what is left to move.
private slots:
    void importChassisDialog() override;
    void removeChassis() override;
    void importHardpointsDialog() override;
    void mirrorHardpointsDialog() override;
    bool overwriteWorkbook() override;
    bool exportWorkbookAs() override;
    void removeHardpoints() override;
    /// A point of the user's own, next to the selected one. In a project with
    /// no workbook yet this is also what makes one: New Hardpoint Table.
    void addPointDialog() override;
    void deleteSelectedPoints() override;
    void renamePointDialog() override;
    /// The targets, the preview, and -- if the user says so -- the points.
    void generateFromDesignDialog() override;
    void importLinkageTemplateDialog() override;
    void resetLinkageTemplate() override;
    /// A part drawn through the selected points, written into the template.
    void newPartFromSelection() override;
    void editPartsDialog() override;
    void addWheelsDialog() override;
    void removeWheels() override;
    void exportSweepCsv() override;
    void newProject() override;
    void openProject() override;
    void showProjectList() override;
    void revealProjectFolder() override;
    /// Open the project's template in whatever edits JSON on this machine.
    void revealTemplateFile() override;
    /// Let the user say which axle the rack drives, and write it into the
    /// project's own template.
    void steeringDialog() override;
    /// Let the user state each axle's static camber and toe -- or hand an axle
    /// back to its hardpoints -- and solve again with them.
    void staticAnglesDialog() override;
    /// Put every panel back where a new project has it, docked, and keep open
    /// the ones that were open. What rescues a panel floated onto a monitor
    /// that is not plugged in today.
    void resetPanelLayout() override;

private:
    /// Make every command. Built after the docks, because the panel toggles are
    /// actions on them. What each one is, is its own feature's business.
    void buildCommands();
    /// Give every command its tooltip and put it on the window, so its shortcut
    /// works whichever ribbon tab is showing.
    void finishCommands();
    /// The File menu, which the ribbon's accent button opens. It is the only
    /// menu the window has: everything else is a tab.
    void buildFileMenu();
    /// The ribbon, as the window's menu widget. There is no menu bar -- see
    /// the definition.
    void buildRibbon();
    /// The two ways a point is moved in the viewport itself: the arrows on the
    /// selected marker, and the field that X, Y or Z opens over it. Both end up
    /// in moveHardpointCoordinate(), so a point dragged and a point typed are
    /// the same edit.
    void buildPointEditing();
    /// Open the coordinate field on @p axis of the point at @p row, holding
    /// what the table has for it.
    void openCoordinateEntry(int row, int axis);
    /// Write one coordinate of one point through the table. That is what makes
    /// it an edit: the parts, the solve, the wheels and the project all follow
    /// from the model's own signal.
    void moveHardpointCoordinate(int row, int axis, double value);
    /// Say in the status bar where a marker being dragged has got to. A
    /// readout, not an edit -- the table is untouched until the drag ends.
    void showDragPosition(int row, int axis, double distance);

    /// What an edit of the points can change, as the project holds it now.
    EditState currentEditState() const;
    /// Note in the history that an edit called @p label has just been made.
    /// Every edit of the points ends here, after the project holds it; one
    /// that changed nothing is not recorded.
    void recordEdit(const QString& label);
    /// Start the history again from what the project holds now, with nothing
    /// to undo: a project opened, or its points came from somewhere new.
    void restartEditHistory();
    /// Put @p state into the model and the project, and resolve everything
    /// against it again. No step is recorded -- this is how a step is taken.
    void applyEditState(const EditState& state);
    /// Point every wheel centred on a renamed point at its new name. @p renamed
    /// is old name to new.
    void moveWheelsToRenamedPoints(const QHash<QString, QString>& renamed);
    /// The names of the selected points, in picking order.
    QStringList selectedPointNames() const;
    /// Select the points called @p names, the first of them current. Names the
    /// table does not have are skipped.
    void selectPointsNamed(const QStringList& names);

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

    /// Bind the template's mechanism against the current table and tell the
    /// panel which axles came out. The solve itself is Simulation's, in the
    /// core; what is left here is handing it the project's own state and
    /// putting the answer in front of the user.
    void rebuildSolvers();
    /// Run the sweep the panel is asking for and hand the curve over.
    void refreshSweep();
    /// Put the mechanism where the panel says, or back at the coordinates the
    /// table holds. Nothing here touches the table itself.
    void applySimulation();

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
    void collectViewState();
    void applyViewState();

    void setHardpointTable(HardpointTable table, bool refit);
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
    /// Put everything the window says about its own state back in step: which
    /// commands can be used, and what the status line reads.
    void updateChrome();

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

    /// Every command the window has, whichever feature contributed it. One
    /// QAction each, so the ribbon button, the File menu entry and the shortcut
    /// cannot disagree about whether it is on.
    CommandRegistry m_commands{ this };
    /// The features themselves, created from the registry. The window holds
    /// them and names none of them.
    std::vector<std::unique_ptr<Feature>> m_features;
    QMenu* m_recentProjectsMenu = nullptr;

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

    /// The field X, Y and Z open over the viewport. A child of the viewport, so
    /// it sits over the marker it is about.
    CoordinateEntry* m_coordinateEntry = nullptr;
    /// The row that field is open on, because the selection is not allowed to
    /// answer for it: what was typed belongs to the point it was opened on.
    int m_entryRow = -1;

    AnalysisPanel* m_analysisPanel = nullptr;
    QDockWidget* m_analysisDock = nullptr;

    /// Every axle the template names, bound to the table as it stands. Rebuilt
    /// whenever either changes, because both are what a solver is bound to.
    Simulation m_simulation;
    /// The curve on the plot.
    SweepResult m_sweep;
    /// Where the car is standing while the panel is simulating, and nothing the
    /// rest of the time. Derived from the simulation and the panel, so there is
    /// nothing of it to save, and the table is never changed to match any of it.
    SimulationPose m_pose;

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

    /// Every state the user's editing of the points has passed through, for
    /// undo and redo. Not saved: it is this session's way back, and where the
    /// session ended up is already in the project.
    EditHistory m_history;

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
