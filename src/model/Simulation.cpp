#include "model/Simulation.h"

#include <QCoreApplication>

namespace suspkin {
namespace {

QString tr(const char* text) { return QCoreApplication::translate("Simulation", text); }

} // namespace

// ---------------------------------------------------------------------------
// SimulationPose
// ---------------------------------------------------------------------------

HardpointTable SimulationPose::layOver(const HardpointTable& design) const
{
    HardpointTable table = design;
    for (const AxleSample& sample : samples)
        for (const CornerPose* pose : { &sample.left, &sample.right }) {
            if (!pose->valid) continue;
            for (const PosedPoint& posed : pose->points) {
                const int index = table.indexOf(posed.name);
                if (index < 0) continue;
                Hardpoint& point = table.points[static_cast<std::size_t>(index)];
                point.coord[0] = posed.position.x;
                point.coord[1] = posed.position.y;
                point.coord[2] = posed.position.z;
            }
        }

    // Everything, not only what the solve moved: the chassis pickups are on the
    // body too, and the parts are drawn between the two.
    if (bodyMotion) {
        for (Hardpoint& point : table.points) {
            const Vec3 moved =
                bodyMotion->map(Vec3(point.coord[0], point.coord[1], point.coord[2]));
            point.coord[0] = moved.x;
            point.coord[1] = moved.y;
            point.coord[2] = moved.z;
        }
    }
    return table;
}

WheelRotations SimulationPose::wheelRotations() const
{
    WheelRotations rotations;
    for (const AxleSample& sample : samples)
        for (const CornerPose* pose : { &sample.left, &sample.right }) {
            if (!pose->valid || pose->wheelCenterName.isEmpty()) continue;
            rotations.insert(pose->wheelCenterName, pose->uprightMotion.toQuaternion());
        }
    return rotations;
}

// ---------------------------------------------------------------------------
// Simulation
// ---------------------------------------------------------------------------

Simulation Simulation::build(const LinkageTemplate& templ, const HardpointTable& table,
                             const MirrorSpec& mirror,
                             const QHash<QString, StaticAlignment>& alignment,
                             const QString& steeringNote)
{
    Simulation simulation;

    // A template written before the solver existed still draws perfectly well;
    // it simply does not say which point plays which role. The reader fills that
    // in from the built-in block so that everything reading a template sees the
    // same roles -- the solve here, and what each hardpoint is for in the
    // configuration table. All that is left to do is say so.
    const MechanismTemplate& mechanism = templ.mechanism;
    if (templ.mechanismAssumed) {
        simulation.m_note =
            tr("This project's template does not say which hardpoint plays which role, "
               "so the built-in mechanism is being used, here and in the hardpoint "
               "table. Linkage > Reset to Built-in Template writes it into the file.");
    }
    if (!steeringNote.isEmpty()) {
        simulation.m_note = simulation.m_note.isEmpty()
                                ? steeringNote
                                : simulation.m_note + QLatin1Char('\n') + steeringNote;
    }

    if (!mechanism.isEmpty() && !table.isEmpty()) {
        std::vector<CornerSpec> corners = templ.corners;
        // A template with no corners spells its point names out in full, which
        // is one axle rather than none.
        if (corners.empty()) corners.push_back(CornerSpec{});
        // Whether the template says anything at all about steering. It is asked
        // once, for the whole file: a template that says nothing leaves every
        // axle steered, which is what every project made before the role existed
        // has always done.
        const bool steeringDeclared = templ.steeringDeclared();
        for (const CornerSpec& corner : corners) {
            // Static camber and toe the project states win over whatever the
            // table's wheel axis or contact patch would have said.
            const auto stated = alignment.constFind(corner.token);
            AxleSolver axle = AxleSolver::build(
                mechanism, corner, table, mirror, steeringDeclared,
                stated == alignment.constEnd() ? std::nullopt
                                               : std::optional<StaticAlignment>(*stated));
            if (axle.isEmpty()) continue;
            simulation.m_axles.push_back(std::move(axle));
        }
        // Ackermann is measured against the far axle, which only the whole list
        // of them knows about.
        assignWheelbases(simulation.m_axles);
    }

    // An axle-less table replaces the note rather than adding to it: whatever
    // was assumed about the mechanism does not matter while there is nothing to
    // solve, and the reason there is nothing is what the user needs.
    if (simulation.m_axles.empty()) {
        simulation.m_note = table.isEmpty()
                                ? tr("Import hardpoints to simulate.")
                                : tr("No axle in this table resolves to a complete mechanism. The "
                                     "solver needs both wishbones, the tie rod and a wheel centre.");
    }
    return simulation;
}

const AxleSolver* Simulation::axleFor(const QString& token) const
{
    if (m_axles.empty()) return nullptr;
    for (const AxleSolver& axle : m_axles)
        if (axle.cornerToken() == token) return &axle;
    return &m_axles.front();
}

SimulationPose Simulation::poseAt(const QString& token, SweepKind kind, double input,
                                  double rackTravel, bool moveAllAxles) const
{
    SimulationPose pose;
    const AxleSolver* selected = axleFor(token);
    if (!selected) return pose;

    pose.samples.push_back(sampleAxleAt(*selected, kind, input, rackTravel));

    // The other axles ride along, so the car heaves and rolls as a car rather
    // than as one axle with the rest of it left behind. Steering is the
    // exception: a rack belongs to one axle, and pushing a rear toe link with
    // it would be inventing a rear-steer this car has not got.
    if (moveAllAxles && kind != SweepKind::Steer) {
        for (const AxleSolver& axle : m_axles) {
            if (&axle == selected) continue;
            pose.samples.push_back(sampleAxleAt(axle, kind, input, rackTravel));
        }
    }

    // The sweep has the road tilting under a car that stays put, which is right
    // for its numbers and wrong to look at: a car in a corner rolls on a level
    // road. So the whole of it -- monocoque, every point, every wheel -- is
    // drawn turned about the roll axis, which is what keeps the tyres on the
    // road where they were. Only when every axle is following, though: a body
    // cannot roll with an axle left behind, and drawing it would put that
    // axle's wheels through the road.
    if (kind == SweepKind::Roll && pose.samples.size() == m_axles.size())
        pose.bodyMotion = bodyRollMotion(rollAxisThrough(m_axles), input);

    return pose;
}

} // namespace suspkin
