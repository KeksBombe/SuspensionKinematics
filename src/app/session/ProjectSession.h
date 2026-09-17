#pragma once

#include "app/session/ChassisDocument.h"
#include "app/session/HardpointDocument.h"
#include "app/session/LinkageDocument.h"
#include "app/session/SimulationRunner.h"
#include "app/session/WheelsDocument.h"
#include "model/EditHistory.h"
#include "project/Project.h"

#include <QObject>

#include <functional>

class QTimer;

namespace suspkin {

/// The live state one open project is unfolded into.
///
/// The Project is what is saved; the documents are what it is read into -- the
/// chassis, the points and their workbook, the template and its parts, the solve,
/// the wheels -- and this is what holds them together. Nothing here is a widget:
/// what the window has to draw is announced through the signals below, in the
/// order the window has to draw it, and what the user has to be told comes back
/// as a SessionMessage.
///
/// Beyond holding things, its one job is the cascade: when the table moves,
/// everything resolved against the table is resolved again.
class ProjectSession : public QObject {
    Q_OBJECT

public:
    explicit ProjectSession(Project project, QObject* parent = nullptr);
    ~ProjectSession() override;

    Project& project() { return m_project; }
    const Project& project() const { return m_project; }

    ChassisDocument& chassis() { return m_chassis; }
    HardpointDocument& hardpoints() { return m_hardpoints; }
    const HardpointDocument& hardpoints() const { return m_hardpoints; }
    LinkageDocument& linkage() { return m_linkage; }
    const LinkageDocument& linkage() const { return m_linkage; }
    SimulationRunner& simulation() { return m_simulation; }
    const SimulationRunner& simulation() const { return m_simulation; }
    WheelsDocument& wheels() { return m_wheels; }
    const WheelsDocument& wheels() const { return m_wheels; }

    // --- saving -------------------------------------------------------------
    /// Note that something worth persisting changed, and schedule a save. Inert
    /// while loading.
    void markDirty();
    /// A save has been scheduled and has not happened yet.
    bool savePending() const;
    /// Write the project out now: the pending edits, then the manifest. Whoever
    /// calls this has already put the view state into the project.
    bool save(QString* error);

    /// Set while a project is being opened, so restoring a saved value does not
    /// read back as a change the user made.
    bool loading() const { return m_loading; }
    void setLoading(bool loading) { m_loading = loading; }

    // --- undo and redo ------------------------------------------------------
    /// Every state the user's editing of the points has passed through. Not
    /// saved: where the session ended up is in the project already.
    EditHistory& history() { return m_history; }
    const EditHistory& history() const { return m_history; }
    /// What an edit of the points can change, as the project holds it now.
    EditState editState() const;
    /// Note in the history that an edit called @p label has just been made.
    void recordEdit(const QString& label);
    /// Start the history again from what the project holds now.
    void restartHistory();

    // --- the cascade ----------------------------------------------------------
    /// Where the solve reads what the user is asking of it -- which axle, how
    /// far, whether to simulate at all. Asked afresh at every step, because
    /// binding the axles can change which axle is being looked at.
    void setRequestSource(std::function<SimulationRequest()> source);

    /// Everything that is resolved against the table, resolved again: the
    /// parts, the configuration, the bound axles, the sweep, the pose, the
    /// wheels.
    void resolveFromTable();
    /// Bind the mechanism again, then run the sweep and the pose. What a moved
    /// coordinate or a changed static angle needs.
    void resolveMechanism();
    /// The half the analysis panel drives: the sweep, then the pose.
    void resolveSimulation();
    /// Run the sweep the request asks for.
    void runSweep();
    /// Put the mechanism where the request says, then the wheels on it.
    void resolvePose();
    /// Place the wheel models on the table as it is posed now.
    void placeWheels();

signals:
    /// The debounced save is due. The window collects its view state and saves.
    void saveDue();
    /// The template was resolved against the table again: new parts.
    void partsResolved();
    /// The axles were bound again: the panel's axle box has to follow, before
    /// anything asks it which axle is chosen.
    void axlesBound();
    /// A sweep was run, or cleared.
    void sweepRun();
    /// The mechanism was posed, or put back where the table has it.
    void posed();
    /// The wheels were placed again.
    void wheelsPlaced();
    /// A whole resolve from the table is finished.
    void resolved();
    /// A step was recorded or the history restarted: Undo and Redo follow.
    void historyChanged();

private:
    SimulationRequest request() const;

    Project m_project;
    ChassisDocument m_chassis{ *this };
    HardpointDocument m_hardpoints{ *this };
    LinkageDocument m_linkage{ *this };
    SimulationRunner m_simulation;
    WheelsDocument m_wheels{ *this };
    EditHistory m_history;

    std::function<SimulationRequest()> m_requestSource;
    QTimer* m_saveTimer = nullptr;
    bool m_loading = false;
};

} // namespace suspkin
