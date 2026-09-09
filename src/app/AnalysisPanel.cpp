#include "app/AnalysisPanel.h"

#include "app/PlotWidget.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QStyle>
#include <QTableWidget>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <cmath>

namespace suspkin {
namespace {

/// The slider works in steps of this many per full range, which is fine enough
/// that dragging it looks continuous and coarse enough that every step is a
/// solve worth doing.
constexpr int kSliderSteps = 1000;

/// Thirty-odd frames a second. A corner solve is microseconds, so what this is
/// really pacing is the repaint.
constexpr int kAnimationIntervalMs = 30;

/// Which numbers are worth having in front of you while the model moves. The
/// full list is on the curve selector; this is the subset somebody actually
/// reads off a corner.
const std::vector<SweepMeasure>& readoutMeasures()
{
    static const std::vector<SweepMeasure> measures = {
        SweepMeasure::WheelTravel,       SweepMeasure::Camber,
        SweepMeasure::Toe,               SweepMeasure::Caster,
        SweepMeasure::KingpinInclination, SweepMeasure::ScrubRadius,
        SweepMeasure::HalfTrackChange,   SweepMeasure::DamperTravel,
        SweepMeasure::InstallationRatio, SweepMeasure::RollCentreHeight,
        SweepMeasure::AntiRollTwist,
    };
    return measures;
}

} // namespace

AnalysisPanel::AnalysisPanel(QWidget* parent) : QWidget(parent) { buildUi(); }

void AnalysisPanel::buildUi()
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(8);

    auto* controls = new QGridLayout;
    controls->setHorizontalSpacing(8);
    controls->setVerticalSpacing(6);

    m_axleBox = new QComboBox(this);
    m_axleBox->setToolTip(tr("Which axle to put through its travel."));
    controls->addWidget(new QLabel(tr("Axle"), this), 0, 0);
    controls->addWidget(m_axleBox, 0, 1, 1, 2);

    m_simulate = new QCheckBox(tr("Simulate"), this);
    m_simulate->setToolTip(tr("Move the suspension in the viewport. The hardpoints in the table "
                              "are not changed -- this only poses them."));
    controls->addWidget(m_simulate, 0, 3, 1, 2);

    m_positionSlider = new QSlider(Qt::Horizontal, this);
    m_positionSlider->setRange(0, kSliderSteps);
    m_positionBox = new QDoubleSpinBox(this);
    m_positionBox->setDecimals(2);
    m_positionBox->setSingleStep(1.0);
    m_positionUnit = new QLabel(QStringLiteral("mm"), this);
    controls->addWidget(new QLabel(tr("Position"), this), 1, 0);
    controls->addWidget(m_positionSlider, 1, 1, 1, 2);
    controls->addWidget(m_positionBox, 1, 3);
    controls->addWidget(m_positionUnit, 1, 4);

    m_kindBox = new QComboBox(this);
    m_kindBox->addItem(tr("Bump"), int(SweepKind::Bump));
    m_kindBox->addItem(tr("Roll"), int(SweepKind::Roll));
    m_kindBox->addItem(tr("Steer"), int(SweepKind::Steer));
    m_kindBox->setToolTip(tr("Bump moves both wheels together, roll moves them opposite ways, "
                             "steer moves the rack at a fixed ride height."));
    controls->addWidget(new QLabel(tr("Sweep"), this), 2, 0);
    controls->addWidget(m_kindBox, 2, 1);

    m_fromBox = new QDoubleSpinBox(this);
    m_fromBox->setRange(-500.0, 500.0);
    m_fromBox->setDecimals(2);
    m_fromBox->setValue(-25.0);
    m_toBox = new QDoubleSpinBox(this);
    m_toBox->setRange(-500.0, 500.0);
    m_toBox->setDecimals(2);
    m_toBox->setValue(25.0);
    m_stepsBox = new QSpinBox(this);
    m_stepsBox->setRange(2, 501);
    m_stepsBox->setValue(41);
    m_stepsBox->setToolTip(tr("How many positions the sweep solves."));
    controls->addWidget(m_fromBox, 2, 2);
    controls->addWidget(m_toBox, 2, 3);
    controls->addWidget(m_stepsBox, 2, 4);

    m_rackBox = new QDoubleSpinBox(this);
    m_rackBox->setRange(-200.0, 200.0);
    m_rackBox->setDecimals(2);
    m_rackBox->setToolTip(tr("Rack travel held through a bump or roll sweep, so bump steer can be "
                             "looked at on a wheel that is already turned."));
    controls->addWidget(new QLabel(tr("Rack held"), this), 3, 0);
    controls->addWidget(m_rackBox, 3, 1);

    m_playButton = new QToolButton(this);
    m_playButton->setCheckable(true);
    m_playButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_playButton->setIcon(style()->standardIcon(QStyle::SP_MediaPlay));
    m_playButton->setText(tr("Play"));
    m_playButton->setToolTip(tr("Run the mechanism back and forth through the sweep so the "
                                "movement can be watched rather than scrubbed."));
    controls->addWidget(m_playButton, 3, 2);

    m_secondsBox = new QDoubleSpinBox(this);
    m_secondsBox->setRange(0.2, 60.0);
    m_secondsBox->setDecimals(1);
    m_secondsBox->setSingleStep(0.5);
    m_secondsBox->setValue(4.0);
    m_secondsBox->setSuffix(tr(" s/cycle"));
    m_secondsBox->setToolTip(tr("How long one there-and-back run of the travel takes."));
    controls->addWidget(m_secondsBox, 3, 3);

    m_allAxles = new QCheckBox(tr("All axles"), this);
    m_allAxles->setChecked(true);
    m_allAxles->setToolTip(tr("Move every axle together. The curve and the readout still belong "
                              "to the axle chosen above."));
    controls->addWidget(m_allAxles, 3, 4);

    m_exportButton = new QPushButton(tr("Export CSV..."), this);
    controls->addWidget(m_exportButton, 4, 3, 1, 2);

    controls->setColumnStretch(1, 1);
    controls->setColumnStretch(2, 1);
    layout->addLayout(controls);

    auto* curveRow = new QHBoxLayout;
    curveRow->addWidget(new QLabel(tr("Curve"), this));
    m_measureBox = new QComboBox(this);
    for (SweepMeasure measure : sweepMeasures())
        m_measureBox->addItem(QStringLiteral("%1 [%2]").arg(sweepMeasureLabel(measure),
                                                            sweepMeasureUnit(measure)),
                              sweepMeasureKey(measure));
    curveRow->addWidget(m_measureBox, 1);
    layout->addLayout(curveRow);

    m_plot = new PlotWidget(this);
    layout->addWidget(m_plot, 1);

    m_readout = new QTableWidget(int(readoutMeasures().size()), 2, this);
    m_readout->setHorizontalHeaderLabels({ tr("Left"), tr("Right") });
    QStringList rows;
    for (SweepMeasure measure : readoutMeasures())
        rows << QStringLiteral("%1 [%2]").arg(sweepMeasureLabel(measure),
                                              sweepMeasureUnit(measure));
    m_readout->setVerticalHeaderLabels(rows);
    m_readout->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_readout->verticalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_readout->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_readout->setSelectionMode(QAbstractItemView::NoSelection);
    m_readout->setAlternatingRowColors(true);
    for (int row = 0; row < m_readout->rowCount(); ++row)
        for (int column = 0; column < 2; ++column) {
            auto* item = new QTableWidgetItem;
            item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            m_readout->setItem(row, column, item);
        }
    layout->addWidget(m_readout);

    m_status = new QLabel(this);
    m_status->setWordWrap(true);
    m_status->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(m_status);

    // --- wiring ---------------------------------------------------------
    // Every one of these is guarded by m_updating, because the window sets the
    // panel from the project and must not have that read back as a change.

    connect(m_axleBox, &QComboBox::currentIndexChanged, this, [this] {
        if (!m_updating) emit axleChanged();
    });
    connect(m_simulate, &QCheckBox::toggled, this, [this](bool on) {
        // Turning the simulation off while it is running leaves the model at
        // design, which is the one place a paused animation should never be
        // left half way to.
        if (!on && m_playButton->isChecked()) m_playButton->setChecked(false);
        if (!m_updating) emit simulatingChanged(on);
    });
    connect(m_measureBox, &QComboBox::currentIndexChanged, this, [this] {
        m_plot->setMeasure(measure());
        if (!m_updating) emit measureChanged();
    });
    connect(m_exportButton, &QPushButton::clicked, this, &AnalysisPanel::exportCsvRequested);
    connect(m_allAxles, &QCheckBox::toggled, this, [this] {
        if (!m_updating) emit positionChanged(position());
    });

    m_animation = new QTimer(this);
    m_animation->setInterval(kAnimationIntervalMs);
    connect(m_animation, &QTimer::timeout, this, &AnalysisPanel::stepAnimation);
    connect(m_playButton, &QToolButton::toggled, this, [this](bool on) {
        m_playButton->setIcon(
            style()->standardIcon(on ? QStyle::SP_MediaPause : QStyle::SP_MediaPlay));
        m_playButton->setText(on ? tr("Pause") : tr("Play"));
        if (on) {
            // Watching a mechanism that is not being drawn posed would show
            // nothing at all, so pressing play turns the simulation on.
            if (!m_simulate->isChecked()) {
                m_simulate->setChecked(true);
                if (!m_updating) emit simulatingChanged(true);
            }
            seedAnimationPhase();
            m_animation->start();
        } else {
            m_animation->stop();
        }
        if (!m_updating) emit animatingChanged(on);
    });

    connect(m_positionSlider, &QSlider::valueChanged, this, [this](int value) {
        if (m_updating) return;
        emitPositionFromSlider(value);
    });
    connect(m_positionBox, &QDoubleSpinBox::valueChanged, this, [this](double value) {
        if (m_updating) return;
        m_updating = true;
        m_positionSlider->setValue(positionToSlider(value));
        m_updating = false;
        m_plot->setMarker(value);
        emit positionChanged(value);
    });
    connect(m_plot, &PlotWidget::markerMoved, this, [this](double value) {
        setPosition(value);
        emit positionChanged(value);
    });

    const auto specEdited = [this] {
        if (m_updating) return;
        m_kind = static_cast<SweepKind>(m_kindBox->currentData().toInt());
        syncPositionRange();
        emit specChanged();
    };
    connect(m_kindBox, &QComboBox::currentIndexChanged, this, specEdited);
    connect(m_fromBox, &QDoubleSpinBox::valueChanged, this, specEdited);
    connect(m_toBox, &QDoubleSpinBox::valueChanged, this, specEdited);
    connect(m_stepsBox, &QSpinBox::valueChanged, this, specEdited);
    connect(m_rackBox, &QDoubleSpinBox::valueChanged, this, specEdited);

    syncPositionRange();
}

void AnalysisPanel::setAxles(const QList<QPair<QString, QString>>& axles)
{
    const QString wanted = axle();
    m_updating = true;
    m_axleBox->clear();
    for (const auto& entry : axles) m_axleBox->addItem(entry.second, entry.first);
    const int index = m_axleBox->findData(wanted);
    if (index >= 0) m_axleBox->setCurrentIndex(index);
    m_updating = false;

    // Nothing to sweep is not an error state to explain, it is a panel with
    // nothing in it. The window says why in the status line.
    const bool usable = !axles.isEmpty();
    m_axleBox->setEnabled(usable);
    m_simulate->setEnabled(usable);
    m_positionSlider->setEnabled(usable);
    m_positionBox->setEnabled(usable);
    m_kindBox->setEnabled(usable);
    m_fromBox->setEnabled(usable);
    m_toBox->setEnabled(usable);
    m_stepsBox->setEnabled(usable);
    m_rackBox->setEnabled(usable);
    m_exportButton->setEnabled(usable);
    m_playButton->setEnabled(usable);
    m_secondsBox->setEnabled(usable);
    m_allAxles->setEnabled(usable);
    if (!usable) m_playButton->setChecked(false);
}

QString AnalysisPanel::axle() const { return m_axleBox->currentData().toString(); }

void AnalysisPanel::setAxle(const QString& token)
{
    const int index = m_axleBox->findData(token);
    if (index < 0) return;
    m_updating = true;
    m_axleBox->setCurrentIndex(index);
    m_updating = false;
}

SweepSpec AnalysisPanel::spec() const
{
    SweepSpec spec;
    spec.kind = static_cast<SweepKind>(m_kindBox->currentData().toInt());
    spec.from = m_fromBox->value();
    spec.to = m_toBox->value();
    spec.steps = m_stepsBox->value();
    spec.rackTravel = m_rackBox->value();
    return spec;
}

void AnalysisPanel::setSpec(const SweepSpec& spec)
{
    m_updating = true;
    const int kindIndex = m_kindBox->findData(int(spec.kind));
    if (kindIndex >= 0) m_kindBox->setCurrentIndex(kindIndex);
    m_kind = spec.kind;
    m_fromBox->setValue(spec.from);
    m_toBox->setValue(spec.to);
    m_stepsBox->setValue(spec.steps);
    m_rackBox->setValue(spec.rackTravel);
    m_updating = false;
    syncPositionRange();
}

double AnalysisPanel::position() const { return m_positionBox->value(); }

void AnalysisPanel::setPosition(double position)
{
    m_updating = true;
    m_positionBox->setValue(position);
    m_positionSlider->setValue(positionToSlider(m_positionBox->value()));
    m_updating = false;
    m_plot->setMarker(m_positionBox->value());
}

bool AnalysisPanel::simulating() const { return m_simulate->isChecked(); }

void AnalysisPanel::setSimulating(bool simulating)
{
    m_updating = true;
    m_simulate->setChecked(simulating);
    m_updating = false;
}

SweepMeasure AnalysisPanel::measure() const
{
    return sweepMeasureFromKey(m_measureBox->currentData().toString());
}

void AnalysisPanel::setMeasure(SweepMeasure measure)
{
    const int index = m_measureBox->findData(sweepMeasureKey(measure));
    if (index < 0) return;
    m_updating = true;
    m_measureBox->setCurrentIndex(index);
    m_updating = false;
    m_plot->setMeasure(measure);
}

bool AnalysisPanel::animating() const { return m_playButton->isChecked(); }

void AnalysisPanel::setAnimating(bool animating)
{
    if (m_playButton->isChecked() == animating) return;
    m_playButton->setChecked(animating);
}

bool AnalysisPanel::movesAllAxles() const { return m_allAxles->isChecked(); }

void AnalysisPanel::setMovesAllAxles(bool all)
{
    m_updating = true;
    m_allAxles->setChecked(all);
    m_updating = false;
}

void AnalysisPanel::seedAnimationPhase()
{
    const double low = m_positionBox->minimum();
    const double high = m_positionBox->maximum();
    if (high - low < 1e-9) {
        m_phase = 0.0;
        return;
    }
    // The cycle runs low -> high -> low, so the first half of it is the rising
    // branch and that is the one to start on.
    const double fraction = std::clamp((m_positionBox->value() - low) / (high - low), 0.0, 1.0);
    m_phase = fraction * 0.5;
}

void AnalysisPanel::stepAnimation()
{
    const double seconds = std::max(0.2, m_secondsBox->value());
    m_phase = std::fmod(m_phase + (kAnimationIntervalMs / 1000.0) / seconds, 1.0);

    // A triangle wave, so the mechanism runs to one end of its travel and back
    // rather than snapping from the top to the bottom every cycle.
    const double ramp = m_phase < 0.5 ? m_phase * 2.0 : 2.0 - m_phase * 2.0;
    const double low = m_positionBox->minimum();
    const double high = m_positionBox->maximum();
    const double position = low + (high - low) * ramp;

    m_updating = true;
    m_positionBox->setValue(position);
    m_positionSlider->setValue(positionToSlider(position));
    m_updating = false;
    m_plot->setMarker(position);
    emit positionChanged(position);
}

void AnalysisPanel::setResult(const SweepResult& result) { m_plot->setSweep(result); }

void AnalysisPanel::setReadout(const AxleSample& sample, SweepKind kind)
{
    Q_UNUSED(kind);
    const std::vector<SweepMeasure>& measures = readoutMeasures();
    for (std::size_t row = 0; row < measures.size(); ++row) {
        const SweepMeasure measure = measures[row];
        const bool perSide = sweepMeasureIsPerSide(measure);
        for (int column = 0; column < 2; ++column) {
            QTableWidgetItem* item = m_readout->item(int(row), column);
            if (!item) continue;
            // An axle-wide number is written once, in the left column, rather
            // than twice: a roll centre does not have a left and a right.
            if (!perSide && column == 1) {
                item->setText(QString());
                continue;
            }
            double value = 0.0;
            item->setText(sweepMeasureValue(sample, measure, column == 0, &value)
                              ? QString::number(value, 'f', 3)
                              : QStringLiteral("--"));
        }
    }
}

void AnalysisPanel::clearReadout()
{
    for (int row = 0; row < m_readout->rowCount(); ++row)
        for (int column = 0; column < 2; ++column)
            if (QTableWidgetItem* item = m_readout->item(row, column)) item->setText(QString());
}

void AnalysisPanel::setStatus(const QString& text) { m_status->setText(text); }

void AnalysisPanel::syncPositionRange()
{
    const SweepSpec current = spec();
    const double low = std::min(current.from, current.to);
    const double high = std::max(current.from, current.to);

    m_updating = true;
    m_positionUnit->setText(sweepInputUnit(current.kind));
    m_positionBox->setSuffix(QString());
    m_positionBox->setRange(low, high);
    m_positionBox->setSingleStep(current.kind == SweepKind::Roll ? 0.1 : 1.0);
    m_positionSlider->setValue(positionToSlider(m_positionBox->value()));
    m_updating = false;
    m_plot->setMarker(m_positionBox->value());
}

double AnalysisPanel::sliderToPosition(int value) const
{
    const double low = m_positionBox->minimum();
    const double high = m_positionBox->maximum();
    return low + (high - low) * (double(value) / double(kSliderSteps));
}

int AnalysisPanel::positionToSlider(double position) const
{
    const double low = m_positionBox->minimum();
    const double high = m_positionBox->maximum();
    if (high - low < 1e-9) return 0;
    const double t = (position - low) / (high - low);
    return int(std::lround(std::clamp(t, 0.0, 1.0) * kSliderSteps));
}

void AnalysisPanel::emitPositionFromSlider(int value)
{
    const double position = sliderToPosition(value);
    m_updating = true;
    m_positionBox->setValue(position);
    m_updating = false;
    m_plot->setMarker(position);
    emit positionChanged(position);
}

} // namespace suspkin
