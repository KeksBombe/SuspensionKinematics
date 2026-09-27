#pragma once

#include "model/Sweep.h"

#include <QHash>
#include <QList>
#include <QPair>
#include <QString>
#include <QWidget>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QHBoxLayout;
class QLabel;
class QPushButton;
class QSlider;
class QTableWidget;
class QTimer;
class QToolButton;

namespace suspkin {

class PlotWidget;
class SweepParametersDialog;

/// One axle the dock can sweep.
///
/// It carries whether the axle is steered because that is not a property of the
/// sweep or of the panel -- it is the project's linkage template saying whether
/// this axle has a rack -- and the panel cannot ask anybody: it holds no solver
/// and no project on purpose.
struct AxleEntry {
    QString token;
    QString label;
    bool steered = false;
};

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

    /// The axles that can be simulated. An empty list disables the panel: there
    /// is nothing to sweep.
    void setAxles(const QList<AxleEntry>& axles);
    /// The axle the position drives: the one the viewport moves when not every
    /// axle comes along, and the one the readout table shows. Which axles are
    /// *plotted* is @ref plotAxles.
    QString axle() const;
    void setAxle(const QString& token);

    /// The axles whose curves are overlaid in the plot, by token, in the order
    /// the axles are listed. Never empty while there is an axle: the last one
    /// cannot be hidden, because an empty plot reads as broken, not as a choice.
    QStringList plotAxles() const;
    /// Show these axles. An empty list, or one naming no axle there is, leaves
    /// every axle shown.
    void setPlotAxles(const QStringList& tokens);
    /// The plotted axles the current sweep can be run on: all of them, except
    /// in a steer sweep, where an axle without a rack has no curve to give.
    /// What the window runs a sweep for.
    QStringList sweptAxles() const;

    /// What the sweep is run with: the travel and increment of all three kinds
    /// at once, edited in the parameters window rather than here.
    SweepSettings settings() const;
    void setSettings(const SweepSettings& settings);

    /// Which of the three is being swept.
    SweepKind kind() const;
    void setKind(SweepKind kind);

    /// The two together: the range and step count the solver is actually given.
    SweepSpec spec() const;

    /// How long one there-and-back run of the travel takes.
    double animationSeconds() const;
    void setAnimationSeconds(double seconds);

    /// Whether the parameters window is open. It is project state like every
    /// other thing the user arranges, so it is asked about and restored.
    bool parametersVisible() const;
    void setParametersVisible(bool visible);

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

    /// The one curve on screen.
    SweepMeasure measure() const { return m_measure; }
    void setMeasure(SweepMeasure measure);

    /// Which wheels the plot draws, and which column the readout keeps: both,
    /// or one of them for a car whose two sides mirror each other anyway.
    SweepSides sides() const { return m_sides; }
    void setSides(SweepSides sides);

    /// The sweeps the plot draws its curves from, one per axle. Any of an axle
    /// that is not shown is ignored.
    void setResults(const std::vector<SweepResult>& results);
    /// The numbers at the position the model is actually standing in, which is
    /// not always one of the sweep's own steps.
    void setReadout(const AxleSample& sample, SweepKind kind);
    void clearReadout();

    void setStatus(const QString& text);

public slots:
    /// Open the parameters window and bring it to the front.
    void showParameters();
    /// Close it, the way its own Close button would.
    void hideParameters();

signals:
    void axleChanged();
    /// The sweep's shape changed and wants running again.
    void specChanged();
    /// The model should move to this position, in the sweep's units.
    void positionChanged(double position);
    void simulatingChanged(bool simulating);
    void animatingChanged(bool animating);
    void measureChanged();
    /// Axles were shown or hidden: the ones shown want sweeping.
    void plotAxlesChanged();
    void sidesChanged();
    /// Something that is remembered but does not change the curve: how fast the
    /// animation runs, whether the parameters window is open.
    void playbackChanged();
    /// The parameters window opened or closed, by any route -- this panel's
    /// button, the window's own Close, Escape, a restored project. What the
    /// window's toggle for it follows.
    void parametersVisibilityChanged(bool visible);
    void exportCsvRequested();

private:
    void buildUi();
    /// The row that says what the plot shows: which curve, which axles, which
    /// wheels.
    QHBoxLayout* buildShowRow();
    /// One checkbox per axle, in the order the axles are listed.
    void rebuildAxleToggles();
    /// A shown axle was ticked or unticked by the user.
    void toggleAxle(const QString& token, bool shown);
    /// A wheel was ticked or unticked by the user.
    void toggleSide();
    /// The axle checkboxes from @ref m_plotAxles, and which of them the current
    /// sweep can use.
    void syncAxleToggles();
    /// The side checkboxes from @ref m_sides.
    void syncSideToggles();
    /// Hand the plot the sweeps of the axles it is showing.
    void syncPlotSweeps();
    /// Whether the current kind of sweep can be run on this axle.
    bool canSweep(const QString& token) const;
    int axleIndex(const QString& token) const;
    void setPlotMarker(double input);
    /// Hide the readout column of a side the plot is not showing.
    void syncReadoutColumns();
    /// Offer Steer only for an axle that has a rack, and step off it when the
    /// selected axle has none.
    void syncSteerAvailability();
    void syncPositionRange();
    /// The part of @p low to @p high every axle swept assembles in, from the
    /// last sweeps. All of it when they have nothing to say.
    SweepInterval reachableRange(double low, double high) const;
    void emitPositionFromSlider(int value);
    double sliderToPosition(int value) const;
    int positionToSlider(double position) const;
    /// One frame of the animation: advance the phase and move to where it says.
    void stepAnimation();
    /// Start the phase where the model is standing, so pressing play does not
    /// jump it back to the end of the travel first.
    void seedAnimationPhase();

    QComboBox* m_axleBox = nullptr;
    QList<AxleEntry> m_axles;
    /// Which axles have a steering rack, by token. Not every axle does, and an
    /// axle that does not cannot be asked for a steer sweep at all.
    QHash<QString, bool> m_steerable;
    QCheckBox* m_simulate = nullptr;
    QSlider* m_positionSlider = nullptr;
    QDoubleSpinBox* m_positionBox = nullptr;
    QLabel* m_positionUnit = nullptr;
    QComboBox* m_kindBox = nullptr;
    QComboBox* m_curveBox = nullptr;
    /// Where the axle checkboxes go, rebuilt whenever the axles change.
    QHBoxLayout* m_axleToggleRow = nullptr;
    QList<QCheckBox*> m_axleToggles;
    QCheckBox* m_leftToggle = nullptr;
    QCheckBox* m_rightToggle = nullptr;
    QToolButton* m_playButton = nullptr;
    QPushButton* m_parametersButton = nullptr;
    /// The travel, the increments and the playback settings, in a window of
    /// their own. Owned here rather than by the main window because this is
    /// what asks the sweep for its numbers.
    SweepParametersDialog* m_parameters = nullptr;
    QTimer* m_animation = nullptr;
    /// Where the animation is in its there-and-back cycle, in [0, 1).
    double m_phase = 0.0;
    QPushButton* m_exportButton = nullptr;
    QLabel* m_status = nullptr;
    QTableWidget* m_readout = nullptr;

    PlotWidget* m_plot = nullptr;
    SweepMeasure m_measure = SweepMeasure::Camber;
    SweepSides m_sides = SweepSides::Both;
    /// The axles the user asked to see, by token. Kept as asked even while an
    /// axle is missing from the table, so it comes back shown when it returns.
    QStringList m_plotAxles;
    /// Kept so that an axle shown again gets its curve back at once, and one
    /// hidden loses it at once, without waiting for the window to sweep.
    std::vector<SweepResult> m_results;

    /// Set while the panel is being told what to show, so echoing it straight
    /// back out does not look like the user having done something.
    bool m_updating = false;
};

} // namespace suspkin
