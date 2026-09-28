#pragma once

#include "app/framework/AppContext.h"
#include "app/framework/CommandRegistry.h"
#include "app/framework/Feature.h"
#include "app/session/ProjectSession.h"
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
class MainWindow : public QMainWindow, public AppContext {
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
    QMainWindow* window() override { return this; }
    Project& project() override { return m_session->project(); }
    ProjectSession& session() override { return *m_session; }
    void markDirty() override { m_session->markDirty(); }
    void requestProject(const QString& manifestPath) override;
    void requestProjectList() override;
    ViewportWidget* viewport() override { return m_viewport; }
    HardpointModel* hardpoints() override { return &m_session->hardpoints().model(); }
    void showStatus(const QString& text, int milliseconds) override;
    void showProblem(const SessionMessage& problem) override;
    void selectPoints(const QList<int>& rows, int current) override;
    QDockWidget* hardpointDock() override { return m_hardpointDock; }
    QDockWidget* analysisDock() override { return m_analysisDock; }
    AnalysisPanel* analysisPanel() override { return m_analysisPanel; }
    Ribbon* ribbon() override { return m_ribbon; }
    QByteArray defaultDockState() const override { return m_defaultDockState; }
    void setHardpointTable(const HardpointTable& table, bool refit) override;
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
    /// Tell the viewport which points are chassis-fixed, so that it draws no
    /// member between two of them. What a point is for is edited in the table
    /// long after the parts were resolved, so this follows the configuration
    /// rather than the linkage.
    void syncGroundedPoints();

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

    /// Save the project whenever a panel is floated, docked or moved to another
    /// area, so a layout the user put back is the one the project reopens with.
    void markDirtyWhenDocksMove();
    void collectViewState();
    void applyViewState();

    /// The title: the project, its chassis and its workbook, starred while
    /// @p pending holds edits the workbook does not have yet.
    void updateWindowTitle(const HardpointEdits& pending);
    /// The status line's count of points, parts, wheels and @p pending edits.
    void updateHardpointStatus(const HardpointEdits& pending);
    /// Put everything the window says about its own state back in step: which
    /// commands can be used, what the status line reads, and the title.
    void updateChrome();

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
