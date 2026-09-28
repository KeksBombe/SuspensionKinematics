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
    m_sweeps = request.sweepWanted ? sweepsFor(request) : std::vector<SweepResult>{};
}

std::vector<SweepResult> SimulationRunner::sweepsFor(const SimulationRequest& request) const
{
    std::vector<SweepResult> sweeps;
    for (const QString& token : request.sweptAxles)
        if (const AxleSolver* axle = m_simulation.axleFor(token))
            sweeps.push_back(suspkin::runSweep(*axle, request.spec));
    return sweeps;
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
    for (const SweepResult& sweep : m_sweeps)
        for (const QString& warning : sweep.warnings)
            if (!lines.contains(warning)) lines << warning;
    return lines;
}

} // namespace suspkin
