#include "app/session/SimulationRunner.h"

namespace suspkin {

void SimulationRunner::bind(const LinkageTemplate& templ, const HardpointTable& table,
                            const MirrorSpec& mirror,
                            const QHash<QString, StaticAlignment>& alignment,
                            const QString& steeringNote)
{
    m_simulation = Simulation::build(templ, table, mirror, alignment, steeringNote);
}

void SimulationRunner::runSweep(const SimulationRequest& request)
{
    const AxleSolver* axle = m_simulation.axleFor(request.axle);
    m_sweep = (request.sweepWanted && axle) ? suspkin::runSweep(*axle, request.spec) : SweepResult{};
}

void SimulationRunner::pose(const SimulationRequest& request)
{
    m_pose = SimulationPose{};
    if (!request.simulating) return;
    m_pose = m_simulation.poseAt(request.axle, request.spec.kind, request.position,
                                 request.spec.rackTravel, request.moveAllAxles);
}

QStringList SimulationRunner::status() const
{
    QStringList lines;
    if (!m_simulation.note().isEmpty()) lines << m_simulation.note();
    // runSweep already carries the axle's own warnings, so they are not added
    // here a second time.
    lines += m_sweep.warnings;
    return lines;
}

} // namespace suspkin
