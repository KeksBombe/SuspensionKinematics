#include "model/Sweep.h"

#include "model/GeomSolve.h"

#include <QCoreApplication>

#include <algorithm>
#include <cmath>

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

AxleSolver AxleSolver::build(const MechanismTemplate& mechanism, const CornerSpec& corner,
                             const HardpointTable& table, const MirrorSpec& mirror,
                             bool steeringDeclared)
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
        const MechanismTemplate named = instantiateMechanism(roles, corner.token, mirrored, mirror);
        if (named.isEmpty()) continue;

        // A corner or a side with not one of its points in the table is not a
        // corner that is missing something -- it is a workbook holding one axle,
        // or one that has not been mirrored yet. Saying so would be noise, and
        // the noise would bury the corners that really are half there.
        const MechanismCoverage coverage = coverMechanism(named, table);
        if (coverage.absent) continue;

        QString error;
        std::optional<CornerSolver> solver = CornerSolver::bind(named, table, &error);
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

    // A U-bar's axis runs from one arm root to the other, and only an axle knows
    // both. Without the far side it stays the y direction, which is what a
    // transverse bar is anyway.
    if (axle.m_left && axle.m_right) {
        const MechanismTemplate& leftNames = axle.m_left->mechanism();
        const MechanismTemplate& rightNames = axle.m_right->mechanism();
        const Hardpoint* leftPivot = table.find(leftNames.antiRollArmPivot);
        const Hardpoint* rightPivot = table.find(rightNames.antiRollArmPivot);
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
        const double y = solver.designPose().contactPatch.y;
        return solver.poseAtContactPatchRise(-y * std::tan(input * kDegToRad), rackTravel,
                                             previous);
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

/// Damper millimetres per wheel millimetre, by central difference against the
/// neighbouring samples. Differentiating the curve rather than the mechanism
/// keeps it honest about what was actually solved.
double installationRatio(const CornerPose& before, const CornerPose& after)
{
    if (!before.valid || !after.valid || !before.hasDamper || !after.hasDamper) return 0.0;
    const double travel = after.wheelTravel - before.wheelTravel;
    if (std::abs(travel) < 1e-9) return 0.0;
    return (after.damperTravel - before.damperTravel) / travel;
}

} // namespace

AxleSample sampleAxleAt(const AxleSolver& axle, SweepKind kind, double input,
                        double rackTravel)
{
    AxleSample sample;
    sample.input = input;
    if (axle.left()) sample.left = poseFor(*axle.left(), kind, input, rackTravel, nullptr);
    if (axle.right()) sample.right = poseFor(*axle.right(), kind, input, rackTravel, nullptr);
    computeRollCentre(&sample);

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
    result.axleLabel = axle.label();
    result.warnings = axle.warnings();
    if (axle.isEmpty()) return result;

    // A steer sweep of an axle with no rack is not a flat curve to plot, it is a
    // question that cannot be asked. Coming back empty and saying why is the
    // honest answer; a line of zeroes would read as "this suspension has no bump
    // steer", which is a claim about the car rather than about the model.
    if (spec.kind == SweepKind::Steer && !axle.isSteered()) {
        result.warnings << tr("%1 has no steering: the linkage template names no hardpoint for "
                              "its rack to drive. Parts > Steering names one.")
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
        SweepMeasure::Camber,           SweepMeasure::Toe,
        SweepMeasure::WheelTravel,      SweepMeasure::Caster,
        SweepMeasure::KingpinInclination, SweepMeasure::ScrubRadius,
        SweepMeasure::MechanicalTrail,  SweepMeasure::HalfTrackChange,
        SweepMeasure::WheelbaseChange,  SweepMeasure::DamperTravel,
        SweepMeasure::DamperLength,     SweepMeasure::InstallationRatio,
        SweepMeasure::RollCentreHeight, SweepMeasure::RollCentreLateral,
        SweepMeasure::AntiRollTwist,
    };
    return all;
}

QString sweepMeasureLabel(SweepMeasure measure)
{
    switch (measure) {
    case SweepMeasure::WheelTravel: return tr("Wheel travel");
    case SweepMeasure::Camber: return tr("Camber");
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
    }
    return QString();
}

QString sweepMeasureUnit(SweepMeasure measure)
{
    switch (measure) {
    case SweepMeasure::Camber:
    case SweepMeasure::Toe:
    case SweepMeasure::Caster:
    case SweepMeasure::KingpinInclination:
    case SweepMeasure::AntiRollTwist: return QStringLiteral("deg");
    case SweepMeasure::InstallationRatio: return QStringLiteral("mm/mm");
    default: break;
    }
    return QStringLiteral("mm");
}

QString sweepMeasureKey(SweepMeasure measure)
{
    switch (measure) {
    case SweepMeasure::WheelTravel: return QStringLiteral("wheelTravel");
    case SweepMeasure::Camber: return QStringLiteral("camber");
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
    case SweepMeasure::AntiRollTwist: return false;
    default: break;
    }
    return true;
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
    default: break;
    }

    const CornerPose& pose = leftSide ? sample.left : sample.right;
    if (!pose.valid) return false;

    switch (measure) {
    case SweepMeasure::WheelTravel: *value = pose.wheelTravel; return true;
    case SweepMeasure::Camber: *value = pose.camber; return true;
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

QByteArray sweepToCsv(const SweepResult& result)
{
    QByteArray csv;
    const QByteArray unit = sweepInputUnit(result.kind).toUtf8();

    csv += sweepInputLabel(result.kind).toUtf8() + " [" + unit + "]";
    for (const char* side : { "left", "right" }) {
        const QByteArray s = QByteArray(side);
        csv += ",travel_" + s + " [mm]";
        csv += ",camber_" + s + " [deg]";
        csv += ",toe_" + s + " [deg]";
        csv += ",caster_" + s + " [deg]";
        csv += ",kpi_" + s + " [deg]";
        csv += ",scrub_radius_" + s + " [mm]";
        csv += ",trail_" + s + " [mm]";
        csv += ",half_track_change_" + s + " [mm]";
        csv += ",wheelbase_change_" + s + " [mm]";
        csv += ",damper_length_" + s + " [mm]";
        csv += ",damper_travel_" + s + " [mm]";
        csv += ",installation_ratio_" + s + " [mm/mm]";
    }
    csv += ",roll_centre_height [mm],roll_centre_lateral [mm],anti_roll_twist [deg]\n";

    for (const AxleSample& sample : result.samples) {
        csv += field(sample.input);
        for (int side = 0; side < 2; ++side) {
            const CornerPose& pose = side == 0 ? sample.left : sample.right;
            const double ratio = side == 0 ? sample.leftInstallationRatio
                                           : sample.rightInstallationRatio;
            if (!pose.valid) {
                // Twelve empty fields rather than twelve zeros: a position that
                // did not assemble has no camber, and plotting one as zero would
                // put a spike in the middle of an otherwise honest curve.
                csv += QByteArray(",,,,,,,,,,,,");
                continue;
            }
            csv += "," + field(pose.wheelTravel);
            csv += "," + field(pose.camber);
            csv += "," + field(pose.toe);
            csv += "," + field(pose.caster);
            csv += "," + field(pose.kingpinInclination);
            csv += "," + field(pose.scrubRadius);
            csv += "," + field(pose.mechanicalTrail);
            csv += "," + field(pose.halfTrackChange);
            csv += "," + field(pose.wheelbaseChange);
            csv += "," + field(pose.damperLength);
            csv += "," + field(pose.damperTravel);
            csv += "," + field(ratio);
        }
        if (sample.rollCenterValid)
            csv += "," + field(sample.rollCenterHeight) + "," + field(sample.rollCenterLateral);
        else
            csv += ",,";
        csv += sample.hasAntiRoll ? "," + field(sample.antiRollTwist) : QByteArray(",");
        csv += "\n";
    }
    return csv;
}

} // namespace suspkin
