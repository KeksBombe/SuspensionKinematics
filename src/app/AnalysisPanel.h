#pragma once

#include "model/Sweep.h"

#include <QList>
#include <QPair>
#include <QString>
#include <QWidget>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QSlider;
class QSpinBox;
class QTableWidget;
class QTimer;
class QToolButton;

namespace suspkin {

class PlotWidget;

/// The analysis dock: where the suspension is put through its travel, and what
/// comes out when it is.
///
/// It holds no solver and no project. Everything it knows arrives through
/// @ref setResult and @ref setReadout, and everything the user does leaves as a
/// signal -- so the window stays the only thing that knows how to solve, and
/// this stays testable by looking at it.
class AnalysisPanel : public QWidget {
    Q_OBJECT

public:
    explicit AnalysisPanel(QWidget* parent = nullptr);

    /// The axles that can be simulated, as token and label. An empty list
    /// disables the panel: there is nothing to sweep.
    void setAxles(const QList<QPair<QString, QString>>& axles);
    QString axle() const;
    void setAxle(const QString& token);

    SweepSpec spec() const;
    void setSpec(const SweepSpec& spec);

    /// Where the model is standing, in the sweep's own units.
    double position() const;
    void setPosition(double position);

    bool simulating() const;
    void setSimulating(bool simulating);

    /// Whether the mechanism is running through its travel on its own.
    ///
    /// Worth asking about from outside for one reason: a position that is moving
    /// thirty times a second is not a position worth writing to the project on
    /// every frame.
    bool animating() const;
    void setAnimating(bool animating);

    /// Whether every axle moves, or only the one the curve belongs to. A car
    /// that heaves on its front wheels alone does not read as a car.
    bool movesAllAxles() const;
    void setMovesAllAxles(bool all);

    SweepMeasure measure() const;
    void setMeasure(SweepMeasure measure);

    /// The curve.
    void setResult(const SweepResult& result);
    /// The numbers at the position the model is actually standing in, which is
    /// not always one of the sweep's own steps.
    void setReadout(const AxleSample& sample, SweepKind kind);
    void clearReadout();

    void setStatus(const QString& text);

signals:
    void axleChanged();
    /// The sweep's shape changed and wants running again.
    void specChanged();
    /// The model should move to this position, in the sweep's units.
    void positionChanged(double position);
    void simulatingChanged(bool simulating);
    void animatingChanged(bool animating);
    void measureChanged();
    void exportCsvRequested();

private:
    void buildUi();
    void syncPositionRange();
    void emitPositionFromSlider(int value);
    double sliderToPosition(int value) const;
    int positionToSlider(double position) const;
    /// One frame of the animation: advance the phase and move to where it says.
    void stepAnimation();
    /// Start the phase where the model is standing, so pressing play does not
    /// jump it back to the end of the travel first.
    void seedAnimationPhase();

    QComboBox* m_axleBox = nullptr;
    QCheckBox* m_simulate = nullptr;
    QSlider* m_positionSlider = nullptr;
    QDoubleSpinBox* m_positionBox = nullptr;
    QLabel* m_positionUnit = nullptr;
    QComboBox* m_kindBox = nullptr;
    QDoubleSpinBox* m_fromBox = nullptr;
    QDoubleSpinBox* m_toBox = nullptr;
    QSpinBox* m_stepsBox = nullptr;
    QDoubleSpinBox* m_rackBox = nullptr;
    QComboBox* m_measureBox = nullptr;
    QToolButton* m_playButton = nullptr;
    QDoubleSpinBox* m_secondsBox = nullptr;
    QCheckBox* m_allAxles = nullptr;
    QTimer* m_animation = nullptr;
    /// Where the animation is in its there-and-back cycle, in [0, 1).
    double m_phase = 0.0;
    QPushButton* m_exportButton = nullptr;
    QLabel* m_status = nullptr;
    QTableWidget* m_readout = nullptr;
    PlotWidget* m_plot = nullptr;

    SweepKind m_kind = SweepKind::Bump;
    /// Set while the panel is being told what to show, so echoing it straight
    /// back out does not look like the user having done something.
    bool m_updating = false;
};

} // namespace suspkin
