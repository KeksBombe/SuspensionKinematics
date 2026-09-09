#include "app/AnalysisPanel.h"

#include "app/PlotWidget.h"
#include "app/SweepParametersDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QStandardItemModel>
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
                             "steer moves the rack at a fixed ride height. Each keeps its own "
                             "travel and increment, set in the parameters window."));
    controls->addWidget(new QLabel(tr("Sweep"), this), 2, 0);
    controls->addWidget(m_kindBox, 2, 1);

    m_playButton = new QToolButton(this);
    m_playButton->setCheckable(true);
    m_playButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_playButton->setIcon(style()->standardIcon(QStyle::SP_MediaPlay));
    m_playButton->setText(tr("Play"));
    m_playButton->setToolTip(tr("Run the mechanism back and forth through the sweep so the "
                                "movement can be watched rather than scrubbed."));
    controls->addWidget(m_playButton, 2, 2);

    // The travel, the increments and the playback settings live in a window of
    // their own: three sweeps' worth of numbers do not fit beside a plot, and
    // they are set once and left rather than reached for every minute.
    m_parameters = new SweepParametersDialog(this);
    m_parametersButton = new QPushButton(tr("Parameters..."), this);
    m_parametersButton->setToolTip(tr("How far each sweep travels and how finely it is solved, "
                                      "in a window that can be left open beside the viewport."));
    controls->addWidget(m_parametersButton, 2, 3, 1, 2);

    m_exportButton = new QPushButton(tr("Export CSV..."), this);
    controls->addWidget(m_exportButton, 3, 3, 1, 2);

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
        syncSteerAvailability();
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
    connect(m_parametersButton, &QPushButton::clicked, this, &AnalysisPanel::showParameters);

    // The parameters window is live: what it changes is applied as it is typed,
    // which is the whole reason it is worth leaving open.
    connect(m_parameters, &SweepParametersDialog::settingsChanged, this, [this] {
        syncPositionRange();
        if (!m_updating) emit specChanged();
    });
    connect(m_parameters, &SweepParametersDialog::movesAllAxlesChanged, this, [this] {
        if (!m_updating) emit positionChanged(position());
    });
    connect(m_parameters, &SweepParametersDialog::animationSecondsChanged, this, [this] {
        if (!m_updating) emit playbackChanged();
    });
    connect(m_parameters, &SweepParametersDialog::closedByUser, this, [this] {
        if (!m_updating) emit playbackChanged();
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

    connect(m_kindBox, &QComboBox::currentIndexChanged, this, [this] {
        m_parameters->setKind(kind());
        syncPositionRange();
        if (!m_updating) emit specChanged();
    });

    syncPositionRange();
}

void AnalysisPanel::setAxles(const QList<AxleEntry>& axles)
{
    const QString wanted = axle();
    m_updating = true;
    m_axleBox->clear();
    m_steerable.clear();
    for (const AxleEntry& entry : axles) {
        m_axleBox->addItem(entry.label, entry.token);
        m_steerable.insert(entry.token, entry.steered);
    }
    const int index = m_axleBox->findData(wanted);
    if (index >= 0) m_axleBox->setCurrentIndex(index);
    m_updating = false;
    syncSteerAvailability();

    // Nothing to sweep is not an error state to explain, it is a panel with
    // nothing in it. The window says why in the status line.
    const bool usable = !axles.isEmpty();
    m_axleBox->setEnabled(usable);
    m_simulate->setEnabled(usable);
    m_positionSlider->setEnabled(usable);
    m_positionBox->setEnabled(usable);
    m_kindBox->setEnabled(usable);
    m_exportButton->setEnabled(usable);
    m_playButton->setEnabled(usable);
    m_parametersButton->setEnabled(usable);
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
    // Restoring a project happens in this order: the sweep kind first, then the
    // axle. A project that was left on a steer sweep of an axle with no rack --
    // which is exactly what the old behaviour let people do -- has to land
    // somewhere sensible rather than steering a suspension that cannot.
    syncSteerAvailability();
}

void AnalysisPanel::syncSteerAvailability()
{
    const bool steered = m_steerable.value(axle(), true);

    // Greyed out rather than removed: the entry is still the answer to "why can
    // I not steer this?", and a combo whose items move about under the cursor
    // is worse than one with a disabled row in it.
    if (auto* model = qobject_cast<QStandardItemModel*>(m_kindBox->model())) {
        const int row = m_kindBox->findData(int(SweepKind::Steer));
        if (QStandardItem* item = row >= 0 ? model->item(row) : nullptr) {
            item->setEnabled(steered);
            item->setToolTip(steered ? QString()
                                     : tr("This axle has no steering: the linkage template names "
                                          "no hardpoint for a rack to drive. Parts > Steering "
                                          "names one."));
        }
    }
    m_parameters->setSteeringAvailable(steered);

    if (steered || kind() != SweepKind::Steer) return;

    // Standing on a sweep this axle cannot do. Bump is the one every suspension
    // can. setKind() guards the signal itself; what it does not do is restore
    // whatever guard we were already inside, so that is put back by hand.
    const bool wasUpdating = m_updating;
    setKind(SweepKind::Bump);
    m_parameters->setKind(SweepKind::Bump);
    syncPositionRange();
    m_updating = wasUpdating;
    if (!wasUpdating) emit specChanged();
}

SweepSettings AnalysisPanel::settings() const { return m_parameters->settings(); }

void AnalysisPanel::setSettings(const SweepSettings& settings)
{
    m_updating = true;
    m_parameters->setSettings(settings);
    m_updating = false;
    syncPositionRange();
}

SweepKind AnalysisPanel::kind() const
{
    return static_cast<SweepKind>(m_kindBox->currentData().toInt());
}

void AnalysisPanel::setKind(SweepKind kind)
{
    const int index = m_kindBox->findData(int(kind));
    if (index < 0) return;
    m_updating = true;
    m_kindBox->setCurrentIndex(index);
    m_updating = false;
    m_parameters->setKind(kind);
    syncPositionRange();
}

SweepSpec AnalysisPanel::spec() const { return settings().specFor(kind()); }

double AnalysisPanel::animationSeconds() const { return m_parameters->animationSeconds(); }

void AnalysisPanel::setAnimationSeconds(double seconds)
{
    m_updating = true;
    m_parameters->setAnimationSeconds(seconds);
    m_updating = false;
}

bool AnalysisPanel::parametersVisible() const { return m_parameters->isVisible(); }

void AnalysisPanel::setParametersVisible(bool visible)
{
    if (m_parameters->isVisible() == visible) return;
    m_updating = true;
    if (visible)
        m_parameters->show();
    else
        m_parameters->close();
    m_updating = false;
}

void AnalysisPanel::showParameters()
{
    const bool wasVisible = m_parameters->isVisible();
    m_parameters->show();
    m_parameters->raise();
    m_parameters->activateWindow();
    if (!wasVisible && !m_updating) emit playbackChanged();
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

bool AnalysisPanel::movesAllAxles() const { return m_parameters->movesAllAxles(); }

void AnalysisPanel::setMovesAllAxles(bool all)
{
    m_updating = true;
    m_parameters->setMovesAllAxles(all);
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
    const double seconds = std::max(0.2, m_parameters->animationSeconds());
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
    const double previous = m_positionBox->value();

    m_updating = true;
    m_positionUnit->setText(sweepInputUnit(current.kind));
    m_positionBox->setSuffix(QString());
    m_positionBox->setRange(low, high);
    // A position that means nothing in the new range goes back to the design
    // position rather than to whichever end of the travel it happened to be
    // nearest. Twenty millimetres of wheel travel is not twenty degrees of body
    // roll, and it is not full lock either.
    if (previous < low || previous > high) m_positionBox->setValue(std::clamp(0.0, low, high));
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
