#include "app/session/ProjectSession.h"

#include <QTimer>

#include <utility>

namespace suspkin {
namespace {

/// How long after the last change the project is written. Long enough that an
/// orbit drag is one save rather than two hundred, short enough that closing the
/// lid on a laptop a second later loses nothing.
constexpr int kAutoSaveDelayMs = 1200;

} // namespace

ProjectSession::ProjectSession(Project project, QObject* parent)
    : QObject(parent), m_project(std::move(project))
{
    // One timer for the whole session: everything that changes calls
    // markDirty() and the project is written once, shortly after the user stops.
    m_saveTimer = new QTimer(this);
    m_saveTimer->setSingleShot(true);
    m_saveTimer->setInterval(kAutoSaveDelayMs);
    connect(m_saveTimer, &QTimer::timeout, this, &ProjectSession::saveDue);
}

ProjectSession::~ProjectSession() = default;

// ---------------------------------------------------------------------------
// Saving
// ---------------------------------------------------------------------------

void ProjectSession::markDirty()
{
    if (m_loading) return;
    m_saveTimer->start();
}

bool ProjectSession::savePending() const
{
    return m_saveTimer->isActive();
}

bool ProjectSession::save(QString* error)
{
    m_saveTimer->stop();
    // The edits file first: if the manifest lands and the edits do not, the
    // project would come back claiming there is nothing to write to the workbook.
    if (!m_hardpoints.writePendingEdits(error)) return false;
    return m_project.save(error);
}

// ---------------------------------------------------------------------------
// Undo and redo
// ---------------------------------------------------------------------------

EditState ProjectSession::editState() const
{
    return EditState{ m_hardpoints.model().table(), m_hardpoints.model().config(),
                      m_project.alignment() };
}

void ProjectSession::recordEdit(const QString& label)
{
    m_history.record(label, editState());
    // Undo and Redo say what they would do, so they follow every step.
    emit historyChanged();
}

void ProjectSession::restartHistory()
{
    m_history.reset(editState());
    emit historyChanged();
}

// ---------------------------------------------------------------------------
// The cascade
// ---------------------------------------------------------------------------

void ProjectSession::setRequestSource(std::function<SimulationRequest()> source)
{
    m_requestSource = std::move(source);
}

SimulationRequest ProjectSession::request() const
{
    return m_requestSource ? m_requestSource() : SimulationRequest{};
}

void ProjectSession::resolveFromTable()
{
    const HardpointTable& table = m_hardpoints.model().table();
    m_linkage.resolve(table);
    emit partsResolved();

    // The configuration table is resolved against the same two things the parts
    // are, so whatever changed there changed this too. What was inferred is the
    // user's from the moment it is in front of them, so it is saved.
    if (m_hardpoints.refreshConfig(m_linkage.linkageTemplate()) > 0) markDirty();

    // The mechanism is bound to the same two things the parts are -- this
    // template and this table -- so it is rebound in the same breath. Doing it
    // here rather than at each call site is what keeps it independent of the
    // order a project happens to load its pieces in: the points are read before
    // the template, so binding at the point the table arrives would bind against
    // a template that is not there yet. Posing places the wheels as well.
    resolveMechanism();
    emit resolved();
}

void ProjectSession::resolveMechanism()
{
    m_simulation.bind(m_linkage.linkageTemplate(), m_hardpoints.model().table(),
                      m_project.mirror(), m_project.alignment(), m_linkage.steeringNote());
    emit axlesBound();
    resolveSimulation();
}

void ProjectSession::resolveSimulation()
{
    runSweep();
    resolvePose();
}

void ProjectSession::runSweep()
{
    m_simulation.runSweep(request());
    emit sweepRun();
}

void ProjectSession::resolvePose()
{
    m_simulation.pose(request());
    emit posed();
    // A wheel centre that the solver moved takes its wheel with it.
    placeWheels();
}

void ProjectSession::placeWheels()
{
    m_wheels.place(m_hardpoints.model().table(), m_simulation.simulation().designWheelAttitudes(),
                   m_simulation.currentPose());
    emit wheelsPlaced();
}

} // namespace suspkin
