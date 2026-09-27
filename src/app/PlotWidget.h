#pragma once

#include "model/Sweep.h"

#include <QWidget>

namespace suspkin {

/// One axle's sweep, the way a plot is handed it.
struct PlotSweep {
    SweepResult result;
    /// The axle's place among every axle the car has, not among the ones on
    /// screen: the front stays the front's colour when the rear is hidden.
    int colorIndex = 0;
};

/// One measure of one or more axles' sweeps, drawn against the sweep's input.
///
/// Every axle it is given is overlaid in the one plot, the way Lotus draws a
/// front and a rear: a colour per axle, a solid line for the left wheel and a
/// dashed one for the right.
///
/// Painted with QPainter rather than pulled in from Qt Charts: charts is a whole
/// extra Qt module to find, build and deploy on both platforms, and what is
/// needed here is a few polylines, an axis pair and a marker.
///
/// Its colours come from the palette, the way the hardpoint table's do, so it
/// reads the same on a light desktop and a dark one.
class PlotWidget : public QWidget {
    Q_OBJECT

public:
    explicit PlotWidget(QWidget* parent = nullptr);

    /// The axles to draw, in the order their curves are listed in the readout.
    void setSweeps(const QList<PlotSweep>& sweeps);
    void setMeasure(SweepMeasure measure);
    SweepMeasure measure() const { return m_measure; }

    /// Which wheels to draw, for a measure that has one curve per wheel. A
    /// measure of the whole axle -- the roll centre, Ackermann -- is one curve
    /// per axle whatever this says.
    void setSides(SweepSides sides);
    SweepSides sides() const { return m_sides; }

    /// What the plot says when it has no sweep at all to draw.
    void setPlaceholder(const QString& text);

    /// Where the model is standing right now, drawn as a vertical line so the
    /// viewport and the curve always agree about where you are.
    void setMarker(double input);

    QSize minimumSizeHint() const override;
    QSize sizeHint() const override;

    /// The colour an axle's curves are drawn in, by @ref PlotSweep::colorIndex.
    static QColor axleColor(int colorIndex);

signals:
    /// The user clicked or dragged in the plot: they want to be at this input.
    void markerMoved(double input);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    /// One wheel's worth of points, or one axle's for an axle-wide measure,
    /// already in data coordinates.
    struct Series {
        QList<QPointF> points;
        QColor color;
        Qt::PenStyle style = Qt::SolidLine;
        QString label;
        int sweep = 0;     ///< which of @ref m_sweeps it is read from
        bool left = true;  ///< which side's value the readout asks for
    };

    void rebuild();
    /// The curves one axle's sweep contributes: a wheel each, or one for the axle.
    QList<Series> seriesOf(int sweepIndex) const;
    /// Fit the axes around every point of @ref m_series.
    void fitRange();
    /// The first sweep that has samples, which is what the axis and the
    /// readout's input line are taken from. Null when none has.
    const SweepResult* leadingResult() const;
    /// Why there is nothing to draw, for a plot with no curve in it.
    QString emptyText() const;

    /// The plotting area, inside the margins the labels need.
    QRectF plotArea() const;
    QPointF toWidget(const QRectF& area, const QPointF& value) const;
    double fromWidgetX(const QRectF& area, double x) const;
    void paintGrid(QPainter& painter, const QRectF& area) const;
    void paintSeries(QPainter& painter, const QRectF& area) const;
    void paintMarkerAndReadout(QPainter& painter, const QRectF& area) const;

    QList<PlotSweep> m_sweeps;
    SweepMeasure m_measure = SweepMeasure::Camber;
    SweepSides m_sides = SweepSides::Both;
    QString m_placeholder;
    QList<Series> m_series;

    double m_xMin = -1.0;
    double m_xMax = 1.0;
    double m_yMin = -1.0;
    double m_yMax = 1.0;
    bool m_hasData = false;

    double m_marker = 0.0;
    double m_hover = 0.0;
    bool m_hovering = false;
};

} // namespace suspkin
