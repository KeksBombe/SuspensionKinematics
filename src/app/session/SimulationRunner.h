#pragma once

#include "model/Simulation.h"
#include "model/Sweep.h"

#include <QHash>
#include <QString>
#include <QStringList>

namespace suspkin {

struct HardpointTable;
struct LinkageTemplate;
struct MirrorSpec;

/// What the user is asking the solve for, as plain data. The analysis panel is
/// a widget, so the runner never sees it: the window reads this off the panel.
struct SimulationRequest {
    QString axle;
    SweepSpec spec;
    double position = 0.0;
    bool moveAllAxles = false;
    bool simulating = false;
    /// The sweep is the most expensive thing here, and there is nothing to draw
    /// a curve on while the dock is shut.
    bool sweepWanted = true;
};

/// The bound axles, the sweep on the plot and the pose the car stands in.
///
/// All of it is derived -- from the template, the table and the request -- so
/// there is nothing of it to save, and nothing here ever changes the table.
class SimulationRunner {
public:
    /// Bind every axle the template names against the table as it stands.
    void bind(const LinkageTemplate& templ, const HardpointTable& table, const MirrorSpec& mirror,
              const QHash<QString, StaticAlignment>& alignment, const QString& steeringNote);
    /// Run the sweep @p request asks for, or clear it when it wants none.
    void runSweep(const SimulationRequest& request);
    /// Put the car where @p request says, or back at the table's coordinates
    /// when it is not simulating.
    void pose(const SimulationRequest& request);

    const Simulation& simulation() const { return m_simulation; }
    /// The curve on the plot.
    const SweepResult& sweep() const { return m_sweep; }
    /// Where the car is standing while simulating; empty the rest of the time.
    const SimulationPose& currentPose() const { return m_pose; }
    /// What the panel says under the plot: the solve's note, then the sweep's
    /// warnings.
    QStringList status() const;

private:
    /// Every axle the template names, bound to the table as it stands.
    Simulation m_simulation;
    SweepResult m_sweep;
    SimulationPose m_pose;
};

} // namespace suspkin
