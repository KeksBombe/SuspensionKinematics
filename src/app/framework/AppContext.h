#pragma once

#include <QByteArray>
#include <QList>
#include <QMainWindow>
#include <QString>

class QDockWidget;

namespace suspkin {

class AnalysisPanel;
class HardpointModel;
class ProjectSession;
class Ribbon;
class ViewportWidget;
class Project;
struct EditState;
struct HardpointTable;
struct SessionMessage;

/// What the window offers a feature.
///
/// Services, not commands: the project, what is on screen, and the few things
/// only the window can do. A feature does its own work with these, which is what
/// keeps the window from growing a method every time one is added.
///
/// The rule that shapes everything still holds through here -- anything a
/// feature changes is the project's, and markDirty() is how it says so.
class AppContext {
public:
    virtual ~AppContext();

    /// What to parent a dialog to.
    virtual QMainWindow* window() = 0;

    // --- the project, which owns all state ---------------------------------
    virtual Project& project() = 0;
    /// Note that something worth persisting changed, and schedule a save.
    virtual void markDirty() = 0;
    /// Write the project out now, rather than when the timer says.
    virtual bool saveProject() = 0;
    /// Everything the project is unfolded into: the chassis, the points and
    /// their workbook, the template and its parts, the solve, the wheels, and
    /// the undo history of the points.
    virtual ProjectSession& session() = 0;
    /// Leave this project for the one whose manifest is @p manifestPath. The
    /// caller has saved; the window is finished with once this returns.
    virtual void requestProject(const QString& manifestPath) = 0;
    /// Leave this project for the list of projects.
    virtual void requestProjectList() = 0;

    // --- what is on screen --------------------------------------------------
    virtual ViewportWidget* viewport() = 0;
    virtual HardpointModel* hardpoints() = 0;
    virtual void showStatus(const QString& text, int milliseconds) = 0;
    /// Put what the session had to say in front of the user. Nothing, when it
    /// had nothing to say.
    virtual void showProblem(const SessionMessage& problem) = 0;
    /// Select @p rows in the viewport and the table together, @p current the
    /// one editing acts on.
    virtual void selectPoints(const QList<int>& rows, int current) = 0;

    // --- the window's own furniture -----------------------------------------
    // Docks and the ribbon are the window rather than any one feature, so they
    // are offered here for the feature that puts a command on them.
    virtual QDockWidget* hardpointDock() = 0;
    virtual QDockWidget* analysisDock() = 0;
    virtual AnalysisPanel* analysisPanel() = 0;
    virtual Ribbon* ribbon() = 0;
    /// Where the docks sit in a project that has never been laid out.
    virtual QByteArray defaultDockState() const = 0;

    // --- saying that something changed --------------------------------------
    /// Put @p table in the model, and resolve everything against it again.
    virtual void setHardpointTable(const HardpointTable& table, bool refit) = 0;
    /// Everything that is resolved against the table, resolved again: the
    /// markers, the parts, the solve, the wheels.
    virtual void syncTableToViewport(bool refit) = 0;
    /// Ask every command whether it can still be used, and say what the status
    /// line should now read.
    virtual void refreshCommands() = 0;

    // --- undo and redo ------------------------------------------------------
    // The history itself is the session's.
    /// Put the project back in @p to, coming from @p from: the points, what
    /// they are for, and everything resolved against them. What the step
    /// between the two touched is selected, so the user sees what came back.
    virtual void restoreEditState(const EditState& from, const EditState& to) = 0;
};

} // namespace suspkin
