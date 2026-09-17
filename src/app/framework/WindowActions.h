#pragma once

namespace suspkin {

/// Command bodies that still live on MainWindow.
///
/// Every method here is one command's work waiting to move into the feature
/// that owns it. The interface exists so those features can be written and
/// registered against a seam now, instead of the window keeping a slot per
/// command and a list of every command there is.
///
/// It only ever shrinks: moving a body into its feature deletes a line from
/// here. When it is empty, delete the file -- that is the end of the migration.
class WindowActions {
public:
    virtual ~WindowActions();

    // --- the project ------------------------------------------------------
    virtual void newProject() = 0;
    virtual void openProject() = 0;
    virtual void showProjectList() = 0;
    virtual void revealProjectFolder() = 0;

    // --- geometry ---------------------------------------------------------
    virtual void importChassisDialog() = 0;
    virtual void removeChassis() = 0;
    virtual void addWheelsDialog() = 0;
    virtual void removeWheels() = 0;

    // --- the workbook ------------------------------------------------------
    virtual void importHardpointsDialog() = 0;
    virtual bool overwriteWorkbook() = 0;
    virtual bool exportWorkbookAs() = 0;
    virtual void removeHardpoints() = 0;

    // --- the points themselves ----------------------------------------------
    virtual void addPointDialog() = 0;
    virtual void deleteSelectedPoints() = 0;
    virtual void renamePointDialog() = 0;
    virtual void generateFromDesignDialog() = 0;
    virtual void mirrorHardpointsDialog() = 0;

    // --- the linkage --------------------------------------------------------
    virtual void newPartFromSelection() = 0;
    virtual void editPartsDialog() = 0;
    virtual void steeringDialog() = 0;
    virtual void staticAnglesDialog() = 0;
    virtual void importLinkageTemplateDialog() = 0;
    virtual void resetLinkageTemplate() = 0;
    virtual void revealTemplateFile() = 0;

    // --- analysis -----------------------------------------------------------
    virtual void exportSweepCsv() = 0;

    // --- panels -------------------------------------------------------------
    virtual void resetPanelLayout() = 0;
};

} // namespace suspkin
