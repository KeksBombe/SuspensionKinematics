#pragma once

#include "model/Sweep.h"

#include <QWidget>

namespace suspkin {

/// One measure of a sweep, drawn against the sweep's input.
///
/// Painted with QPainter rather than pulled in from Qt Charts: charts is a whole
/// extra Qt module to find, build and deploy on both platforms, and what is
/// needed here is two polylines, an axis pair and a marker.
///
/// Its colours come from the palette, the way the hardpoint table's do, so it
/// reads the same on a light desktop and a dark one.
class PlotWidget : public QWidget {
    Q_OBJECT

public:
    explicit PlotWidget(QWidget* parent = nullptr);

    void setSweep(const SweepResult& result);
    void setMeasure(SweepMeasure measure);
    SweepMeasure measure() const { return m_measure; }

    /// Which wheels to draw, for a measure that has one curve per wheel. A
    /// measure of the whole axle -- the roll centre, Ackermann -- is one curve
    /// whatever this says.
    void setSides(SweepSides sides);
    SweepSides sides() const { return m_sides; }

    /// Where the model is standing right now, drawn as a vertical line so the
    /// viewport and the curve always agree about where you are.
    void setMarker(double input);

    /// Where the pointer is over another plot of the same sweep, so every plot
    /// on screen reads out the same position at once. @p hovering false puts
    /// the readout back on the marker.
    void setHover(double input, bool hovering);

    QSize minimumSizeHint() const override;
    QSize sizeHint() const override;

signals:
    /// The user clicked or dragged in the plot: they want to be at this input.
    void markerMoved(double input);
    /// The pointer moved over the plot, or left it.
    void hoverMoved(double input, bool hovering);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    /// One side's worth of points, already in data coordinates.
    struct Series {
        QList<QPointF> points;
        QColor color;
        QString label;
        bool left = true; ///< which side's value the readout asks for
    };

    void rebuild();
    /// The plotting area, inside the margins the labels need.
    QRectF plotArea() const;
    QPointF toWidget(const QRectF& area, const QPointF& value) const;
    double fromWidgetX(const QRectF& area, double x) const;
    void paintGrid(QPainter& painter, const QRectF& area) const;
    void paintSeries(QPainter& painter, const QRectF& area) const;
    void paintMarkerAndReadout(QPainter& painter, const QRectF& area) const;

    SweepResult m_result;
    SweepMeasure m_measure = SweepMeasure::Camber;
    SweepSides m_sides = SweepSides::Both;
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
