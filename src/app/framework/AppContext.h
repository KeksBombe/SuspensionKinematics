#pragma once

#include <QString>

#include <vector>

class QDockWidget;
class QWidget;

namespace suspkin {

class AnalysisPanel;
class HardpointModel;
class Ribbon;
class Simulation;
class ViewportWidget;
class Project;
class WindowActions;
struct Linkage;
struct LinkageTemplate;
struct WheelPlacement;

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
    virtual QWidget* window() = 0;

    /// The command bodies that have not moved into a feature yet. Shrinking:
    /// see WindowActions.
    virtual WindowActions* windowActions() = 0;

    // --- the project, which owns all state ---------------------------------
    virtual Project& project() = 0;
    /// Note that something worth persisting changed, and schedule a save.
    virtual void markDirty() = 0;
    /// Write the project out now, rather than when the timer says.
    virtual bool saveProject() = 0;

    // --- what is on screen --------------------------------------------------
    virtual ViewportWidget* viewport() = 0;
    virtual HardpointModel* hardpoints() = 0;
    virtual void showStatus(const QString& text, int milliseconds) = 0;

    // --- the window's own furniture -----------------------------------------
    // Docks and the ribbon are the window rather than any one feature, so they
    // are offered here for the feature that puts a command on them.
    virtual QDockWidget* hardpointDock() = 0;
    virtual QDockWidget* analysisDock() = 0;
    virtual AnalysisPanel* analysisPanel() = 0;
    virtual Ribbon* ribbon() = 0;

    // --- what has been resolved against the table ---------------------------
    virtual const LinkageTemplate& linkageTemplate() const = 0;
    virtual const Linkage& linkage() const = 0;
    virtual const Simulation& simulation() const = 0;
    virtual const std::vector<WheelPlacement>& wheelPlacements() const = 0;
    /// The project's workbook was read whole, so the table can be written back
    /// through it.
    virtual bool workbookWritable() const = 0;

    // --- saying that something changed --------------------------------------
    /// Everything that is resolved against the table, resolved again: the
    /// markers, the parts, the solve, the wheels.
    virtual void syncTableToViewport(bool refit) = 0;
    /// Ask every command whether it can still be used, and say what the status
    /// line should now read.
    virtual void refreshCommands() = 0;
};

} // namespace suspkin
