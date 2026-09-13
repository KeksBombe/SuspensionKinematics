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
class QGridLayout;
class QLabel;
class QMenu;
class QPushButton;
class QScrollArea;
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
    QString axle() const;
    void setAxle(const QString& token);

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

    /// The curves on screen, one plot each, in the order the curve menu lists
    /// them. Never empty: the last one cannot be switched off, because a panel
    /// with no plot in it reads as broken rather than as a choice.
    QList<SweepMeasure> measures() const { return m_measures; }
    void setMeasures(const QList<SweepMeasure>& measures);

    /// Which wheels the plots draw, and which column the readout keeps: both,
    /// or one of them for a car whose two sides mirror each other anyway.
    SweepSides sides() const { return m_sides; }
    void setSides(SweepSides sides);

    /// The sweep every plot draws its curve from.
    void setResult(const SweepResult& result);
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
    void measuresChanged();
    void sidesChanged();
    /// Something that is remembered but does not change the curve: how fast the
    /// animation runs, whether the parameters window is open.
    void playbackChanged();
    /// The parameters window opened or closed, by any route -- this panel's
    /// button, the window's own Close, Escape, a restored project. What the
    /// window's toggle for it follows.
    void parametersVisibilityChanged(bool visible);
    void exportCsvRequested();

protected:
    /// Watches the plots' viewport, so they reflow into more or fewer columns as
    /// the dock is resized.
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void buildUi();
    /// One plot per curve in @ref m_measures: the ones still wanted are kept,
    /// the rest removed, any new ones made. Then laid out again.
    void syncPlots();
    PlotWidget* makePlot(SweepMeasure measure);
    /// Put the plots in a grid as many columns wide as the viewport has room
    /// for. Unless @p force, only when that number of columns has changed.
    void layOutPlots(bool force);
    /// The curve button's text and the ticks in its menu, from @ref m_measures.
    void syncCurveMenu();
    void setPlotMarker(double input);
    /// Hide the readout column of a side the plots are not showing.
    void syncReadoutColumns();
    /// Offer Steer only for an axle that has a rack, and step off it when the
    /// selected axle has none.
    void syncSteerAvailability();
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
    /// Which axles have a steering rack, by token. Not every axle does, and an
    /// axle that does not cannot be asked for a steer sweep at all.
    QHash<QString, bool> m_steerable;
    QCheckBox* m_simulate = nullptr;
    QSlider* m_positionSlider = nullptr;
    QDoubleSpinBox* m_positionBox = nullptr;
    QLabel* m_positionUnit = nullptr;
    QComboBox* m_kindBox = nullptr;
    /// Which curves are plotted: a button whose menu has one tick per measure.
    QToolButton* m_curveButton = nullptr;
    QMenu* m_curveMenu = nullptr;
    QComboBox* m_sidesBox = nullptr;
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

    /// The plots, one per curve, in a scrolling grid: as many as are wanted fit
    /// side by side when the dock is wide, and stack and scroll when it is not.
    QScrollArea* m_plotScroll = nullptr;
    QWidget* m_plotHost = nullptr;
    QGridLayout* m_plotGrid = nullptr;
    QList<PlotWidget*> m_plots;
    QList<SweepMeasure> m_measures{ SweepMeasure::Camber };
    SweepSides m_sides = SweepSides::Both;
    int m_plotColumns = 0;
    /// Kept so that a plot made after the sweep arrived still gets its curve.
    SweepResult m_result;

    /// Set while the panel is being told what to show, so echoing it straight
    /// back out does not look like the user having done something.
    bool m_updating = false;
};

} // namespace suspkin
