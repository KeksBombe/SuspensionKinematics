#include "app/SweepParametersDialog.h"

#include <QCheckBox>
#include <QCloseEvent>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFont>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QVBoxLayout>

namespace suspkin {
namespace {

/// What each block is called when it is not the one being swept. The suffix
/// below is added to whichever one is.
QString bumpTitle() { return SweepParametersDialog::tr("Bump and rebound"); }
QString rollTitle() { return SweepParametersDialog::tr("Roll"); }
QString steerTitle() { return SweepParametersDialog::tr("Steer"); }

/// The line under each block: the range its numbers come to, and how many
/// positions that is. Written out because a travel and an increment are what is
/// typed, and a range and a step count are what gets solved.
QString summaryFor(const SweepSettings& settings, SweepKind kind)
{
    const SweepSpec spec = settings.specFor(kind);
    const QString unit = sweepInputUnit(kind);
    return SweepParametersDialog::tr("%1 to %2 %3 in %4 steps (%5 positions)")
        .arg(QString::number(spec.from, 'f', 2), QString::number(spec.to, 'f', 2), unit,
             QString::number(settings.incrementFor(kind), 'f', 3))
        .arg(spec.steps);
}

} // namespace

SweepParametersDialog::SweepParametersDialog(QWidget* parent) : QDialog(parent)
{
    setWindowTitle(tr("Sweep Parameters"));
    // Not modal: the point of taking these out of the dock is to be able to
    // watch the viewport while they are changed.
    setModal(false);
    // Wide enough that the line under each block saying what its numbers come
    // to fits on one line, which is the only thing here that needs room.
    setMinimumWidth(380);

    auto* layout = new QVBoxLayout(this);

    const auto addSummary = [this](QVBoxLayout* box) {
        auto* label = new QLabel(this);
        label->setWordWrap(true);
        QFont smaller = label->font();
        smaller.setPointSizeF(smaller.pointSizeF() * 0.9);
        label->setFont(smaller);
        box->addWidget(label);
        return label;
    };

    // --- bump ------------------------------------------------------------
    m_bumpGroup = new QGroupBox(bumpTitle(), this);
    auto* bumpBox = new QVBoxLayout(m_bumpGroup);
    auto* bumpForm = new QFormLayout;
    m_bumpTravel = addNumber(bumpForm, tr("Bump travel (mm)"), 0.0, 1000.0, 2, 1.0,
                             tr("How far the wheel is taken upward from the design position."));
    m_reboundTravel =
        addNumber(bumpForm, tr("Rebound travel (mm)"), 0.0, 1000.0, 2, 1.0,
                  tr("How far the wheel is taken downward. Written positive: the sweep runs "
                     "from minus this to plus the bump travel."));
    m_bumpIncrement = addNumber(bumpForm, tr("Increment (mm)"), 0.01, 100.0, 3, 0.5,
                                tr("Millimetres of wheel travel between solved positions."));
    bumpBox->addLayout(bumpForm);
    m_bumpSummary = addSummary(bumpBox);
    layout->addWidget(m_bumpGroup);

    // --- roll ------------------------------------------------------------
    m_rollGroup = new QGroupBox(rollTitle(), this);
    auto* rollBox = new QVBoxLayout(m_rollGroup);
    auto* rollForm = new QFormLayout;
    m_rollAngle = addNumber(rollForm, tr("Roll angle (deg)"), 0.0, 45.0, 3, 0.25,
                            tr("Degrees the body is rolled either side of level."));
    m_rollIncrement = addNumber(rollForm, tr("Increment (deg)"), 0.001, 10.0, 3, 0.05,
                                tr("Degrees of body roll between solved positions."));
    rollBox->addLayout(rollForm);
    m_rollSummary = addSummary(rollBox);
    layout->addWidget(m_rollGroup);

    // --- steer -----------------------------------------------------------
    m_steerGroup = new QGroupBox(steerTitle(), this);
    auto* steerBox = new QVBoxLayout(m_steerGroup);
    auto* steerForm = new QFormLayout;
    m_steerTravel = addNumber(steerForm, tr("Steer travel (mm)"), 0.0, 500.0, 2, 1.0,
                              tr("Rack movement either side of centre."));
    m_steerIncrement = addNumber(steerForm, tr("Increment (mm)"), 0.01, 100.0, 3, 0.5,
                                 tr("Millimetres of rack between solved positions."));
    steerBox->addLayout(steerForm);
    m_steerSummary = addSummary(steerBox);
    layout->addWidget(m_steerGroup);

    // --- what holds across every sweep ------------------------------------
    auto* commonGroup = new QGroupBox(tr("While simulating"), this);
    auto* commonForm = new QFormLayout(commonGroup);
    m_rack = addNumber(commonForm, tr("Rack held (mm)"), -500.0, 500.0, 2, 1.0,
                       tr("Rack position held through a bump or roll sweep, so bump steer can "
                          "be looked at on a wheel that is already turned. The steer sweep "
                          "moves the rack itself and ignores this."));
    m_seconds = addNumber(commonForm, tr("Animation (s/cycle)"), 0.2, 60.0, 1, 0.5,
                          tr("How long one there-and-back run of the travel takes."));
    m_allAxles = new QCheckBox(tr("Move all axles"), this);
    m_allAxles->setToolTip(tr("Move every axle together, which is also what lets the body roll "
                              "about the roll axis in a roll sweep: with one axle moving, the "
                              "chassis stays put. The curve and the readout still belong to the "
                              "axle chosen in the analysis panel."));
    commonForm->addRow(QString(), m_allAxles);
    layout->addWidget(commonGroup);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::close);
    layout->addWidget(buttons);

    // --- wiring ------------------------------------------------------------
    // Every edit is live. m_updating is what stops the panel setting these from
    // the project and having it read straight back out as a change.
    const auto edited = [this] {
        if (m_updating) return;
        refreshSummaries();
        emit settingsChanged();
    };
    for (QDoubleSpinBox* box : { m_bumpTravel, m_reboundTravel, m_bumpIncrement, m_rollAngle,
                                 m_rollIncrement, m_steerTravel, m_steerIncrement, m_rack })
        connect(box, &QDoubleSpinBox::valueChanged, this, edited);

    connect(m_seconds, &QDoubleSpinBox::valueChanged, this, [this] {
        if (!m_updating) emit animationSecondsChanged();
    });
    connect(m_allAxles, &QCheckBox::toggled, this, [this] {
        if (!m_updating) emit movesAllAxlesChanged();
    });

    setSettings(SweepSettings{});
    m_seconds->setValue(4.0);
    m_allAxles->setChecked(true);
    setKind(SweepKind::Bump);
}

QDoubleSpinBox* SweepParametersDialog::addNumber(QFormLayout* form, const QString& label,
                                                 double minimum, double maximum, int decimals,
                                                 double step, const QString& tip)
{
    auto* box = new QDoubleSpinBox(this);
    box->setRange(minimum, maximum);
    box->setDecimals(decimals);
    box->setSingleStep(step);
    box->setToolTip(tip);
    box->setAlignment(Qt::AlignRight);
    form->addRow(label, box);
    return box;
}

SweepSettings SweepParametersDialog::settings() const
{
    SweepSettings settings;
    settings.bumpTravel = m_bumpTravel->value();
    settings.reboundTravel = m_reboundTravel->value();
    settings.bumpIncrement = m_bumpIncrement->value();
    settings.rollAngle = m_rollAngle->value();
    settings.rollIncrement = m_rollIncrement->value();
    settings.steerTravel = m_steerTravel->value();
    settings.steerIncrement = m_steerIncrement->value();
    settings.rackTravel = m_rack->value();
    return settings;
}

void SweepParametersDialog::setSettings(const SweepSettings& settings)
{
    m_updating = true;
    m_bumpTravel->setValue(settings.bumpTravel);
    m_reboundTravel->setValue(settings.reboundTravel);
    m_bumpIncrement->setValue(settings.bumpIncrement);
    m_rollAngle->setValue(settings.rollAngle);
    m_rollIncrement->setValue(settings.rollIncrement);
    m_steerTravel->setValue(settings.steerTravel);
    m_steerIncrement->setValue(settings.steerIncrement);
    m_rack->setValue(settings.rackTravel);
    m_updating = false;
    refreshSummaries();
}

double SweepParametersDialog::animationSeconds() const { return m_seconds->value(); }

void SweepParametersDialog::setAnimationSeconds(double seconds)
{
    m_updating = true;
    m_seconds->setValue(seconds);
    m_updating = false;
}

bool SweepParametersDialog::movesAllAxles() const { return m_allAxles->isChecked(); }

void SweepParametersDialog::setMovesAllAxles(bool all)
{
    m_updating = true;
    m_allAxles->setChecked(all);
    m_updating = false;
}

void SweepParametersDialog::setKind(SweepKind kind)
{
    m_kind = kind;
    refreshSummaries();
}

void SweepParametersDialog::setSteeringAvailable(bool available)
{
    const QString why =
        available ? QString()
                  : tr("This axle has no steering: the linkage template names no hardpoint for a "
                       "rack to drive. Linkage > Steering Rack names one.");
    m_steerGroup->setEnabled(available);
    m_steerGroup->setToolTip(why);
    // The held rack goes with it: on an axle with no rack it is a number that
    // would be quietly ignored by every sweep, which is worse than a control
    // that is visibly not for you.
    m_rack->setEnabled(available);
    m_rack->setToolTip(available ? tr("Rack position held through a bump or roll sweep, so bump "
                                      "steer can be looked at on a wheel that is already turned. "
                                      "The steer sweep moves the rack itself and ignores this.")
                                 : why);
}

void SweepParametersDialog::refreshSummaries()
{
    const SweepSettings current = settings();
    m_bumpSummary->setText(summaryFor(current, SweepKind::Bump));
    m_rollSummary->setText(summaryFor(current, SweepKind::Roll));
    m_steerSummary->setText(summaryFor(current, SweepKind::Steer));

    // The live block says so in its own title. Marking it rather than
    // disabling the other two: setting up a roll sweep before switching to it
    // is the normal way round.
    const auto title = [this](SweepKind kind, const QString& base) {
        return kind == m_kind ? tr("%1 (swept now)").arg(base) : base;
    };
    m_bumpGroup->setTitle(title(SweepKind::Bump, bumpTitle()));
    m_rollGroup->setTitle(title(SweepKind::Roll, rollTitle()));
    m_steerGroup->setTitle(title(SweepKind::Steer, steerTitle()));
}

void SweepParametersDialog::closeEvent(QCloseEvent* event)
{
    QDialog::closeEvent(event);
    if (event->isAccepted()) emit closedByUser();
}

} // namespace suspkin
