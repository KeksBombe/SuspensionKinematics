#pragma once

#include "model/Sweep.h"

#include <QWidget>

namespace suspkin {

/// One measure of a sweep, drawn against the sweep's input.
///
/// Painted with QPainter rather than pulled in from Qt Charts: charts is a whole
/// extra Qt module to find, build and deploy on both platforms, and what is
/// needed here is two polylines, an axis pair and a marker.
class PlotWidget : public QWidget {
    Q_OBJECT

public:
    explicit PlotWidget(QWidget* parent = nullptr);

    void setSweep(const SweepResult& result);
    void setMeasure(SweepMeasure measure);
    SweepMeasure measure() const { return m_measure; }

    /// Where the model is standing right now, drawn as a vertical line so the
    /// viewport and the curve always agree about where you are.
    void setMarker(double input);

    QSize minimumSizeHint() const override;
    QSize sizeHint() const override;

signals:
    /// The user clicked or dragged in the plot: they want to be at this input.
    void markerMoved(double input);

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
