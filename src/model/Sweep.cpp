#include "model/Sweep.h"

#include "model/GeomSolve.h"

#include <QCoreApplication>

#include <algorithm>
#include <cmath>
#include <iterator>

namespace suspkin {
namespace {

QString tr(const char* text) { return QCoreApplication::translate("Sweep", text); }

constexpr double kDegToRad = 0.017453292519943295769236907684886;
constexpr double kRadToDeg = 57.295779513082320876798154814105;

/// A number for the CSV: enough digits to be worth having, none of the noise
/// that seventeen of them would bring.
QByteArray field(double value)
{
    return QByteArray::number(value, 'f', 4);
}

} // namespace

QString sweepKindToString(SweepKind kind)
{
    switch (kind) {
    case SweepKind::Bump: return QStringLiteral("bump");
    case SweepKind::Roll: return QStringLiteral("roll");
    case SweepKind::Steer: return QStringLiteral("steer");
    }
    return QStringLiteral("bump");
}

SweepKind sweepKindFromString(const QString& text, SweepKind fallback)
{
    const QString key = text.trimmed().toLower();
    if (key == QLatin1String("bump")) return SweepKind::Bump;
    if (key == QLatin1String("roll")) return SweepKind::Roll;
    if (key == QLatin1String("steer")) return SweepKind::Steer;
    return fallback;
}

QString sweepInputUnit(SweepKind kind)
{
    return kind == SweepKind::Roll ? QStringLiteral("deg") : QStringLiteral("mm");
}

QString sweepInputLabel(SweepKind kind)
{
    switch (kind) {
    case SweepKind::Bump: return tr("Wheel travel");
    case SweepKind::Roll: return tr("Body roll");
    case SweepKind::Steer: return tr("Rack travel");
    }
    return tr("Input");
}

bool SweepSpec::operator==(const SweepSpec& other) const
{
    return kind == other.kind && from == other.from && to == other.to && steps == other.steps
           && rackTravel == other.rackTravel;
}

double SweepSpec::inputAt(int index) const
{
    if (steps <= 1) return from;
    const int clamped = std::clamp(index, 0, steps - 1);
    return from + (to - from) * (static_cast<double>(clamped) / static_cast<double>(steps - 1));
}

namespace {

/// How many solved positions a range of @p span comes to at @p increment.
///
/// Two at the bottom, because a sweep of one position is not a sweep; and
/// @ref kMaxSweepSteps at the top, because an increment left at nothing would
/// otherwise ask for an unbounded number of solves.
int stepsForIncrement(double span, double increment)
{
    if (!(span > 0.0) || !(increment > 0.0)) return 2;
    const double count = std::floor(span / increment + 0.5) + 1.0;
    if (count >= double(kMaxSweepSteps)) return kMaxSweepSteps;
    return std::max(2, static_cast<int>(count));
}

} // namespace

bool SweepSettings::operator==(const SweepSettings& other) const
{
    return bumpTravel == other.bumpTravel && reboundTravel == other.reboundTravel
           && bumpIncrement == other.bumpIncrement && rollAngle == other.rollAngle
           && rollIncrement == other.rollIncrement && steerTravel == other.steerTravel
           && steerIncrement == other.steerIncrement && rackTravel == other.rackTravel;
}

double SweepSettings::incrementFor(SweepKind kind) const
{
    switch (kind) {
    case SweepKind::Bump: return bumpIncrement;
    case SweepKind::Roll: return rollIncrement;
    case SweepKind::Steer: return steerIncrement;
    }
    return bumpIncrement;
}

SweepSpec SweepSettings::specFor(SweepKind kind) const
{
    SweepSpec spec;
    spec.kind = kind;
    // The rack is held through bump and roll and swept in steer, where the
    // range below is what moves it. Carrying it either way costs nothing and
    // keeps the spec a complete description of the run.
    spec.rackTravel = rackTravel;

    switch (kind) {
    case SweepKind::Bump:
        // Rebound is downward however it is written, so a user who types the
        // minus sign in gets what they meant rather than a sweep that only
        // goes up.
        spec.from = -std::abs(reboundTravel);
        spec.to = std::abs(bumpTravel);
        break;
    case SweepKind::Roll:
        spec.from = -std::abs(rollAngle);
        spec.to = std::abs(rollAngle);
        break;
    case SweepKind::Steer:
        spec.from = -std::abs(steerTravel);
        spec.to = std::abs(steerTravel);
        break;
    }
    spec.steps = stepsForIncrement(spec.to - spec.from, incrementFor(kind));
    return spec;
}

namespace {

/// What the template gives this axle that neither of its bound sides has --
/// the rocker, the bar -- said once for the axle, with the measures it costs.
/// Nothing when every group is there, or when no side bound at all: that
/// axle already has its own warning, or none is due.
QString missingGroupsNote(const AxleSolver& axle, const MechanismTemplate& mechanism)
{
    const CornerSolver* bound = axle.left() ? &*axle.left() : axle.right() ? &*axle.right() : nullptr;
    if (!bound) return QString();
    QStringList lost;
    if (mechanism.hasRocker() && !bound->hasDamper())
        lost << (bound->hasRocker() ? tr("no damper, so damper length, damper travel and the "
                                         "installation ratio are not available")
                                    : tr("no complete pushrod and rocker, so damper length, "
                                         "damper travel and the installation ratio are not "
                                         "available"));
    if (mechanism.hasAntiRoll() && !bound->hasAntiRoll())
        lost << tr("no complete anti-roll bar, so its twist is not available");
    if (lost.isEmpty()) return QString();
    return tr("%1: %2. Camber, toe and the rest of the wheel's kinematics do not depend on them.")
        .arg(axle.label(), lost.join(QStringLiteral("; ")));
}

} // namespace

AxleSolver AxleSolver::build(const MechanismTemplate& mechanism, const CornerSpec& corner,
                             const HardpointTable& table, const MirrorSpec& mirror,
                             bool steeringDeclared,
                             const std::optional<StaticAlignment>& alignment)
{
    AxleSolver axle;
    axle.m_token = corner.token;
    axle.m_label = corner.label.isEmpty() ? corner.token : corner.label;

    // Which axle has a rack is the corner's own business, not the mechanism
    // block's -- that block is one block for every corner. Putting the name in
    // here rather than teaching the solver about corners means it goes through
    // the same {corner} substitution and the same mirror rule as every other
    // role, and the solver keeps knowing only about names.
    //
    // A template that says nothing anywhere leaves every axle steered, which is
    // what every project made before this existed has always done.
    MechanismTemplate roles = mechanism;
    roles.steeringRack = steeringDeclared ? corner.steeringRack : mechanism.tieRodInboard;
    if (steeringDeclared && !roles.steeringRack.isEmpty()
        && roles.steeringRack != mechanism.tieRodInboard) {
        // A rack that picks up anywhere else is a steering linkage -- an idler,
        // a drag link -- and this solve has no such body in it. Saying so beats
        // moving a point nothing is attached to and calling it steering.
        // Named the way the table names them, not with {corner} still in them:
        // the user has to be able to go and look at the point.
        const MechanismTemplate shown = instantiateMechanism(roles, corner.token, false, mirror);
        axle.m_warnings << tr("%1: the steering rack is named as %2, but this model steers by "
                              "moving the inboard tie rod end (%3). The rack is ignored.")
                               .arg(axle.m_label, shown.steeringRack, shown.tieRodInboard);
        roles.steeringRack.clear();
    }

    std::optional<CornerSolver> sides[2];
    for (int side = 0; side < 2; ++side) {
        const bool mirrored = (side == 1);
        // Read against the table, so a point it still holds under a former name
        // is found under that one.
        const MechanismTemplate named =
            instantiateMechanism(roles, corner.token, mirrored, mirror, table);
        if (named.isEmpty()) continue;

        // A corner or a side with not one of its points in the table is not a
        // corner that is missing something -- it is a workbook holding one axle,
        // or one that has not been mirrored yet. Saying so would be noise, and
        // the noise would bury the corners that really are half there.
        const MechanismCoverage coverage = coverMechanism(named, table);
        if (coverage.absent) continue;

        QString error;
        std::optional<CornerSolver> solver = CornerSolver::bind(named, table, &error, alignment);
        if (!solver) {
            axle.m_warnings << tr("%1 %2: %3")
                                   .arg(axle.m_label,
                                        mirrored ? tr("far side") : tr("near side"), error);
            continue;
        }
        sides[side] = std::move(solver);
    }

    // Which of the two is on the left is decided by where the wheel centre
    // actually is, never by which one the template happened to write out.
    for (std::optional<CornerSolver>& solver : sides) {
        if (!solver) continue;
        std::optional<CornerSolver>& slot = solver->isLeft() ? axle.m_left : axle.m_right;
        if (slot) {
            axle.m_warnings << tr("%1: both sides resolve to the same side of the car. Check the "
                                  "mirror rule.")
                                   .arg(axle.m_label);
            continue;
        }
        slot = std::move(solver);
    }

    const QString missing = missingGroupsNote(axle, mechanism);
    if (!missing.isEmpty()) axle.m_warnings << missing;

    // A U-bar's axis runs from one arm root to the other, and only an axle knows
    // both. Without the far side it stays the y direction, which is what a
    // transverse bar is anyway. A side whose table names the bar's bearing has
    // its own axis already, and keeps it.
    if (axle.m_left && axle.m_right) {
        const MechanismTemplate& leftNames = axle.m_left->mechanism();
        const MechanismTemplate& rightNames = axle.m_right->mechanism();
        const Hardpoint* leftPivot = table.find(leftNames.antiRollArmRoot);
        const Hardpoint* rightPivot = table.find(rightNames.antiRollArmRoot);
        if (leftPivot && rightPivot) {
            const Vec3 leftPoint(leftPivot->coord[0], leftPivot->coord[1], leftPivot->coord[2]);
            const Vec3 rightPoint(rightPivot->coord[0], rightPivot->coord[1], rightPivot->coord[2]);
            // One direction for both arms, pointing left. Handing each side the
            // other's pivot would give them opposite directions, and then a pure
            // bump -- where the bar simply turns in its bearings -- would read
            // as the two arms twisting against each other.
            const Vec3 along = leftPoint - rightPoint;
            axle.m_left->setAntiRollAxis(along);
            axle.m_right->setAntiRollAxis(along);
        }
    }

    return axle;
}

bool AxleSolver::isSteered() const
{
    // Either side is enough: the two are the same corner mirrored, so a rack
    // that drives one drives the other, and a half-mirrored table should not
    // read as half a steering system.
    return (m_left && m_left->isSteered()) || (m_right && m_right->isSteered());
}

namespace {

/// Where the ground is under an axle at design, and how far along the car the
/// axle is: the mean of its contact patches, or the one it has. False for an
/// axle with neither side.
bool axleFootprint(const AxleSolver& axle, double* x, double* ground)
{
    double sumX = 0.0;
    double sumZ = 0.0;
    int count = 0;
    for (const std::optional<CornerSolver>* side : { &axle.left(), &axle.right() }) {
        if (!*side) continue;
        const Vec3& patch = (*side)->designPose().contactPatch;
        sumX += patch.x;
        sumZ += patch.z;
        ++count;
    }
    if (count == 0) return false;
    *x = sumX / count;
    *ground = sumZ / count;
    return true;
}

/// Where an axle stands fore and aft, which is all Ackermann asks of it.
bool axleStation(const AxleSolver& axle, double* x)
{
    double ground = 0.0;
    return axleFootprint(axle, x, &ground);
}

/// Below this the inner wheel is taken as pointing straight ahead. Both halves
/// of the Ackermann ratio shrink with the square of the steer angle, so this is
/// where the ratio stops being about the car.
constexpr double kMinAckermannSteerDeg = 0.1;

} // namespace

void assignWheelbases(std::vector<AxleSolver>& axles)
{
    // Farthest rather than "the one without a rack": a turn is centred on the
    // line of the axle that does not steer, but a template that says nothing
    // about steering has every axle steered, and on a two-axle car the answer
    // is the other axle either way.
    for (AxleSolver& axle : axles) {
        double wheelbase = 0.0;
        double here = 0.0;
        if (axleStation(axle, &here)) {
            for (const AxleSolver& other : axles) {
                double there = 0.0;
                if (&other == &axle || !axleStation(other, &there)) continue;
                wheelbase = std::max(wheelbase, std::abs(here - there));
            }
        }
        axle.setWheelbase(wheelbase);
    }
}

bool ackermannPercent(double innerDeg, double outerDeg, double wheelbase, double track,
                      double* percent)
{
    if (!(wheelbase > 0.0) || !(track > 0.0) || !(innerDeg >= kMinAckermannSteerDeg))
        return false;
    const double inner = innerDeg * kDegToRad;
    // cot(idealOuter) = cot(inner) + track / wheelbase, written with a sine and
    // a cosine so it does not go through a tangent that is infinite at ninety.
    const double idealOuter =
        std::atan2(std::sin(inner), std::cos(inner) + track / wheelbase * std::sin(inner));
    const double idealDifference = inner - idealOuter;
    if (!(idealDifference > 0.0)) return false;
    *percent = 100.0 * (inner - outerDeg * kDegToRad) / idealDifference;
    return true;
}

const AxleSample* SweepResult::nearest(double input) const
{
    const AxleSample* best = nullptr;
    double bestDistance = 0.0;
    for (const AxleSample& sample : samples) {
        const double d = std::abs(sample.input - input);
        if (!best || d < bestDistance) {
            best = &sample;
            bestDistance = d;
        }
    }
    return best;
}

namespace {

/// Whether every corner that assembled at @p design assembled in @p sample too.
bool assembledLike(const AxleSample& sample, const AxleSample& design)
{
    return (!design.left.valid || sample.left.valid) && (!design.right.valid || sample.right.valid);
}

} // namespace

std::optional<SweepInterval> assembledInterval(const SweepResult& result)
{
    const AxleSample* design = result.nearest(0.0);
    if (!design || !(design->left.valid || design->right.valid)) return std::nullopt;

    std::vector<const AxleSample*> byInput;
    byInput.reserve(result.samples.size());
    for (const AxleSample& sample : result.samples) byInput.push_back(&sample);
    std::sort(byInput.begin(), byInput.end(),
              [](const AxleSample* a, const AxleSample* b) { return a->input < b->input; });

    const auto at = std::find(byInput.begin(), byInput.end(), design);
    auto first = at;
    while (first != byInput.begin() && assembledLike(**(first - 1), *design)) --first;
    auto last = at;
    while (last + 1 != byInput.end() && assembledLike(**(last + 1), *design)) ++last;
    return SweepInterval{ (*first)->input, (*last)->input };
}

namespace {

/// One corner posed for one point of one kind of sweep.
CornerPose poseFor(const CornerSolver& solver, SweepKind kind, double input, double rackTravel,
                   const CornerPose* previous)
{
    switch (kind) {
    case SweepKind::Bump:
        return solver.poseAtWheelTravel(input, rackTravel, previous);
    case SweepKind::Roll: {
        // The body rolls, so in body coordinates the ground tilts the other way
        // and each contact patch sits at a new height on it. Positive roll is a
        // right-hand rotation about x, which drops the left-hand contact patch
        // and so puts the left wheel into droop.
        const double angle = input * kDegToRad;
        const double y = solver.designPose().contactPatch.y;
        CornerPose pose =
            solver.poseAtContactPatchRise(-y * std::tan(angle), rackTravel, previous);
        // That tilted ground is z = -y tan(roll), whose upward normal is this.
        // Camber against it is what the tyre sees, and what a picture of the
        // car rolled on a level road shows; camber against the body is what
        // the suspension did, and the two part by the whole roll angle.
        const Vec3 up(0.0, std::sin(angle), std::cos(angle));
        pose.camberToGround =
            -std::asin(std::clamp(dot(pose.spinAxis, up), -1.0, 1.0)) * kRadToDeg;
        return pose;
    }
    case SweepKind::Steer:
        return solver.poseAtWheelTravel(0.0, input, previous);
    }
    return CornerPose{};
}

/// Where a side's contact-patch-to-instant-centre line meets the car's centre
/// plane. The one-sided fallback for a roll centre, and exactly what a symmetric
/// axle's construction reduces to.
bool centrePlaneCrossing(const CornerPose& pose, double* height)
{
    if (!pose.valid || !pose.instantCenterValid) return false;
    bool ok = false;
    const Vec3 hit = linePlaneCrossing(pose.contactPatch, pose.instantCenter, 1, 0.0, &ok);
    if (!ok) return false;
    *height = hit.z - pose.contactPatch.z;
    return true;
}

void computeRollCentre(AxleSample* sample)
{
    const bool haveLeft = sample->left.valid && sample->left.instantCenterValid;
    const bool haveRight = sample->right.valid && sample->right.instantCenterValid;

    if (haveLeft && haveRight) {
        bool ok = false;
        const Vec3 centre =
            intersectLines2D(sample->left.contactPatch, sample->left.instantCenter,
                             sample->right.contactPatch, sample->right.instantCenter, 0, &ok);
        if (ok) {
            const double ground =
                0.5 * (sample->left.contactPatch.z + sample->right.contactPatch.z);
            sample->rollCenterHeight = centre.z - ground;
            sample->rollCenterLateral = centre.y;
            sample->rollCenterValid = true;
            return;
        }
        // Parallel lines are not a failure: the arms are parallel and the roll
        // centre is at ground level, infinitely far out.
        sample->rollCenterHeight = 0.0;
        sample->rollCenterLateral = 0.0;
        sample->rollCenterValid = true;
        return;
    }

    double height = 0.0;
    const CornerPose& only = haveLeft ? sample->left : sample->right;
    if ((haveLeft || haveRight) && centrePlaneCrossing(only, &height)) {
        sample->rollCenterHeight = height;
        sample->rollCenterLateral = 0.0;
        sample->rollCenterValid = true;
    }
}

void computeAckermann(AxleSample* sample, const AxleSolver& axle, SweepKind kind)
{
    sample->ackermannValid = false;
    // Only a steer sweep turns the wheels by the rack alone. Toe also changes
    // over a bump, but a percentage read off bump steer is not a number anybody
    // designs to.
    if (kind != SweepKind::Steer || !axle.hasBothSides()) return;
    if (!sample->left.valid || !sample->right.valid) return;

    // Each wheel's steer angle, positive to the left. A steer sweep runs at
    // design ride height with the design pose as rack centre, so this is the
    // toe change with the sign each side reads toe in taken out: toe-in points
    // a left wheel to the right and a right wheel to the left.
    const double left = -sample->left.toeChange;
    const double right = sample->right.toeChange;

    // The inner wheel is the one on the side the car turns towards, which is
    // not necessarily the one turned furthest -- that is the whole question.
    const bool turningLeft = left + right > 0.0;
    const double inner = turningLeft ? left : -right;
    const double outer = turningLeft ? right : -left;
    const double track =
        axle.left()->designPose().contactPatch.y - axle.right()->designPose().contactPatch.y;
    sample->ackermannValid =
        ackermannPercent(inner, outer, axle.wheelbase(), track, &sample->ackermann);
}

/// Millimetres the damper closes per millimetre the wheel rises, by central
/// difference against the neighbouring samples. Differentiating the curve
/// rather than the mechanism keeps it honest about what was actually solved.
///
/// Compression per bump, so a damper that bump compresses -- which is every
/// damper that does its job -- reads positive, the way a motion ratio is quoted.
/// This was the length change per bump until 2026-09-12, which is the same
/// number with the sign turned round: a car whose damper closes a millimetre
/// for every millimetre of wheel travel read -1.
double installationRatio(const CornerPose& before, const CornerPose& after)
{
    if (!before.valid || !after.valid || !before.hasDamper || !after.hasDamper) return 0.0;
    const double travel = after.wheelTravel - before.wheelTravel;
    if (std::abs(travel) < 1e-9) return 0.0;
    return (before.damperLength - after.damperLength) / travel;
}

} // namespace

std::optional<Vec3> AxleSolver::designRollCentre() const
{
    AxleSample sample;
    if (m_left) sample.left = m_left->designPose();
    if (m_right) sample.right = m_right->designPose();
    computeRollCentre(&sample);

    double x = 0.0;
    double ground = 0.0;
    if (!sample.rollCenterValid || !axleFootprint(*this, &x, &ground)) return std::nullopt;
    // The construction gives a height above the ground the patches stand on and
    // an offset from the centreline; as a point it is those two, at the axle.
    return Vec3(x, sample.rollCenterLateral, ground + sample.rollCenterHeight);
}

RollAxis rollAxisThrough(const std::vector<AxleSolver>& axles)
{
    std::vector<Vec3> centres;
    for (const AxleSolver& axle : axles)
        if (const std::optional<Vec3> centre = axle.designRollCentre()) centres.push_back(*centre);

    RollAxis axis;
    if (centres.empty()) {
        // Nothing to construct one from, so the car rolls about the line the
        // sweep itself tilts the ground about: the centreline, at the ground.
        double sumX = 0.0;
        double sumZ = 0.0;
        int count = 0;
        for (const AxleSolver& axle : axles) {
            double x = 0.0;
            double ground = 0.0;
            if (!axleFootprint(axle, &x, &ground)) continue;
            sumX += x;
            sumZ += ground;
            ++count;
        }
        if (count == 0) return axis;
        axis.origin = Vec3(sumX / count, 0.0, sumZ / count);
        axis.direction = Vec3(1.0, 0.0, 0.0);
        axis.valid = true;
        return axis;
    }

    // The front-most and the rear-most, which on anything with two axles is
    // simply both of them.
    const auto byX = [](const Vec3& a, const Vec3& b) { return a.x < b.x; };
    const Vec3 rear = *std::min_element(centres.begin(), centres.end(), byX);
    const Vec3 front = *std::max_element(centres.begin(), centres.end(), byX);

    axis.origin = front;
    axis.direction = Vec3(1.0, 0.0, 0.0);
    // Two roll centres at the same station are one axle's worth of information,
    // not a line across the car: the axis would come out sideways and the body
    // would pitch instead of roll.
    if (front.x - rear.x > 1.0) axis.direction = (front - rear).normalized();
    axis.valid = true;
    return axis;
}

Rigid bodyRollMotion(const RollAxis& axis, double degrees)
{
    Rigid motion;
    if (!axis.valid) return motion;

    const double angle = degrees * kDegToRad;
    // The rotation's columns are where it takes the three unit directions, and
    // Rigid keeps rows; the translation is where it takes the origin, which is
    // what turning about a line through somewhere else amounts to.
    const Axis through{ Vec3(), axis.direction };
    const Vec3 columns[3] = { rotateAbout(Vec3(1, 0, 0), through, angle),
                              rotateAbout(Vec3(0, 1, 0), through, angle),
                              rotateAbout(Vec3(0, 0, 1), through, angle) };
    for (int row = 0; row < 3; ++row)
        motion.r[row] = Vec3(columns[0][row], columns[1][row], columns[2][row]);
    motion.t = rotateAbout(Vec3(), Axis{ axis.origin, axis.direction }, angle);
    return motion;
}

AxleSample sampleAxleAt(const AxleSolver& axle, SweepKind kind, double input,
                        double rackTravel)
{
    AxleSample sample;
    sample.input = input;
    if (axle.left()) sample.left = poseFor(*axle.left(), kind, input, rackTravel, nullptr);
    if (axle.right()) sample.right = poseFor(*axle.right(), kind, input, rackTravel, nullptr);
    computeRollCentre(&sample);
    computeAckermann(&sample, axle, kind);

    // Half a millimetre, or a hundredth of a degree of roll: small enough to be
    // a derivative, large enough not to be noise off the root finder.
    const double probe = kind == SweepKind::Roll ? 0.01 : 0.5;
    for (int side = 0; side < 2; ++side) {
        const std::optional<CornerSolver>& solver = side == 0 ? axle.left() : axle.right();
        if (!solver) continue;
        const CornerPose before = poseFor(*solver, kind, input - probe, rackTravel, nullptr);
        const CornerPose after = poseFor(*solver, kind, input + probe, rackTravel, nullptr);
        (side == 0 ? sample.leftInstallationRatio : sample.rightInstallationRatio) =
            installationRatio(before, after);
    }

    if (sample.left.valid && sample.right.valid && sample.left.hasAntiRoll
        && sample.right.hasAntiRoll) {
        sample.hasAntiRoll = true;
        sample.antiRollTwist =
            (sample.left.antiRollArmAngle - sample.right.antiRollArmAngle) * kRadToDeg;
    }
    return sample;
}

SweepResult runSweep(const AxleSolver& axle, const SweepSpec& spec)
{
    SweepResult result;
    result.kind = spec.kind;
    result.axleToken = axle.cornerToken();
    result.axleLabel = axle.label();
    result.warnings = axle.warnings();
    if (axle.isEmpty()) return result;

    // A steer sweep of an axle with no rack is not a flat curve to plot, it is a
    // question that cannot be asked. Coming back empty and saying why is the
    // honest answer; a line of zeroes would read as "this suspension has no bump
    // steer", which is a claim about the car rather than about the model.
    if (spec.kind == SweepKind::Steer && !axle.isSteered()) {
        result.warnings << tr("%1 has no steering: the linkage template names no hardpoint for "
                              "its rack to drive. Linkage > Steering Rack names one.")
                               .arg(result.axleLabel);
        return result;
    }

    const int steps = std::max(2, spec.steps);
    SweepSpec bounded = spec;
    bounded.steps = steps;

    result.samples.resize(static_cast<std::size_t>(steps));
    for (int i = 0; i < steps; ++i)
        result.samples[static_cast<std::size_t>(i)].input = bounded.inputAt(i);

    // Start where the mechanism is known to assemble -- the design position --
    // and walk outward in both directions, each step continuing from its
    // neighbour. Marching end to end instead would ask the first solve to guess
    // a branch from a pose it has never been near.
    int middle = 0;
    for (int i = 1; i < steps; ++i)
        if (std::abs(result.samples[static_cast<std::size_t>(i)].input)
            < std::abs(result.samples[static_cast<std::size_t>(middle)].input))
            middle = i;

    const auto solveSide = [&](const std::optional<CornerSolver>& solver, bool leftSide) {
        if (!solver) return;
        const CornerPose* previous = nullptr;
        const auto walk = [&](int first, int last, int stride) {
            previous = nullptr;
            for (int i = first; stride > 0 ? i <= last : i >= last; i += stride) {
                AxleSample& sample = result.samples[static_cast<std::size_t>(i)];
                CornerPose pose =
                    poseFor(*solver, bounded.kind, sample.input, bounded.rackTravel, previous);
                (leftSide ? sample.left : sample.right) = std::move(pose);
                const CornerPose& stored = leftSide ? sample.left : sample.right;
                previous = stored.valid ? &stored : previous;
            }
        };
        walk(middle, steps - 1, 1);
        walk(middle, 0, -1);
    };
    solveSide(axle.left(), true);
    solveSide(axle.right(), false);

    for (int i = 0; i < steps; ++i) {
        AxleSample& sample = result.samples[static_cast<std::size_t>(i)];
        computeRollCentre(&sample);
        computeAckermann(&sample, axle, bounded.kind);

        const int before = std::max(0, i - 1);
        const int after = std::min(steps - 1, i + 1);
        sample.leftInstallationRatio =
            installationRatio(result.samples[static_cast<std::size_t>(before)].left,
                              result.samples[static_cast<std::size_t>(after)].left);
        sample.rightInstallationRatio =
            installationRatio(result.samples[static_cast<std::size_t>(before)].right,
                              result.samples[static_cast<std::size_t>(after)].right);

        if (sample.left.valid && sample.right.valid && sample.left.hasAntiRoll
            && sample.right.hasAntiRoll) {
            sample.hasAntiRoll = true;
            sample.antiRollTwist =
                (sample.left.antiRollArmAngle - sample.right.antiRollArmAngle) * kRadToDeg;
        }
    }

    // A step that would not assemble is worth one line, not one line each.
    int refused = 0;
    QString firstReason;
    for (const AxleSample& sample : result.samples) {
        for (const CornerPose* pose : { &sample.left, &sample.right }) {
            if (pose->valid || pose->error.isEmpty()) continue;
            ++refused;
            if (firstReason.isEmpty()) firstReason = pose->error;
        }
    }
    if (refused > 0)
        result.warnings << tr("%1 of the sweep's positions did not assemble. %2")
                               .arg(refused)
                               .arg(firstReason);

    return result;
}

const std::vector<SweepMeasure>& sweepMeasures()
{
    static const std::vector<SweepMeasure> all = {
        SweepMeasure::Camber,           SweepMeasure::CamberToGround,
        SweepMeasure::Toe,
        SweepMeasure::WheelTravel,      SweepMeasure::Caster,
        SweepMeasure::KingpinInclination, SweepMeasure::ScrubRadius,
        SweepMeasure::MechanicalTrail,  SweepMeasure::HalfTrackChange,
        SweepMeasure::WheelbaseChange,  SweepMeasure::DamperTravel,
        SweepMeasure::DamperLength,     SweepMeasure::InstallationRatio,
        SweepMeasure::RollCentreHeight, SweepMeasure::RollCentreLateral,
        SweepMeasure::AntiRollTwist,    SweepMeasure::Ackermann,
    };
    return all;
}

QString sweepMeasureLabel(SweepMeasure measure)
{
    switch (measure) {
    case SweepMeasure::WheelTravel: return tr("Wheel travel");
    case SweepMeasure::Camber: return tr("Camber");
    case SweepMeasure::CamberToGround: return tr("Camber to ground");
    case SweepMeasure::Toe: return tr("Toe");
    case SweepMeasure::Caster: return tr("Caster");
    case SweepMeasure::KingpinInclination: return tr("Kingpin inclination");
    case SweepMeasure::ScrubRadius: return tr("Scrub radius");
    case SweepMeasure::MechanicalTrail: return tr("Mechanical trail");
    case SweepMeasure::HalfTrackChange: return tr("Half-track change");
    case SweepMeasure::WheelbaseChange: return tr("Wheelbase change");
    case SweepMeasure::DamperLength: return tr("Damper length");
    case SweepMeasure::DamperTravel: return tr("Damper travel");
    case SweepMeasure::InstallationRatio: return tr("Installation ratio");
    case SweepMeasure::RollCentreHeight: return tr("Roll centre height");
    case SweepMeasure::RollCentreLateral: return tr("Roll centre offset");
    case SweepMeasure::AntiRollTwist: return tr("Anti-roll bar twist");
    case SweepMeasure::Ackermann: return tr("Ackermann");
    }
    return QString();
}

QString sweepMeasureUnit(SweepMeasure measure)
{
    switch (measure) {
    case SweepMeasure::Camber:
    case SweepMeasure::CamberToGround:
    case SweepMeasure::Toe:
    case SweepMeasure::Caster:
    case SweepMeasure::KingpinInclination:
    case SweepMeasure::AntiRollTwist: return QStringLiteral("deg");
    case SweepMeasure::InstallationRatio: return QStringLiteral("mm/mm");
    case SweepMeasure::Ackermann: return QStringLiteral("%");
    default: break;
    }
    return QStringLiteral("mm");
}

QString sweepMeasureKey(SweepMeasure measure)
{
    switch (measure) {
    case SweepMeasure::WheelTravel: return QStringLiteral("wheelTravel");
    case SweepMeasure::Camber: return QStringLiteral("camber");
    case SweepMeasure::CamberToGround: return QStringLiteral("camberToGround");
    case SweepMeasure::Toe: return QStringLiteral("toe");
    case SweepMeasure::Caster: return QStringLiteral("caster");
    case SweepMeasure::KingpinInclination: return QStringLiteral("kingpinInclination");
    case SweepMeasure::ScrubRadius: return QStringLiteral("scrubRadius");
    case SweepMeasure::MechanicalTrail: return QStringLiteral("mechanicalTrail");
    case SweepMeasure::HalfTrackChange: return QStringLiteral("halfTrackChange");
    case SweepMeasure::WheelbaseChange: return QStringLiteral("wheelbaseChange");
    case SweepMeasure::DamperLength: return QStringLiteral("damperLength");
    case SweepMeasure::DamperTravel: return QStringLiteral("damperTravel");
    case SweepMeasure::InstallationRatio: return QStringLiteral("installationRatio");
    case SweepMeasure::RollCentreHeight: return QStringLiteral("rollCentreHeight");
    case SweepMeasure::RollCentreLateral: return QStringLiteral("rollCentreLateral");
    case SweepMeasure::AntiRollTwist: return QStringLiteral("antiRollTwist");
    case SweepMeasure::Ackermann: return QStringLiteral("ackermann");
    }
    return QString();
}

SweepMeasure sweepMeasureFromKey(const QString& key, SweepMeasure fallback)
{
    for (SweepMeasure measure : sweepMeasures())
        if (sweepMeasureKey(measure) == key) return measure;
    return fallback;
}

bool sweepMeasureIsPerSide(SweepMeasure measure)
{
    switch (measure) {
    case SweepMeasure::RollCentreHeight:
    case SweepMeasure::RollCentreLateral:
    case SweepMeasure::AntiRollTwist:
    case SweepMeasure::Ackermann: return false;
    default: break;
    }
    return true;
}

QString sweepSidesToString(SweepSides sides)
{
    switch (sides) {
    case SweepSides::Both: return QStringLiteral("both");
    case SweepSides::Left: return QStringLiteral("left");
    case SweepSides::Right: return QStringLiteral("right");
    }
    return QStringLiteral("both");
}

SweepSides sweepSidesFromString(const QString& text, SweepSides fallback)
{
    if (text == QLatin1String("both")) return SweepSides::Both;
    if (text == QLatin1String("left")) return SweepSides::Left;
    if (text == QLatin1String("right")) return SweepSides::Right;
    return fallback;
}

double sweepMeasureResolution(SweepMeasure measure)
{
    // By unit, because what is too small to matter is a matter of the unit: a
    // tenth of a millimetre is below anything a suspension is built to, while
    // a hundredth of a degree of toe is still bump steer somebody designs out.
    const QString unit = sweepMeasureUnit(measure);
    if (unit == QLatin1String("deg")) return 0.01;
    if (unit == QLatin1String("mm/mm")) return 0.001;
    return 0.1; // millimetres, and percent Ackermann
}

QString sweepValueText(double value)
{
    if (std::abs(value) < 0.0005) value = 0.0;
    return QString::number(value, 'f', 3);
}

bool sweepMeasureValue(const AxleSample& sample, SweepMeasure measure, bool leftSide,
                       double* value)
{
    switch (measure) {
    case SweepMeasure::RollCentreHeight:
        if (!sample.rollCenterValid) return false;
        *value = sample.rollCenterHeight;
        return true;
    case SweepMeasure::RollCentreLateral:
        if (!sample.rollCenterValid) return false;
        *value = sample.rollCenterLateral;
        return true;
    case SweepMeasure::AntiRollTwist:
        if (!sample.hasAntiRoll) return false;
        *value = sample.antiRollTwist;
        return true;
    case SweepMeasure::Ackermann:
        if (!sample.ackermannValid) return false;
        *value = sample.ackermann;
        return true;
    default: break;
    }

    const CornerPose& pose = leftSide ? sample.left : sample.right;
    if (!pose.valid) return false;

    switch (measure) {
    case SweepMeasure::WheelTravel: *value = pose.wheelTravel; return true;
    case SweepMeasure::Camber: *value = pose.camber; return true;
    case SweepMeasure::CamberToGround: *value = pose.camberToGround; return true;
    case SweepMeasure::Toe: *value = pose.toe; return true;
    case SweepMeasure::Caster: *value = pose.caster; return true;
    case SweepMeasure::KingpinInclination: *value = pose.kingpinInclination; return true;
    case SweepMeasure::ScrubRadius: *value = pose.scrubRadius; return true;
    case SweepMeasure::MechanicalTrail: *value = pose.mechanicalTrail; return true;
    case SweepMeasure::HalfTrackChange: *value = pose.halfTrackChange; return true;
    case SweepMeasure::WheelbaseChange: *value = pose.wheelbaseChange; return true;
    case SweepMeasure::DamperLength:
        if (!pose.hasDamper) return false;
        *value = pose.damperLength;
        return true;
    case SweepMeasure::DamperTravel:
        if (!pose.hasDamper) return false;
        *value = pose.damperTravel;
        return true;
    case SweepMeasure::InstallationRatio:
        if (!pose.hasDamper) return false;
        *value = leftSide ? sample.leftInstallationRatio : sample.rightInstallationRatio;
        return true;
    default: break;
    }
    return false;
}

bool sweepMeasureAvailable(const SweepResult& result, SweepMeasure measure)
{
    double value = 0.0;
    return std::any_of(result.samples.begin(), result.samples.end(), [&](const AxleSample& sample) {
        return sweepMeasureValue(sample, measure, true, &value)
               || sweepMeasureValue(sample, measure, false, &value);
    });
}

QString sweepMeasureRequirement(SweepMeasure measure)
{
    switch (measure) {
    case SweepMeasure::DamperLength:
    case SweepMeasure::DamperTravel:
    case SweepMeasure::InstallationRatio: return tr("a pushrod, a rocker and a damper");
    case SweepMeasure::AntiRollTwist: return tr("an anti-roll bar");
    default: break;
    }
    return QString();
}

namespace {

/// One per-wheel column of the CSV: its name, and the unit in its header.
struct CsvColumn {
    const char* name;
    const char* unit;
};

/// The columns each side of the axle takes, in the order they are written.
constexpr CsvColumn kCsvSideColumns[] = {
    { "travel", "mm" },           { "camber", "deg" },
    { "camber_to_ground", "deg" }, { "toe", "deg" },
    { "caster", "deg" },          { "kpi", "deg" },
    { "scrub_radius", "mm" },     { "trail", "mm" },
    { "half_track_change", "mm" }, { "wheelbase_change", "mm" },
    { "damper_length", "mm" },    { "damper_travel", "mm" },
    { "installation_ratio", "mm/mm" },
};

/// The header of one side's columns, each name prefixed with @p prefix.
QByteArray csvSideHeader(const QByteArray& prefix, const QByteArray& side)
{
    QByteArray header;
    for (const CsvColumn& column : kCsvSideColumns)
        header += "," + prefix + column.name + "_" + side + " [" + column.unit + "]";
    return header;
}

/// How many columns belong to the axle rather than to a wheel.
constexpr int kCsvAxleColumns = 4;

/// The header of the columns that belong to the axle rather than to a wheel.
QByteArray csvAxleHeader(const QByteArray& prefix)
{
    return "," + prefix + "roll_centre_height [mm]," + prefix + "roll_centre_lateral [mm],"
           + prefix + "anti_roll_twist [deg]," + prefix + "ackermann [%]";
}

/// One side's fields of one row.
QByteArray csvSideFields(const CornerPose& pose, double installationRatio)
{
    // Empty fields rather than zeros: a position that did not assemble has no
    // camber, and plotting one as zero would put a spike in the middle of an
    // otherwise honest curve.
    if (!pose.valid) return QByteArray(int(std::size(kCsvSideColumns)), ',');

    QByteArray fields;
    for (const double value : { pose.wheelTravel, pose.camber, pose.camberToGround, pose.toe,
                                pose.caster, pose.kingpinInclination, pose.scrubRadius,
                                pose.mechanicalTrail, pose.halfTrackChange, pose.wheelbaseChange,
                                pose.damperLength, pose.damperTravel, installationRatio })
        fields += "," + field(value);
    return fields;
}

/// The axle-wide fields of one row.
QByteArray csvAxleFields(const AxleSample& sample)
{
    QByteArray fields = sample.rollCenterValid ? "," + field(sample.rollCenterHeight) + ","
                                                     + field(sample.rollCenterLateral)
                                               : QByteArray(",,");
    fields += sample.hasAntiRoll ? "," + field(sample.antiRollTwist) : QByteArray(",");
    fields += sample.ackermannValid ? "," + field(sample.ackermann) : QByteArray(",");
    return fields;
}

/// Which of an axle's two wheels a CSV carries columns for.
struct CsvSides {
    bool left = true;
    bool right = true;
};

/// What an axle's columns are prefixed with when several axles share a sheet:
/// its label, or its corner token when the template gave it none.
QByteArray csvAxlePrefix(const SweepResult& result)
{
    const QString name = result.axleLabel.isEmpty() ? result.axleToken : result.axleLabel;
    return name.toUtf8() + ' ';
}

/// The header of one axle's columns: the wheels asked for, then the axle-wide ones.
QByteArray csvSweepHeader(const QByteArray& prefix, CsvSides sides)
{
    QByteArray header;
    if (sides.left) header += csvSideHeader(prefix, "left");
    if (sides.right) header += csvSideHeader(prefix, "right");
    return header + csvAxleHeader(prefix);
}

/// One axle's fields of row @p row, all of them empty past the end of its sweep.
QByteArray csvSweepFields(const SweepResult& result, std::size_t row, CsvSides sides)
{
    if (row >= result.samples.size()) {
        const int sideColumns = int(std::size(kCsvSideColumns));
        return QByteArray((sides.left ? sideColumns : 0) + (sides.right ? sideColumns : 0)
                              + kCsvAxleColumns,
                          ',');
    }
    const AxleSample& sample = result.samples[row];
    QByteArray fields;
    if (sides.left) fields += csvSideFields(sample.left, sample.leftInstallationRatio);
    if (sides.right) fields += csvSideFields(sample.right, sample.rightInstallationRatio);
    return fields + csvAxleFields(sample);
}

} // namespace

QByteArray sweepsToCsv(const std::vector<SweepResult>& results, SweepSides sides)
{
    std::vector<const SweepResult*> swept;
    for (const SweepResult& result : results)
        if (!result.isEmpty()) swept.push_back(&result);
    if (swept.empty()) return QByteArray();

    const CsvSides wanted{ sides != SweepSides::Right, sides != SweepSides::Left };
    const bool several = swept.size() > 1;
    // Every sweep of one spec has the same inputs; the longest supplies them.
    const SweepResult* longest = *std::max_element(
        swept.begin(), swept.end(), [](const SweepResult* a, const SweepResult* b) {
            return a->samples.size() < b->samples.size();
        });

    QByteArray csv = sweepInputLabel(longest->kind).toUtf8() + " ["
                     + sweepInputUnit(longest->kind).toUtf8() + "]";
    for (const SweepResult* result : swept)
        csv += csvSweepHeader(several ? csvAxlePrefix(*result) : QByteArray(), wanted);
    csv += "\n";

    for (std::size_t row = 0; row < longest->samples.size(); ++row) {
        csv += field(longest->samples[row].input);
        for (const SweepResult* result : swept) csv += csvSweepFields(*result, row, wanted);
        csv += "\n";
    }
    return csv;
}

QByteArray sweepToCsv(const SweepResult& result)
{
    QByteArray csv;
    const QByteArray unit = sweepInputUnit(result.kind).toUtf8();

    csv += sweepInputLabel(result.kind).toUtf8() + " [" + unit + "]";
    csv += csvSideHeader(QByteArray(), "left");
    csv += csvSideHeader(QByteArray(), "right");
    csv += csvAxleHeader(QByteArray()) + "\n";

    for (const AxleSample& sample : result.samples) {
        csv += field(sample.input);
        csv += csvSideFields(sample.left, sample.leftInstallationRatio);
        csv += csvSideFields(sample.right, sample.rightInstallationRatio);
        csv += csvAxleFields(sample);
        csv += "\n";
    }
    return csv;
}

} // namespace suspkin
