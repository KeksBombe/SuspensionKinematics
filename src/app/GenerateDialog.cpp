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
    defaults->setToolTip(tr("The 2025 car's targets, which are what the generator ships with. "
                            "Which corners are generated, and on which side, are kept."));
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
    grid->addWidget(new QLabel(tr("Generate this axle into corner"), host), row, 0);
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
    m_side->setToolTip(tr("The points are generated on this side under the template's own names; "
                          "the other side comes from the project's mirror rule."));
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
        { "Wheelbase", &DesignParameters::wheelbase, 300, 6000, 1, "mm", "" },
        { "Centre of gravity, x", &DesignParameters::cogX, -20000, 20000, 1, "mm",
          "Where the centre of gravity is along the car, in the car's own frame." },
        { "Centre of gravity, height", &DesignParameters::cogHeight, 0, 3000, 1, "mm", "" },
        { "Weight on the front axle", &DesignParameters::frontWeight, 1, 99, 1, "%",
          "Of the static load. It is what places the two axles either side of the centre of "
          "gravity." },
        { "Front brake bias", &DesignParameters::frontBrakeBias, 0, 100, 1, "%",
          "The share of the braking the front axle does. Anti-dive and anti-lift are measured "
          "against each axle's own share." },
        { "Loaded radius", &DesignParameters::loadedRadius, 10, 2000, 1, "mm",
          "The wheel centre's height above the ground." },
        { "Rim radius", &DesignParameters::rimRadius, 0, 2000, 1, "mm",
          "A ball joint further from the wheel's axis than this is outside the rim, and is "
          "warned about." },
    };
    for (const CarSpec& spec : car) {
        const QString unit = QLatin1String(spec.unit) == QLatin1String("%") ? pct : mm;
        QDoubleSpinBox* spin = number(spec.minimum, spec.maximum, spec.decimals, unit, tr(spec.tip));
        grid->addWidget(new QLabel(tr(spec.label), host), row, 0);
        grid->addWidget(spin, row++, 1);
        m_carNumbers.push_back(CarNumber{ spec.member, spin });
    }

    m_useChassis = new QCheckBox(tr("Put the chassis pivots against the imported geometry"), host);
    m_useChassis->setEnabled(m_chassisAvailable);
    m_useChassis->setToolTip(
        m_chassisAvailable
            ? tr("Each wishbone leg is cast from its ball joint toward the chassis, and its pivot "
                 "put the clearance below off the surface it meets. Only worth it when the "
                 "imported geometry is the chassis on its own: an upright in the model is met "
                 "first.")
            : tr("Import the chassis geometry first."));
    connect(m_useChassis, &QCheckBox::toggled, this, &GenerateDialog::schedulePlan);
    grid->addWidget(m_useChassis, row++, 0, 1, 3);
    QDoubleSpinBox* clearance = number(0, 500, 1, mm, tr("How far off the chassis surface each "
                                                           "pivot is put."));
    grid->addWidget(new QLabel(tr("Chassis clearance"), host), row, 0);
    grid->addWidget(clearance, row++, 1);
    m_carNumbers.push_back(CarNumber{ &DesignParameters::chassisClearance, clearance });

    // --- each axle -----------------------------------------------------------
    struct AxleSpec {
        const char* heading; ///< starts a group when set
        const char* label;
        double AxleDesign::*member;
        double minimum, maximum;
        int decimals;
        bool degrees;
        const char* tip;
    };
    const AxleSpec axle[] = {
        { "Wheel", "Track", &AxleDesign::track, 100, 5000, 1, false,
          "Between the centres of the two contact patches." },
        { nullptr, "Static camber", &AxleDesign::camber, -15, 15, 2, true,
          "Negative leans the top of the wheel inboard." },
        { nullptr, "Static toe", &AxleDesign::toe, -10, 10, 3, true,
          "Positive points the front of the wheel inboard. Stated in the table by the point on "
          "the wheel's axis, which is the only way a hardpoint table can state it." },
        { "Steering axis", "Caster", &AxleDesign::caster, -45, 45, 2, true,
          "Positive leans the top of the steering axis rearward." },
        { nullptr, "Kingpin inclination", &AxleDesign::kingpinInclination, -45, 45, 2, true,
          "Positive leans the top of the steering axis inboard." },
        { nullptr, "Scrub radius", &AxleDesign::scrubRadius, -300, 300, 1, false,
          "At the ground. Positive puts the tyre outboard of the steering axis." },
        { nullptr, "Mechanical trail", &AxleDesign::mechanicalTrail, -300, 300, 1, false,
          "Positive puts the contact patch behind the steering axis." },
        { "Instant centres", "Roll centre height", &AxleDesign::rollCentreHeight, -500, 1000, 1,
          false, "The roll centre this axle's front-view instant centre is placed to give." },
        { nullptr, "Front-view swing arm", &AxleDesign::frontViewSwingArm, 1, 1e7, 0, false,
          "How far inboard of the contact patch the front-view instant centre is." },
        { nullptr, "Anti-dive / anti-lift", &AxleDesign::antiPercent, -200, 300, 1, false,
          "Under braking: anti-dive on the front axle, anti-lift on the rear." },
        { nullptr, "Side-view swing arm", &AxleDesign::sideViewSwingArm, 1, 1e7, 0, false,
          "How far from the contact patch the side-view instant centre is, toward the other "
          "axle." },
        { "Wishbones", "Upper ball joint above centre", &AxleDesign::upperJointHeight, -500, 500,
          1, false, "On the steering axis, this far above the wheel centre." },
        { nullptr, "Lower ball joint below centre", &AxleDesign::lowerJointDrop, -500, 500, 1,
          false, "On the steering axis, this far below the wheel centre. Also the radius the "
                 "outer tie rod end is put on." },
        { nullptr, "Upper pivot line", &AxleDesign::upperPivotY, 0, 3000, 1, false,
          "The upper chassis pivots' distance from the car's centreline, where there is no "
          "chassis to put them against." },
        { nullptr, "Lower pivot line", &AxleDesign::lowerPivotY, 0, 3000, 1, false,
          "The lower chassis pivots' distance from the car's centreline." },
        { nullptr, "Upper leg, forward sweep", &AxleDesign::upperForwardAngle, -79, 79, 1, true,
          "In top view, the angle the leg makes with a line straight across the car." },
        { nullptr, "Upper leg, rearward sweep", &AxleDesign::upperRearwardAngle, -79, 79, 1, true, "" },
        { nullptr, "Lower leg, forward sweep", &AxleDesign::lowerForwardAngle, -79, 79, 1, true, "" },
        { nullptr, "Lower leg, rearward sweep", &AxleDesign::lowerRearwardAngle, -79, 79, 1, true, "" },
        { "Steering", "Steering arm", &AxleDesign::steeringArm, -500, 500, 1, false,
          "From the wheel centre to the outer tie rod end, along the car. Positive puts the tie "
          "rod behind the wheel centre." },
        { nullptr, "Ackermann offset", &AxleDesign::ackermann, -200, 200, 1, false,
          "How far inboard of the steering axis the outer tie rod end sits." },
        { nullptr, "Inner tie rod end, x offset", &AxleDesign::tieRodInboardOffsetX, -500, 500, 1,
          false, "How far ahead of the outer end the inner end is put. It moves nothing in front "
                 "view, so it leaves the bump steer alone." },
    };

    for (const AxleSpec& spec : axle) {
        if (spec.heading) {
            addHeading(grid, &row, tr(spec.heading));
            if (QLatin1String(spec.heading) == QLatin1String("Steering")) {
                grid->addWidget(new QLabel(tr("Driven by the steering rack"), host), row, 0);
                for (int column = 0; column < 2; ++column) {
                    m_steered[column] = new QCheckBox(host);
                    m_steered[column]->setToolTip(
                        tr("Written into the template's corner, so that an axle generated without "
                           "a rack is not steered by one."));
                    connect(m_steered[column], &QCheckBox::toggled, this,
                            &GenerateDialog::schedulePlan);
                    grid->addWidget(m_steered[column], row, 1 + column, Qt::AlignCenter);
                }
                ++row;
            }
        }
        AxleNumber field{ spec.member, { nullptr, nullptr } };
        for (int column = 0; column < 2; ++column) {
            field.spin[column] = number(spec.minimum, spec.maximum, spec.decimals,
                                        spec.degrees ? deg : mm, tr(spec.tip));
            grid->addWidget(field.spin[column], row, 1 + column);
        }
        auto* label = new QLabel(tr(spec.label), host);
        label->setToolTip(tr(spec.tip));
        grid->addWidget(label, row++, 0);
        m_axleNumbers.push_back(field);
    }

    grid->addWidget(new QLabel(tr("Pivot left off the tie rod's plane"), host), row, 0);
    const QPair<DesignPivot, QString> pivots[] = {
        { DesignPivot::LowerFront, tr("Lower front") },
        { DesignPivot::LowerRear, tr("Lower rear") },
        { DesignPivot::UpperFront, tr("Upper front") },
        { DesignPivot::UpperRear, tr("Upper rear") },
    };
    for (int column = 0; column < 2; ++column) {
        m_advised[column] = new QComboBox(host);
        for (const auto& [pivot, text] : pivots) m_advised[column]->addItem(text, int(pivot));
        m_advised[column]->setToolTip(
            tr("Three chassis pivots make the plane the inner tie rod end is put on. This is the "
               "fourth: where it would have to be to share that plane is given as advice."));
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
        m_steered[column]->setChecked(axle.steered);
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
        axle.steered = m_steered[column]->isChecked();
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
            tr("%n pivot(s) were put against the imported geometry.", "", onChassis));
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
