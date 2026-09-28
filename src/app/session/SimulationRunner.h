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
    /// The axle the position drives, and the one the readout shows.
    QString axle;
    /// The axles on the plot, by corner token: one sweep each.
    QStringList sweptAxles;
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
    /// Run the sweeps @p request asks for, or clear them when it wants none.
    void runSweep(const SimulationRequest& request);
    /// A sweep of every axle @p request plots, whether or not it wants them
    /// drawn -- which is what an export writes.
    std::vector<SweepResult> sweepsFor(const SimulationRequest& request) const;
    /// Put the car where @p request says, or back at the table's coordinates
    /// when it is not simulating.
    void pose(const SimulationRequest& request);

    const Simulation& simulation() const { return m_simulation; }
    /// The curves on the plot, one per axle shown.
    const std::vector<SweepResult>& sweeps() const { return m_sweeps; }
    /// Where the car is standing while simulating; empty the rest of the time.
    const SimulationPose& currentPose() const { return m_pose; }
    /// What the panel says under the plot: the solve's note, then the sweep's
    /// warnings.
    QStringList status() const;

private:
    /// Every axle the template names, bound to the table as it stands.
    Simulation m_simulation;
    std::vector<SweepResult> m_sweeps;
    SimulationPose m_pose;
};

} // namespace suspkin
