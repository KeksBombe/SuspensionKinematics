#include "app/GenerateDialog.h"

#include "app/HardpointDelegates.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QSplitter>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace suspkin {
namespace {

/// Long enough that typing a number is one plan rather than one per digit,
/// short enough that the preview keeps up with a spin box held down.
constexpr int kPlanDelayMs = 120;

constexpr int kFront = 0;
constexpr int kRear = 1;

/// What a number in the axle columns is measured in. Short, because it is
/// written once per row of the table below.
enum Unit { Mm, Deg, Pct, Ratio };

AxlePosition positionOf(int column) { return column == kFront ? AxlePosition::Front : AxlePosition::Rear; }

QString bulleted(const QStringList& lines)
{
    QString html;
    for (const QString& line : lines) html += QStringLiteral("<li>%1</li>").arg(line.toHtmlEscaped());
    return QStringLiteral("<ul style=\"margin-left: -20px\">%1</ul>").arg(html);
}

} // namespace

GenerateDialog::GenerateDialog(const DesignParameters& seed, const LinkageTemplate& templ,
                               const MirrorSpec& mirror, const HardpointTable& current,
                               const HardpointTable& baseline, bool chassisAvailable,
                               ChassisSource chassis, QWidget* parent)
    : QDialog(parent),
      m_templ(templ),
      m_mirror(mirror),
      m_current(current),
      m_baseline(baseline),
      m_chassisAvailable(chassisAvailable),
      m_chassis(std::move(chassis)),
      m_seed(seed)
{
    setWindowTitle(tr("Generate from Design"));

    m_timer = new QTimer(this);
    m_timer->setSingleShot(true);
    m_timer->setInterval(kPlanDelayMs);
    connect(m_timer, &QTimer::timeout, this, &GenerateDialog::rebuildPlan);

    auto* splitter = new QSplitter(Qt::Horizontal, this);
    splitter->addWidget(buildTargets());
    splitter->addWidget(buildPreview());
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 2);

    m_buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    m_buttons->button(QDialogButtonBox::Ok)->setText(tr("Generate"));
    QPushButton* defaults = m_buttons->addButton(tr("Restore Defaults"), QDialogButtonBox::ResetRole);
    defaults->setToolTip(tr("Reset every number to the 2025 car's targets, which the generator "
                            "ships with. Which axles are generated, into which corners and on "
                            "which side, is kept."));
    connect(m_buttons, &QDialogButtonBox::accepted, this, &GenerateDialog::accept);
    connect(m_buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(defaults, &QPushButton::clicked, this, [this] {
        const DesignParameters now = parameters();
        DesignParameters fresh;
        fresh.side = now.side;
        fresh.front.generate = now.front.generate;
        fresh.front.corner = now.front.corner;
        fresh.rear.generate = now.rear.generate;
        fresh.rear.corner = now.rear.corner;
        load(fresh);
        rebuildPlan();
    });

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(splitter, 1);
    layout->addWidget(m_buttons);

    load(seed);
    rebuildPlan();
    resize(1180, 760);
}

QDoubleSpinBox* GenerateDialog::number(double minimum, double maximum, int decimals,
                                       const QString& suffix, const QString& tip)
{
    auto* spin = new QDoubleSpinBox(this);
    spin->setRange(minimum, maximum);
    spin->setDecimals(decimals);
    spin->setSuffix(suffix);
    spin->setToolTip(tip);
    spin->setAccelerated(true);
    spin->setKeyboardTracking(false);
    spin->setMinimumWidth(96);
    connect(spin, &QDoubleSpinBox::valueChanged, this, &GenerateDialog::schedulePlan);
    return spin;
}

void GenerateDialog::addHeading(QGridLayout* grid, int* row, const QString& text)
{
    auto* heading = new QLabel(QStringLiteral("<b>%1</b>").arg(text.toHtmlEscaped()), this);
    heading->setContentsMargins(0, *row == 0 ? 0 : 10, 0, 2);
    grid->addWidget(heading, (*row)++, 0, 1, 3);
}

QWidget* GenerateDialog::buildTargets()
{
    auto* host = new QWidget(this);
    auto* grid = new QGridLayout(host);
    grid->setColumnStretch(0, 1);
    int row = 0;

    const QString mm = tr(" mm");
    const QString deg = tr(" °");
    const QString pct = tr(" %");

    // --- which corners, and which side ------------------------------------
    addHeading(grid, &row, tr("What to generate"));
    grid->addWidget(new QLabel(tr("Front"), host), row, 1, Qt::AlignCenter);
    grid->addWidget(new QLabel(tr("Rear"), host), row, 2, Qt::AlignCenter);
    ++row;
    const QString generateTip = tr("Tick to generate this axle, and pick which corner of the "
                                   "linkage template its points are named for.");
    auto* generateLabel = new QLabel(tr("Generate this axle into corner"), host);
    generateLabel->setToolTip(generateTip);
    grid->addWidget(generateLabel, row, 0);
    for (int column = 0; column < 2; ++column) {
        auto* cell = new QWidget(host);
        auto* cellLayout = new QHBoxLayout(cell);
        cellLayout->setContentsMargins(0, 0, 0, 0);
        m_generate[column] = new QCheckBox(cell);
        m_corner[column] = new QComboBox(cell);
        // The corners the project's own template has; the generator names
        // the points through that corner's roles.
        for (const CornerSpec& corner : m_templ.corners) {
            m_corner[column]->addItem(corner.label.isEmpty() || corner.label == corner.token
                                          ? corner.token
                                          : QStringLiteral("%1 (%2)").arg(corner.token, corner.label),
                                      corner.token);
        }
        m_generate[column]->setToolTip(generateTip);
        m_corner[column]->setToolTip(generateTip);
        cellLayout->addWidget(m_generate[column]);
        cellLayout->addWidget(m_corner[column], 1);
        connect(m_generate[column], &QCheckBox::toggled, this, &GenerateDialog::schedulePlan);
        connect(m_corner[column], &QComboBox::currentIndexChanged, this, &GenerateDialog::schedulePlan);
        grid->addWidget(cell, row, 1 + column);
    }
    ++row;

    grid->addWidget(new QLabel(tr("Side the template's names are on"), host), row, 0);
    m_side = new QComboBox(host);
    m_side->addItem(tr("Left (+y)"), int(DesignSide::Left));
    m_side->addItem(tr("Right (−y)"), int(DesignSide::Right));
    m_side->setToolTip(tr("Which side of the car the linkage template's point names are on. "
                          "The points are generated on this side under those names; the other "
                          "side is made from them by the project's mirror rule (Y negated)."));
    connect(m_side, &QComboBox::currentIndexChanged, this, &GenerateDialog::schedulePlan);
    grid->addWidget(m_side, row++, 1, 1, 2);

    // --- the car ------------------------------------------------------------
    addHeading(grid, &row, tr("The car"));
    struct CarSpec {
        const char* label;
        double DesignParameters::*member;
        double minimum, maximum;
        int decimals;
        const char* unit;
        const char* tip;
    };
    const CarSpec car[] = {
        { "Wheelbase", &DesignParameters::wheelbase, 300, 6000, 1, "mm",
          "Distance along X between the front and the rear wheel centres." },
        { "Centre of gravity, x", &DesignParameters::cogX, -20000, 20000, 1, "mm",
          "Position of the centre of gravity along the car (X), in the same frame as the "
          "hardpoints (ISO 8855: X forward, Y left, Z up). The two axles are placed either side "
          "of it by the wheelbase and the front weight share." },
        { "Centre of gravity, height", &DesignParameters::cogHeight, 0, 3000, 1, "mm",
          "Height of the centre of gravity above the ground (Z). Used for anti-dive and "
          "anti-lift." },
        { "Weight on the front axle", &DesignParameters::frontWeight, 1, 99, 1, "%",
          "Share of the car's static weight carried by the front axle. Together with the "
          "wheelbase it places the axles relative to the centre of gravity." },
        { "Front brake bias", &DesignParameters::frontBrakeBias, 0, 100, 1, "%",
          "Share of the braking force produced by the front axle; the rear does the rest. "
          "Anti-dive and anti-lift are worked out from each axle's own share." },
        { "Loaded radius", &DesignParameters::loadedRadius, 10, 2000, 1, "mm",
          "Height of the wheel centre above the ground (Z) with the car at ride height." },
        { "Rim radius", &DesignParameters::rimRadius, 0, 2000, 1, "mm",
          "Inner radius of the rim, measured from the wheel's axis. A ball joint further from "
          "the axis than this would sit outside the rim, and is warned about. 0 turns the "
          "check off." },
    };
    for (const CarSpec& spec : car) {
        const QString unit = QLatin1String(spec.unit) == QLatin1String("%") ? pct : mm;
        QDoubleSpinBox* spin = number(spec.minimum, spec.maximum, spec.decimals, unit, tr(spec.tip));
        auto* label = new QLabel(tr(spec.label), host);
        label->setToolTip(spin->toolTip());
        grid->addWidget(label, row, 0);
        grid->addWidget(spin, row++, 1);
        m_carNumbers.push_back(CarNumber{ spec.member, spin });
    }

    // --- where the chassis pivots go -----------------------------------------
    addHeading(grid, &row, tr("Chassis pivots"));
    m_useChassis = new QCheckBox(tr("Place chassis pivots on the imported chassis surface"), host);
    m_useChassis->setEnabled(m_chassisAvailable);
    m_useChassis->setToolTip(
        m_chassisAvailable
            ? tr("Instead of using the pivot distances from the centreline, each wishbone leg is "
                 "extended inboard from its ball joint until it hits the imported chassis; the "
                 "pivot is placed there, Chassis clearance away from the surface. A leg that "
                 "misses the chassis falls back to its pivot distance. Only use this when the "
                 "imported geometry is the bare chassis: an upright or other parts in the model "
                 "would be hit first.")
            : tr("Needs chassis geometry: import it with Geometry > Import first."));
    connect(m_useChassis, &QCheckBox::toggled, this, &GenerateDialog::schedulePlan);
    grid->addWidget(m_useChassis, row++, 0, 1, 3);
    QDoubleSpinBox* clearance = number(0, 500, 1, mm,
                                       tr("Distance between each chassis pivot and the chassis "
                                          "surface, along the wishbone leg. Only used when the "
                                          "pivots are placed on the imported chassis surface."));
    auto* clearanceLabel = new QLabel(tr("Chassis clearance"), host);
    clearanceLabel->setToolTip(clearance->toolTip());
    grid->addWidget(clearanceLabel, row, 0);
    grid->addWidget(clearance, row++, 1);
    // Only means something while the box is ticked.
    const auto syncClearance = [this, clearance, clearanceLabel] {
        const bool used = m_useChassis->isEnabled() && m_useChassis->isChecked();
        clearance->setEnabled(used);
        clearanceLabel->setEnabled(used);
    };
    connect(m_useChassis, &QCheckBox::toggled, this, syncClearance);
    syncClearance();
    m_carNumbers.push_back(CarNumber{ &DesignParameters::chassisClearance, clearance });

    // --- each axle -----------------------------------------------------------
    struct AxleSpec {
        const char* heading; ///< starts a group when set
        const char* label;
        double AxleDesign::*member;
        double minimum, maximum;
        int decimals;
        Unit unit;
        const char* tip;
    };
    const auto suffixOf = [&](Unit unit) {
        switch (unit) {
        case Mm: return mm;
        case Deg: return deg;
        case Pct: return pct;
        case Ratio: break;
        }
        return QString();
    };
    const AxleSpec axle[] = {
        { "Wheel", "Track", &AxleDesign::track, 100, 5000, 1, Mm,
          "Distance across the car (Y) between the centres of the left and right contact "
          "patches." },
        { nullptr, "Static camber", &AxleDesign::camber, -15, 15, 2, Deg,
          "Lean of the wheel in front view, relative to vertical. Negative leans the top of the "
          "wheel inboard, toward the centreline." },
        { nullptr, "Static toe", &AxleDesign::toe, -10, 10, 3, Deg,
          "Angle of the wheel in top view, relative to the car's X axis. Positive (toe-in) "
          "points the front of the wheel inboard. It is stored as the wheel-axis point, the "
          "only way a hardpoint table can state toe." },
        { "Steering axis", "Caster", &AxleDesign::caster, -45, 45, 2, Deg,
          "Lean of the steering axis in side view, relative to vertical. Positive leans the top "
          "of the axis rearward." },
        { nullptr, "Kingpin inclination", &AxleDesign::kingpinInclination, -45, 45, 2, Deg,
          "Lean of the steering axis in front view, relative to vertical. Positive leans the "
          "top of the axis inboard, toward the centreline." },
        { nullptr, "Scrub radius", &AxleDesign::scrubRadius, -300, 300, 1, Mm,
          "In front view, at the ground: the lateral distance (Y) from where the steering axis "
          "meets the ground to the centre of the contact patch. Positive puts the contact "
          "patch outboard of the axis." },
        { nullptr, "Mechanical trail", &AxleDesign::mechanicalTrail, -300, 300, 1, Mm,
          "In side view, at the ground: the distance along X from where the steering axis meets "
          "the ground back to the centre of the contact patch. Positive puts the contact patch "
          "behind the axis." },
        { "Instant centres", "Roll centre height", &AxleDesign::rollCentreHeight, -500, 1000, 1,
          Mm, "Height above the ground (Z) of this axle's roll centre at design. The front-view "
          "instant centre is placed on the line from the contact patch through this point." },
        { nullptr, "Front-view swing arm", &AxleDesign::frontViewSwingArm, 1, 1e7, 0, Mm,
          "In front view: the lateral distance (Y) from the contact patch inboard to the "
          "front-view instant centre, where the two wishbone planes meet. Longer means less "
          "camber change in bump." },
        { nullptr, "Anti-dive / anti-lift", &AxleDesign::antiPercent, -200, 300, 1, Pct,
          "Percentage of the braking load transfer taken by the suspension geometry instead of "
          "the springs. Front: anti-dive, rear: anti-lift. 0 % = none, 100 % = full." },
        { nullptr, "Side-view swing arm", &AxleDesign::sideViewSwingArm, 1, 1e7, 0, Mm,
          "In side view: the distance from the contact patch to the side-view instant centre, "
          "measured toward the other axle (rearward for the front axle, forward for the rear). "
          "Its angle comes from the anti-dive / anti-lift above." },
        { "Wishbones", "Upper ball joint, height above wheel centre", &AxleDesign::upperJointHeight,
          -500, 500, 1, Mm,
          "Vertical distance (Z) from the wheel centre up to the upper ball joint. The joint is "
          "placed on the steering axis at that height, so caster and kingpin inclination decide "
          "its X and Y. Positive is above the wheel centre." },
        { nullptr, "Lower ball joint, depth below wheel centre", &AxleDesign::lowerJointDrop, -500,
          500, 1, Mm,
          "Vertical distance (Z) from the wheel centre down to the lower ball joint, placed on "
          "the steering axis. Positive is below the wheel centre. The outer tie rod end is put "
          "at this same distance from the wheel centre, in side view." },
        { nullptr, "Upper chassis pivots, distance from centreline", &AxleDesign::upperPivotY, 0,
          3000, 1, Mm,
          "Lateral distance (Y) from the car's centreline to the front and rear chassis pivots "
          "of the upper wishbone. Each leg runs inboard from the ball joint until it reaches "
          "this distance. Not used while the pivots are placed on the imported chassis surface, "
          "except for a leg that misses it." },
        { nullptr, "Lower chassis pivots, distance from centreline", &AxleDesign::lowerPivotY, 0,
          3000, 1, Mm,
          "Lateral distance (Y) from the car's centreline to the front and rear chassis pivots "
          "of the lower wishbone. Each leg runs inboard from the ball joint until it reaches "
          "this distance. Not used while the pivots are placed on the imported chassis surface, "
          "except for a leg that misses it." },
        { nullptr, "Upper wishbone front leg, angle in top view", &AxleDesign::upperForwardAngle,
          -79, 79, 1, Deg,
          "In top view: the angle between the front leg (ball joint to front chassis pivot) and "
          "a line straight across the car (Y). 0 runs straight across; positive angles the leg "
          "forward (+X) as it goes inboard, negative rearward." },
        { nullptr, "Upper wishbone rear leg, angle in top view", &AxleDesign::upperRearwardAngle,
          -79, 79, 1, Deg,
          "In top view: the angle between the rear leg (ball joint to rear chassis pivot) and a "
          "line straight across the car (Y). 0 runs straight across; positive angles the leg "
          "rearward (-X) as it goes inboard, negative forward." },
        { nullptr, "Lower wishbone front leg, angle in top view", &AxleDesign::lowerForwardAngle,
          -79, 79, 1, Deg,
          "In top view: the angle between the front leg (ball joint to front chassis pivot) and "
          "a line straight across the car (Y). 0 runs straight across; positive angles the leg "
          "forward (+X) as it goes inboard, negative rearward." },
        { nullptr, "Lower wishbone rear leg, angle in top view", &AxleDesign::lowerRearwardAngle,
          -79, 79, 1, Deg,
          "In top view: the angle between the rear leg (ball joint to rear chassis pivot) and a "
          "line straight across the car (Y). 0 runs straight across; positive angles the leg "
          "rearward (-X) as it goes inboard, negative forward." },
        { "Steering", "Outer tie rod end, x behind wheel centre", &AxleDesign::steeringArm, -500,
          500, 1, Mm,
          "Longitudinal distance (X) from the wheel centre to the outer tie rod end. Positive "
          "puts the tie rod behind the wheel centre, negative ahead of it. Its height follows "
          "from the lower ball joint's depth." },
        { nullptr, "Outer tie rod end, y inboard of steering axis", &AxleDesign::ackermann, -200,
          200, 1, Mm,
          "Lateral distance (Y) from the steering axis inboard to the outer tie rod end, at the "
          "tie rod end's height. Positive is toward the centreline. With the distance behind "
          "the wheel centre it sets the steering arm's angle in top view, and with it the "
          "Ackermann." },
        { nullptr, "Inner tie rod end, x offset from outer end", &AxleDesign::tieRodInboardOffsetX,
          -500, 500, 1, Mm,
          "Longitudinal distance (X) from the outer tie rod end to the inner one. Positive puts "
          "the inner end ahead (+X), negative behind. It moves the inner end along X only, so "
          "the tie rod's front view -- and with it the bump steer -- stays the same." },
        { "Pushrod and rocker", "Pushrod pickup, along the arm", &AxleDesign::pushrodPickupInboard,
          -500, 1000, 1, Mm,
          "Distance from the ball joint of the wishbone the pushrod is mounted on (as the "
          "linkage template says), along that wishbone toward its chassis pivots. A pushrod on "
          "the upright is placed by the lower wishbone's numbers." },
        { nullptr, "Pushrod pickup, above the arm", &AxleDesign::pushrodPickupHeight, -500, 500, 1,
          Mm, "Distance of the pushrod pickup from the wishbone's plane. Positive is above the "
              "plane, negative below it." },
        { nullptr, "Rocker pivot, from centreline", &AxleDesign::rockerPivotY, 0, 3000, 1,
          Mm, "Lateral distance (Y) from the car's centreline to the rocker's pivot." },
        { nullptr, "Rocker pivot, height", &AxleDesign::rockerPivotZ, -500, 3000, 1,
          Mm, "Height of the rocker's pivot above the ground (Z)." },
        { nullptr, "Rocker pivot, ahead of the pickup", &AxleDesign::rockerPivotOffsetX, -1000,
          1000, 1, Mm,
          "Longitudinal distance (X) from the pushrod's outer pickup to the rocker's pivot. "
          "Positive is ahead (+X). The rocker turns about an axis along X." },
        { nullptr, "Rocker arm to the pushrod", &AxleDesign::rockerPushrodArm, 1, 1000, 1,
          Mm, "Distance on the rocker from its pivot to the pushrod's inner end. The pushrod "
              "meets this arm at a right angle at design." },
        { nullptr, "Damper arm, turned from the pushrod's", &AxleDesign::rockerDamperAngle, -180,
          180, 1, Deg,
          "Angle on the rocker, seen along its pivot axis, from the pushrod's arm to the "
          "damper's arm. Positive turns inboard, toward the centreline." },
        { nullptr, "Installation ratio", &AxleDesign::installationRatio, 0.05, 5, 3, Ratio,
          "Damper compression per unit of wheel bump at design (motion ratio, damper over "
          "wheel). The damper's arm on the rocker is sized so that this is met exactly." },
        { nullptr, "Damper length", &AxleDesign::damperLength, 1, 2000, 1, Mm,
          "Eye-to-eye length of the damper at design. The damper is laid at a right angle to "
          "its rocker arm, so that bump compresses it." },
        { "Anti-roll bar", "Drop link arm on the rocker", &AxleDesign::antiRollRockerArm, 1, 1000,
          1, Mm,
          "Distance on the rocker from its pivot to the drop link's upper end. Used when the "
          "linkage template mounts the drop link on the rocker." },
        { nullptr, "Drop link arm, turned from the pushrod's", &AxleDesign::antiRollRockerAngle,
          -180, 180, 1, Deg,
          "Angle on the rocker, seen along its pivot axis, from the pushrod's arm to the drop "
          "link's arm. Positive turns inboard, toward the centreline. Used when the drop link is "
          "on the rocker." },
        { nullptr, "Drop link pickup, along the arm", &AxleDesign::antiRollPickupInboard, -500,
          1000, 1, Mm,
          "Used when the linkage template mounts the drop link on a wishbone or the upright: "
          "the distance from that wishbone's ball joint toward its chassis pivots." },
        { nullptr, "Drop link length", &AxleDesign::dropLinkLength, 1, 1000, 1, Mm,
          "Length of the drop link between its two ball joints. It is laid at a right angle to "
          "the rocker arm it hangs from." },
        { nullptr, "Bar arm length", &AxleDesign::antiRollArmLength, -1000, 1000, 1,
          Mm, "Length of the anti-roll bar's arm, from the bar's axis to the drop link. "
              "Positive runs it forward (+X) from the bar, negative rearward." },
        { nullptr, "Bearing inboard of the arm", &AxleDesign::antiRollBearingInset, 1, 2000, 1,
          Mm, "Lateral distance (Y) from the bar's arm root inboard to its bearing. The bar's "
              "axis runs through both, across the car." },
    };

    // A switch opens some groups: whether the group is generated at all.
    struct GroupSwitch {
        const char* heading;
        const char* label;
        bool AxleDesign::*member;
        const char* tip;
    };
    const GroupSwitch switches[] = {
        { "Steering", "Driven by the steering rack", &AxleDesign::steered,
          "Tick if this axle has a steering rack: the rack moves the inner tie rod end across "
          "the car (Y). Unticked, the inner tie rod end is fixed to the chassis, as on a rear "
          "axle's toe link. Written into the linkage template for this corner." },
        { "Pushrod and rocker", "Generate the pushrod, rocker and damper",
          &AxleDesign::generateRocker,
          "Tick to place the pushrod, rocker and damper points from the numbers below. Untick "
          "to keep a rocker placed by hand: none of its points are touched." },
        { "Anti-roll bar", "Generate the anti-roll bar", &AxleDesign::generateAntiRollBar,
          "Tick to place the anti-roll bar's points from the numbers below, on the mount the "
          "linkage template names for the drop link. Untick to keep a bar placed by hand." },
    };

    for (const AxleSpec& spec : axle) {
        if (spec.heading) {
            addHeading(grid, &row, tr(spec.heading));
            for (const GroupSwitch& group : switches) {
                if (QLatin1String(group.heading) != QLatin1String(spec.heading)) continue;
                grid->addWidget(new QLabel(tr(group.label), host), row, 0);
                AxleFlag flag{ group.member, { nullptr, nullptr } };
                for (int column = 0; column < 2; ++column) {
                    flag.box[column] = new QCheckBox(host);
                    flag.box[column]->setToolTip(tr(group.tip));
                    connect(flag.box[column], &QCheckBox::toggled, this,
                            &GenerateDialog::schedulePlan);
                    grid->addWidget(flag.box[column], row, 1 + column, Qt::AlignCenter);
                }
                m_axleFlags.push_back(flag);
                ++row;
            }
        }
        AxleNumber field{ spec.member, { nullptr, nullptr } };
        for (int column = 0; column < 2; ++column) {
            field.spin[column] = number(spec.minimum, spec.maximum, spec.decimals,
                                        suffixOf(spec.unit), tr(spec.tip));
            grid->addWidget(field.spin[column], row, 1 + column);
        }
        auto* label = new QLabel(tr(spec.label), host);
        label->setToolTip(tr(spec.tip));
        grid->addWidget(label, row++, 0);
        m_axleNumbers.push_back(field);
    }

    const QString advisedTip =
        tr("For low bump steer the inner tie rod end should lie in the plane of the wishbones' "
           "chassis pivots. Four pivots rarely share one plane, so three of them define it and "
           "the inner tie rod end is placed on it. Pick the fourth here: where it would have to "
           "move to lie in the same plane is shown as bump-steer advice, never applied.");
    auto* advisedLabel = new QLabel(tr("Chassis pivot excluded from the tie-rod plane"), host);
    advisedLabel->setToolTip(advisedTip);
    grid->addWidget(advisedLabel, row, 0);
    const QPair<DesignPivot, QString> pivots[] = {
        { DesignPivot::LowerFront, tr("Lower front") },
        { DesignPivot::LowerRear, tr("Lower rear") },
        { DesignPivot::UpperFront, tr("Upper front") },
        { DesignPivot::UpperRear, tr("Upper rear") },
    };
    for (int column = 0; column < 2; ++column) {
        m_advised[column] = new QComboBox(host);
        for (const auto& [pivot, text] : pivots) m_advised[column]->addItem(text, int(pivot));
        m_advised[column]->setToolTip(advisedTip);
        connect(m_advised[column], &QComboBox::currentIndexChanged, this,
                &GenerateDialog::schedulePlan);
        grid->addWidget(m_advised[column], row, 1 + column);
    }
    ++row;
    grid->setRowStretch(row, 1);

    auto* scroll = new QScrollArea(this);
    scroll->setWidget(host);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    return scroll;
}

QWidget* GenerateDialog::buildPreview()
{
    auto* host = new QWidget(this);
    auto* layout = new QVBoxLayout(host);
    layout->setContentsMargins(8, 0, 0, 0);

    auto* title = new QLabel(tr("<b>What Generate will do</b>"), host);
    layout->addWidget(title);

    m_summary = new QLabel(host);
    m_summary->setWordWrap(true);
    layout->addWidget(m_summary);

    m_preview = new QTreeWidget(host);
    m_preview->setRootIsDecorated(false);
    m_preview->setUniformRowHeights(true);
    m_preview->setHeaderLabels({ tr("Point"), tr("Change"), tr("Note") });
    m_preview->header()->setStretchLastSection(true);
    m_preview->setSelectionMode(QAbstractItemView::NoSelection);
    layout->addWidget(m_preview, 3);

    m_notes = new QLabel(host);
    m_notes->setWordWrap(true);
    m_notes->setTextFormat(Qt::RichText);
    m_notes->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    m_notes->setTextInteractionFlags(Qt::TextSelectableByMouse);
    auto* notesScroll = new QScrollArea(host);
    notesScroll->setWidget(m_notes);
    notesScroll->setWidgetResizable(true);
    notesScroll->setFrameShape(QFrame::NoFrame);
    layout->addWidget(notesScroll, 2);
    return host;
}

void GenerateDialog::load(const DesignParameters& parameters)
{
    m_loading = true;
    for (const CarNumber& field : m_carNumbers) field.spin->setValue(parameters.*field.member);
    m_useChassis->setChecked(m_chassisAvailable && parameters.useChassis);
    m_side->setCurrentIndex(m_side->findData(int(parameters.side)));

    for (int column = 0; column < 2; ++column) {
        const AxleDesign& axle = parameters.axle(positionOf(column));
        for (const AxleNumber& field : m_axleNumbers) field.spin[column]->setValue(axle.*field.member);
        for (const AxleFlag& flag : m_axleFlags) flag.box[column]->setChecked(axle.*flag.member);
        m_advised[column]->setCurrentIndex(m_advised[column]->findData(int(axle.advisedPivot)));
        const int corner = m_corner[column]->findData(axle.corner);
        m_corner[column]->setCurrentIndex(corner >= 0 ? corner : std::min(column, m_corner[column]->count() - 1));
        m_generate[column]->setChecked(axle.generate && m_corner[column]->count() > column);
    }
    m_loading = false;
}

DesignParameters GenerateDialog::parameters() const
{
    DesignParameters parameters = m_seed;
    for (const CarNumber& field : m_carNumbers) parameters.*field.member = field.spin->value();
    // A box that could not be ticked says nothing about what was wanted, so
    // the stored answer stays until there is geometry to ask the question of.
    if (m_chassisAvailable) parameters.useChassis = m_useChassis->isChecked();
    parameters.side = static_cast<DesignSide>(m_side->currentData().toInt());

    for (int column = 0; column < 2; ++column) {
        AxleDesign& axle = parameters.axle(positionOf(column));
        for (const AxleNumber& field : m_axleNumbers) axle.*field.member = field.spin[column]->value();
        axle.generate = m_generate[column]->isChecked();
        if (m_corner[column]->currentIndex() >= 0) axle.corner = m_corner[column]->currentData().toString();
        for (const AxleFlag& flag : m_axleFlags) axle.*flag.member = flag.box[column]->isChecked();
        axle.advisedPivot = static_cast<DesignPivot>(m_advised[column]->currentData().toInt());
    }
    return parameters;
}

void GenerateDialog::schedulePlan()
{
    if (m_loading) return;
    m_timer->start();
}

void GenerateDialog::rebuildPlan()
{
    m_timer->stop();
    const DesignParameters targets = parameters();
    const MeshQuery* chassis =
        (targets.useChassis && m_chassisAvailable && m_chassis) ? m_chassis() : nullptr;
    m_plan = planDesign(targets, m_templ, m_mirror, m_current, m_baseline, chassis);
    showPlan();
}

void GenerateDialog::showPlan()
{
    const QPalette pal = palette();
    const QString errorColor = issueColor(ConfigIssueLevel::Error, pal).name();
    const QString warningColor = issueColor(ConfigIssueLevel::Warning, pal).name();

    m_preview->clear();
    m_buttons->button(QDialogButtonBox::Ok)->setEnabled(m_plan.ok());

    if (!m_plan.ok()) {
        m_summary->setText(QStringLiteral("<span style=\"color:%1\">%2</span>")
                               .arg(errorColor, m_plan.error.toHtmlEscaped()));
        m_notes->clear();
        return;
    }

    const int added = m_plan.count(DesignChange::Kind::Added);
    const int moved = m_plan.count(DesignChange::Kind::Moved);
    const int unchanged = m_plan.count(DesignChange::Kind::Unchanged);
    const int edited = m_plan.handEditedCount();
    QString summary = tr("%1 new, %2 moved, %3 unchanged.").arg(added).arg(moved).arg(unchanged);
    if (edited > 0) {
        summary += QStringLiteral(" <span style=\"color:%1\">%2</span>")
                       .arg(warningColor,
                            tr("%n point(s) you have edited by hand will be overwritten.", "", edited)
                                .toHtmlEscaped());
    }
    if (m_current.isEmpty())
        summary += QLatin1Char(' ') + tr("The project has no workbook yet; one is made for them.");
    m_summary->setText(summary);

    const QColor warn(warningColor);
    for (const DesignChange& change : m_plan.changes) {
        QString what;
        switch (change.kind) {
        case DesignChange::Kind::Added: what = tr("new"); break;
        case DesignChange::Kind::Moved: what = tr("moves %1 mm").arg(change.distance, 0, 'f', 2); break;
        case DesignChange::Kind::Unchanged: what = tr("unchanged"); break;
        }
        QString note;
        if (change.handEdited) note = tr("your edit is overwritten");
        else if (change.mirrored) note = tr("far side, from the mirror rule");
        auto* item = new QTreeWidgetItem(m_preview, { change.name, what, note });
        if (change.handEdited)
            for (int column = 0; column < 3; ++column) item->setForeground(column, warn);
    }
    m_preview->resizeColumnToContents(0);
    m_preview->resizeColumnToContents(1);

    QString notes;
    if (!m_plan.advice.isEmpty())
        notes += tr("<b>Bump steer</b> (advice, not applied)") + bulleted(m_plan.advice);
    if (!m_plan.warnings.isEmpty())
        notes += tr("<b>Worth knowing</b>") + bulleted(m_plan.warnings);
    if (m_plan.steeringChanged) {
        QStringList steering;
        for (const CornerSpec& corner : m_plan.steering) {
            const QString name = corner.label.isEmpty() ? corner.token : corner.label;
            steering << (corner.steeringRack.isEmpty() ? tr("%1: not steered").arg(name)
                                                       : tr("%1: steered by the rack").arg(name));
        }
        notes += tr("<b>The linkage template will say</b>") + bulleted(steering);
    }
    int onChassis = 0;
    for (const GeneratedCorner& corner : m_plan.corners) onChassis += corner.pivotsOnChassis;
    if (onChassis > 0)
        notes += QStringLiteral("<p>%1</p>").arg(
            tr("%n pivot(s) were placed on the imported chassis surface.", "", onChassis));
    m_notes->setText(notes);
}

void GenerateDialog::accept()
{
    // The preview runs a moment behind the typing. What is generated is what
    // the fields say now, not what they said a keystroke ago.
    rebuildPlan();
    if (!m_plan.ok()) return;
    QDialog::accept();
}

} // namespace suspkin
