#pragma once

#include "model/Linkage.h"
#include "model/SuspensionSolver.h"

#include <QByteArray>
#include <QString>
#include <QStringList>

#include <optional>
#include <vector>

namespace suspkin {

/// What is being varied.
///
/// The three between them are what a suspension is signed off on: how it behaves
/// over a bump, how it behaves in a corner, and what the steering does to it.
enum class SweepKind {
    Bump,  ///< both wheels of the axle move together, in millimetres
    Roll,  ///< the body rolls, in degrees, and the two wheels move opposite ways
    Steer, ///< the rack moves, in millimetres, at a fixed ride height
};

QString sweepKindToString(SweepKind kind);
SweepKind sweepKindFromString(const QString& text, SweepKind fallback = SweepKind::Bump);
/// The unit the input is in, for an axis label.
QString sweepInputUnit(SweepKind kind);
QString sweepInputLabel(SweepKind kind);

/// One axle: the corner as the template names it, and its mirror.
///
/// Roll centre is the reason this exists rather than a bare pair of corners --
/// it is a property of an axle, not of a wheel, and it needs both sides at the
/// same instant.
class AxleSolver {
public:
    /// Bind @p mechanism for @p corner against @p table, both sides, taking the
    /// far side from @p mirror the same way the parts do. A side the table does
    /// not hold is simply absent; that is a workbook with one axle done, not an
    /// error.
    ///
    /// @p steeringDeclared is whether the template says anything anywhere about
    /// which axles are steered (@ref LinkageTemplate::steeringDeclared). When it
    /// does not -- every template written before the role existed -- every axle
    /// is steered, which is the behaviour those projects have always had. When
    /// it does, this corner is steered only if it names a rack point itself.
    static AxleSolver build(const MechanismTemplate& mechanism, const CornerSpec& corner,
                            const HardpointTable& table, const MirrorSpec& mirror,
                            bool steeringDeclared = false);

    bool isEmpty() const { return !m_left && !m_right; }
    bool hasBothSides() const { return m_left.has_value() && m_right.has_value(); }
    /// Whether a rack drives this axle. A steer sweep on an axle that has none
    /// is not a flat curve, it is a question that cannot be asked.
    bool isSteered() const;
    const QString& cornerToken() const { return m_token; }
    const QString& label() const { return m_label; }
    const QStringList& warnings() const { return m_warnings; }

    /// The two corners, by which side of the car they turned out to be on --
    /// which is decided by the coordinates, not by which one the template wrote
    /// out first.
    const std::optional<CornerSolver>& left() const { return m_left; }
    const std::optional<CornerSolver>& right() const { return m_right; }

    /// Where this axle's roll centre is at the design position, as a point in
    /// the car's own coordinates: across the car where the two construction
    /// lines cross, along it where the contact patches are. Nothing when there
    /// is no instant centre to construct one from -- two wishbones parallel in
    /// the front view.
    std::optional<Vec3> designRollCentre() const;

private:
    QString m_token;
    QString m_label;
    std::optional<CornerSolver> m_left;
    std::optional<CornerSolver> m_right;
    QStringList m_warnings;
};

/// The line the body rolls about: through the roll centre of each axle.
///
/// A roll sweep is solved in the car's own coordinates, with the ground tilted
/// underneath it, because that is where the suspension's own numbers live. Seen
/// from the road it is the other way round -- the road stays level and the
/// monocoque turns about this line, taking every wheel with it -- and the roll
/// centres are what make the two pictures agree: turning the body about them
/// is the one motion that leaves the contact patches where they are on the
/// road, to first order, rather than dragging them sideways.
struct RollAxis {
    Vec3 origin;    ///< a point on it: a roll centre, or the ground under the centreline
    Vec3 direction; ///< unit, pointing forward, so positive roll is right-handed about it
    bool valid = false;
};

/// The roll axis of a car with these axles.
///
/// Through the front-most and the rear-most roll centres, and so inclined
/// whenever the two are at different heights, which on most cars they are.
/// With only one axle in the table it runs along x through that axle's roll
/// centre. With not one roll centre it runs along x through the ground under the
/// centreline -- which is the axis a roll sweep tilts the ground about -- and
/// with no axle at all it is invalid.
RollAxis rollAxisThrough(const std::vector<AxleSolver>& axles);

/// Where @p degrees of roll puts the body, as seen from the road: every point
/// of the car -- chassis, suspension and wheels, as a roll sweep solved them --
/// goes to @c map(p). Positive roll is the sweep's own sense, right-handed about
/// the forward-pointing axis, so the left of the car rises and its left wheel
/// is the one in droop. Identity for an invalid axis.
Rigid bodyRollMotion(const RollAxis& axis, double degrees);

/// What to sweep, and how finely.
///
/// One range, in one unit. It is what a sweep is actually run with, and it is
/// derived from @ref SweepSettings rather than edited directly -- see there for
/// why the three kinds cannot share a range.
struct SweepSpec {
    SweepKind kind = SweepKind::Bump;
    /// Millimetres of wheel travel, degrees of body roll, or millimetres of rack,
    /// depending on @ref kind.
    double from = -25.0;
    double to = 25.0;
    int steps = 41;
    /// Held constant through a bump or roll sweep, so bump steer can be looked
    /// at on a wheel that is already turned.
    double rackTravel = 0.0;

    bool operator==(const SweepSpec& other) const;
    bool operator!=(const SweepSpec& other) const { return !(*this == other); }
    /// The input value at step @p index, clamped into range.
    double inputAt(int index) const;
};

/// The most positions a sweep is allowed to solve. Past this a plot is drawing
/// more points than it has pixels, and an increment of nearly zero stops being
/// a finer sweep and starts being a hung window.
constexpr int kMaxSweepSteps = 2001;

/// How far each kind of sweep travels, and how finely -- stated the way a
/// suspension is signed off on rather than as a bare range.
///
/// All three kinds are held at once, and that is the whole point. A single
/// range cannot be shared between them: it is in millimetres of wheel travel
/// for one, degrees of body roll for the next and millimetres of rack for the
/// third, so switching from bump to roll on a shared range asks for twenty-five
/// degrees of roll because the wheel travel happened to be twenty-five
/// millimetres. Keeping each kind's own numbers means every sweep comes back to
/// what it was last given.
///
/// Travel is stated as a distance and a step, not as a start and an end,
/// because that is what is written on a damper and on a test sheet. Bump and
/// rebound are separate because they are not symmetrical on a real car; roll
/// and steer are, so they get one number each.
struct SweepSettings {
    /// Millimetres of wheel travel each way, both written positive: the sweep
    /// runs from -@ref reboundTravel to +@ref bumpTravel.
    double bumpTravel = 25.0;
    double reboundTravel = 25.0;
    double bumpIncrement = 1.0;

    /// Degrees of body roll either side of level, and the step between them.
    double rollAngle = 3.0;
    double rollIncrement = 0.25;

    /// Millimetres of rack either side of centre, and the step between them.
    double steerTravel = 30.0;
    double steerIncrement = 2.0;

    /// Held constant through a bump or roll sweep, so bump steer can be looked
    /// at on a wheel that is already turned.
    double rackTravel = 0.0;

    bool operator==(const SweepSettings& other) const;
    bool operator!=(const SweepSettings& other) const { return !(*this == other); }

    /// The range and step count @p kind runs over. Travels are taken as
    /// magnitudes, so a rebound written as a negative number still means
    /// downward, and an increment that would produce more than
    /// @ref kMaxSweepSteps positions is honoured only as far as that.
    SweepSpec specFor(SweepKind kind) const;

    /// The travel and increment @p kind is stated with, for the readout that
    /// says what the numbers above come to.
    double incrementFor(SweepKind kind) const;
};

/// The axle at one point in the sweep.
struct AxleSample {
    double input = 0.0;
    CornerPose left;
    CornerPose right;

    /// Where the two contact-patch-to-instant-centre lines cross, in the front
    /// view. Height is above the ground plane the contact patches sit on, and
    /// lateral is off the car's centreline -- which is not zero once the axle is
    /// rolled, and is the number that gets forgotten.
    double rollCenterHeight = 0.0;
    double rollCenterLateral = 0.0;
    bool rollCenterValid = false;

    /// Millimetres of damper per millimetre of wheel. Called the installation
    /// ratio to keep it apart from its reciprocal, which is also called the
    /// motion ratio by about half the literature.
    double leftInstallationRatio = 0.0;
    double rightInstallationRatio = 0.0;

    /// Degrees of twist between the bar's two arms. Zero in pure bump -- both
    /// arms turn together and the bar goes along for the ride -- and largest in
    /// roll, which is the whole point of fitting one.
    double antiRollTwist = 0.0;
    bool hasAntiRoll = false;

    bool valid() const { return left.valid || right.valid; }
};

/// A whole sweep, ready to plot or to write out.
struct SweepResult {
    SweepKind kind = SweepKind::Bump;
    QString axleLabel;
    std::vector<AxleSample> samples;
    QStringList warnings;

    bool isEmpty() const { return samples.empty(); }
    /// The sample nearest @p input, or nothing when there are none.
    const AxleSample* nearest(double input) const;
};

/// One thing that can be plotted against the sweep's input.
///
/// Named here rather than in the widget that draws them so that what a curve
/// means, what it is called and what unit it is in all live with the numbers
/// themselves -- and so that the list can be tested.
enum class SweepMeasure {
    WheelTravel,
    Camber,
    Toe,
    Caster,
    KingpinInclination,
    ScrubRadius,
    MechanicalTrail,
    HalfTrackChange,
    WheelbaseChange,
    DamperLength,
    DamperTravel,
    InstallationRatio,
    RollCentreHeight,
    RollCentreLateral,
    AntiRollTwist,
};

/// Every measure, in the order they belong in a menu: the wheel first, then the
/// steering axis, then the ground, then the spring, then the axle.
const std::vector<SweepMeasure>& sweepMeasures();

QString sweepMeasureLabel(SweepMeasure measure);
QString sweepMeasureUnit(SweepMeasure measure);
/// A stable identifier for the project file, so a saved choice of curve reads
/// back the same in the next release.
QString sweepMeasureKey(SweepMeasure measure);
SweepMeasure sweepMeasureFromKey(const QString& key, SweepMeasure fallback = SweepMeasure::Camber);

/// False for the ones that belong to the axle rather than to a wheel -- the roll
/// centre and the bar's twist -- which are drawn as one curve, not two.
bool sweepMeasureIsPerSide(SweepMeasure measure);

/// @p measure read off @p sample, for one side when it has sides. False when
/// there is no number there: a position that did not assemble, an axle with no
/// bar, a roll centre that could not be constructed.
bool sweepMeasureValue(const AxleSample& sample, SweepMeasure measure, bool leftSide,
                       double* value);

/// The axle at one position, on its own.
///
/// What the travel slider needs: a whole sweep's worth of numbers for one place,
/// solved from the design position rather than continued from a neighbour. The
/// installation ratio still comes from a difference, taken across a small probe
/// either side.
AxleSample sampleAxleAt(const AxleSolver& axle, SweepKind kind, double input, double rackTravel);

/// Run @p spec over @p axle.
///
/// Solved outward from the design position rather than end to end, so every step
/// continues from a pose next door to it. That is what keeps the solve on the
/// same assembly branch all the way to the end of the travel.
SweepResult runSweep(const AxleSolver& axle, const SweepSpec& spec);

/// The sweep as a spreadsheet: one row per step, one column per measure, with a
/// header naming the units. Comma-separated and dot-decimal, because it is read
/// by other tools more often than by a person.
QByteArray sweepToCsv(const SweepResult& result);

} // namespace suspkin
