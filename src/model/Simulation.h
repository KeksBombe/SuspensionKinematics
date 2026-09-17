#pragma once

#include "io/LinkageTemplate.h"
#include "model/Sweep.h"
#include "model/Wheels.h"

#include <QHash>
#include <QString>

#include <optional>
#include <vector>

namespace suspkin {

/// Where the car is standing at one point of a sweep.
///
/// Nothing here is the table. A pose is laid *over* a table to be drawn, by
/// name, and the table itself is never changed to match -- which is what keeps
/// a simulation from ever reaching the edits file or a workbook.
struct SimulationPose {
    /// The axle the user is looking at first, then whichever others were along
    /// for the ride. Empty when nothing is being simulated.
    std::vector<AxleSample> samples;

    /// Where the body is, seen from the road, while it rolls: turned about the
    /// roll axis, which is what leaves the contact patches where they were.
    /// Nothing the rest of the time, which is the body where the table has it.
    std::optional<Rigid> bodyMotion;

    bool isEmpty() const { return samples.empty(); }

    /// @p design with every solved position laid over it by name, and -- while
    /// the body is rolled -- every point of it moved with the body.
    ///
    /// Every point, not only the ones the solve moved: the chassis pickups are
    /// on the body too, and a part is drawn between the two.
    HardpointTable layOver(const HardpointTable& design) const;

    /// How far each posed upright has turned, by the name of its wheel centre.
    /// Empty when nothing is being simulated, which leaves every wheel model at
    /// the attitude its CAD file drew it in.
    WheelRotations wheelRotations() const;
};

/// Every axle a linkage template describes, bound to the table as it stands.
///
/// This is the whole of what the window knows about solving: which axles there
/// are, which one is being looked at, and where the car stands when the travel
/// slider is somewhere. It holds no widgets and draws nothing, so a sweep and a
/// pose can both be checked without a graphics context -- see test_simulation.
class Simulation {
public:
    /// Bind @p templ's mechanism against @p table, once per corner the template
    /// names. Cheap -- it resolves names and measures link lengths -- so
    /// anything that changes a coordinate can call it.
    ///
    /// @p steeringNote is what the window has to say about steering: that a
    /// template which predates the role was filled in, or that one says nothing
    /// and every axle is therefore steerable. It is passed in rather than
    /// bolted on afterwards because a table with no complete axle in it
    /// replaces the note entirely, so the two have to be decided together.
    static Simulation build(const LinkageTemplate& templ, const HardpointTable& table,
                            const MirrorSpec& mirror,
                            const QHash<QString, StaticAlignment>& alignment,
                            const QString& steeringNote = QString());

    bool isEmpty() const { return m_axles.empty(); }
    const std::vector<AxleSolver>& axles() const { return m_axles; }

    /// The axle @p token names. The first one when nothing matches, because a
    /// panel asking about an axle that has gone still has to show something;
    /// nothing at all only when there are no axles.
    const AxleSolver* axleFor(const QString& token) const;

    /// What to tell the user about the solve: why there is no curve when there
    /// is none, or what was assumed to get one. Empty when there is nothing to
    /// say.
    const QString& note() const { return m_note; }

    /// Where the car stands with @p token's axle at @p input of a @p kind
    /// sweep.
    ///
    /// @p moveAllAxles brings the others along, so the car heaves and rolls as
    /// a car rather than as one axle with the rest of it left behind. Steering
    /// is the exception whatever is asked for: a rack belongs to one axle, and
    /// pushing a rear toe link with it would be inventing a rear-steer this car
    /// has not got.
    SimulationPose poseAt(const QString& token, SweepKind kind, double input, double rackTravel,
                          bool moveAllAxles) const;

private:
    std::vector<AxleSolver> m_axles;
    QString m_note;
};

} // namespace suspkin
