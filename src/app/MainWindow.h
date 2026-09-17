#pragma once

#include "app/framework/AppContext.h"
#include "app/framework/CommandRegistry.h"
#include "app/framework/Feature.h"
#include "app/framework/WindowActions.h"
#include "app/session/ProjectSession.h"
#include "model/HardpointGenerator.h"
#include "project/Project.h"
#include "render/Camera.h"
#include "render/ViewportWidget.h"

#include <QMainWindow>
#include <QString>

#include <memory>

class QAction;
class QDockWidget;
class QLabel;
class QMenu;

namespace suspkin {

class AnalysisPanel;
class HardpointModel;
class HardpointPanel;
class PointEditController;
class Ribbon;

/// The application window, which always has exactly one project open.
///
/// Nothing here is transient. Every import, every edit, every change of view
/// belongs to the project and is written back to it -- see saveProject(), which
/// a debounced timer calls after anything at all changes, and which closing the
/// window calls one last time. There is no "unsaved document" state to lose,
/// and so no dialog asking about one.
///
/// What the project is unfolded into lives in the ProjectSession; the window
/// draws it and says so when it changes.
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

    const Project& project() const { return m_session->project(); }

    /// Write the project out now: the manifest, and the pending hardpoint edits.
    bool saveProject() override;

    // --- AppContext: the services a feature may use -------------------------
    QWidget* window() override { return this; }
    WindowActions* windowActions() override { return this; }
    Project& project() override { return m_session->project(); }
    ProjectSession& session() override { return *m_session; }
    void markDirty() override { m_session->markDirty(); }
    ViewportWidget* viewport() override { return m_viewport; }
    HardpointModel* hardpoints() override { return &m_session->hardpoints().model(); }
    void showStatus(const QString& text, int milliseconds) override;
    QDockWidget* hardpointDock() override { return m_hardpointDock; }
    QDockWidget* analysisDock() override { return m_analysisDock; }
    AnalysisPanel* analysisPanel() override { return m_analysisPanel; }
    Ribbon* ribbon() override { return m_ribbon; }
    void syncTableToViewport(bool refit) override;
    void refreshCommands() override { updateChrome(); }
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
    /// The File menu and the ribbon, as the window's menu widget.
    void buildRibbon();

    /// Push what the session resolves at the viewport and the panel, as it
    /// resolves it.
    void connectSession();
    /// What the analysis panel is asking the solve for.
    SimulationRequest simulationRequest() const;
    /// The panel's axle box, from the axles the session bound.
    void showAxles();
    /// The sweep on the plot, and what the panel says under it.
    void showSweep();
    /// The markers, the chassis and the readout where the pose puts them.
    void showPose();

    /// The names of the selected points, in picking order.
    QStringList selectedPointNames() const;
    /// Select the points called @p names, the first of them current. Names the
    /// table does not have are skipped.
    void selectPointsNamed(const QStringList& names);

    void buildHardpointDock();
    void buildAnalysisDock();

    /// Load what the project already holds: its geometry copy, its workbook copy
    /// and edits file, its view and its window layout.
    void openProjectContents();
    bool openChassis();
    bool openHardpoints();
    /// Read the project's linkage template, writing the built-in one into the
    /// project first if it does not have one yet.
    bool openLinkageTemplate();
    /// Read the wheel and rim models the project holds and hand them to the
    /// viewport, then place them.
    bool openWheels();

    void collectViewState();
    void applyViewState();

    void setHardpointTable(HardpointTable table, bool refit);
    /// Select @p rows in the viewport and the table together.
    void selectRows(const QList<int>& rows, int current);
    /// Give a project with no workbook one of its own, filled with @p table,
    /// and read it back as the baseline -- so from here on it is an ordinary
    /// project with an ordinary workbook, not a special case.
    bool adoptNewWorkbook(const HardpointTable& table);
    /// Put @p problem in front of the user, when there is one.
    void warn(const SessionMessage& problem);
    /// The targets a project that has never opened the generator starts from:
    /// the 2025 car, pointed at this project's own corners and side.
    DesignParameters initialDesign() const;
    void updateWindowTitle();
    void updateHardpointStatus();
    /// Put everything the window says about its own state back in step: which
    /// commands can be used, what the status line reads, and the title.
    void updateChrome();

    QString geometryDialogDirectory() const;
    QString hardpointDialogDirectory() const;

    /// The project and everything it is unfolded into. A child of the window,
    /// created before anything that is drawn from it.
    ProjectSession* m_session = nullptr;

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
    Ribbon* m_ribbon = nullptr;
    /// Where the docks sit in a project that has never been laid out: taken
    /// before the project's own layout is restored, and what Reset Panel
    /// Layout goes back to.
    QByteArray m_defaultDockState;

    HardpointPanel* m_hardpointPanel = nullptr;
    QDockWidget* m_hardpointDock = nullptr;

    /// The arrows on a selected marker, and the field X, Y and Z open.
    PointEditController* m_pointEditing = nullptr;

    AnalysisPanel* m_analysisPanel = nullptr;
    QDockWidget* m_analysisDock = nullptr;
};

} // namespace suspkin
