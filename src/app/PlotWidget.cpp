#include "app/PlotWidget.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>

namespace suspkin {
namespace {

/// Room for the tick labels and the axis titles. Fixed rather than measured:
/// the labels are short numbers and a fixed margin keeps the axes from twitching
/// as the curve is scrolled.
constexpr int kLeftMargin = 62;
constexpr int kRightMargin = 14;
constexpr int kTopMargin = 26;
constexpr int kBottomMargin = 40;

const QColor kLeftColor(58, 124, 216);
const QColor kRightColor(214, 106, 42);
const QColor kAxisColor(120, 120, 128);
const QColor kGridColor(214, 214, 220);
const QColor kMarkerColor(150, 150, 158);

/// A tick step that lands on a 1, a 2 or a 5, which are the only spacings a
/// person reads without having to think about it.
double niceStep(double span, int target)
{
    if (span <= 0.0 || target < 1) return 1.0;
    const double rough = span / target;
    const double magnitude = std::pow(10.0, std::floor(std::log10(rough)));
    const double normalized = rough / magnitude;
    if (normalized < 1.5) return magnitude;
    if (normalized < 3.5) return 2.0 * magnitude;
    if (normalized < 7.5) return 5.0 * magnitude;
    return 10.0 * magnitude;
}

/// Tick labels get as many decimals as the step needs and no more, so a 0.5 step
/// reads "1.5" and a 5 step reads "10".
QString tickText(double value, double step)
{
    int decimals = 0;
    if (step < 0.05) decimals = 3;
    else if (step < 0.5) decimals = 2;
    else if (step < 5.0) decimals = 1;
    // Keeps a tick that lands a hair below zero from printing as "-0".
    if (std::abs(value) < step * 1e-6) value = 0.0;
    return QString::number(value, 'f', decimals);
}

} // namespace

PlotWidget::PlotWidget(QWidget* parent) : QWidget(parent)
{
    setMouseTracking(true);
    setAutoFillBackground(true);
    setBackgroundRole(QPalette::Base);
}

QSize PlotWidget::minimumSizeHint() const { return QSize(240, 160); }
QSize PlotWidget::sizeHint() const { return QSize(420, 260); }

void PlotWidget::setSweep(const SweepResult& result)
{
    m_result = result;
    rebuild();
    update();
}

void PlotWidget::setMeasure(SweepMeasure measure)
{
    if (m_measure == measure) return;
    m_measure = measure;
    rebuild();
    update();
}

void PlotWidget::setMarker(double input)
{
    if (qFuzzyCompare(m_marker + 1.0, input + 1.0)) return;
    m_marker = input;
    update();
}

void PlotWidget::rebuild()
{
    m_series.clear();
    m_hasData = false;
    if (m_result.isEmpty()) return;

    const bool perSide = sweepMeasureIsPerSide(m_measure);
    struct SideSpec {
        bool left;
        QColor color;
        QString label;
    };
    QList<SideSpec> sides;
    if (perSide) {
        sides.append({ true, kLeftColor, tr("left") });
        sides.append({ false, kRightColor, tr("right") });
    } else {
        sides.append({ true, kLeftColor, sweepMeasureLabel(m_measure) });
    }

    double xMin = 0.0;
    double xMax = 0.0;
    double yMin = 0.0;
    double yMax = 0.0;
    bool first = true;

    for (const SideSpec& side : sides) {
        Series series;
        series.color = side.color;
        series.label = side.label;
        for (const AxleSample& sample : m_result.samples) {
            double value = 0.0;
            if (!sweepMeasureValue(sample, m_measure, side.left, &value)) continue;
            series.points.append(QPointF(sample.input, value));
            if (first) {
                xMin = xMax = sample.input;
                yMin = yMax = value;
                first = false;
            } else {
                xMin = std::min(xMin, sample.input);
                xMax = std::max(xMax, sample.input);
                yMin = std::min(yMin, value);
                yMax = std::max(yMax, value);
            }
        }
        if (!series.points.isEmpty()) m_series.append(series);
    }

    if (m_series.isEmpty()) return;

    // A flat curve still needs a band to be drawn in, or it lands on the axis
    // and reads as no data at all.
    if (xMax - xMin < 1e-9) {
        xMin -= 1.0;
        xMax += 1.0;
    }
    const double ySpan = yMax - yMin;
    if (ySpan < 1e-9) {
        const double pad = std::max(0.5, std::abs(yMax) * 0.1);
        yMin -= pad;
        yMax += pad;
    } else {
        yMin -= ySpan * 0.08;
        yMax += ySpan * 0.08;
    }

    m_xMin = xMin;
    m_xMax = xMax;
    m_yMin = yMin;
    m_yMax = yMax;
    m_hasData = true;
}

QRectF PlotWidget::plotArea() const
{
    return QRectF(rect()).adjusted(kLeftMargin, kTopMargin, -kRightMargin, -kBottomMargin);
}

QPointF PlotWidget::toWidget(const QRectF& area, const QPointF& value) const
{
    const double x = area.left() + (value.x() - m_xMin) / (m_xMax - m_xMin) * area.width();
    const double y = area.bottom() - (value.y() - m_yMin) / (m_yMax - m_yMin) * area.height();
    return QPointF(x, y);
}

double PlotWidget::fromWidgetX(const QRectF& area, double x) const
{
    if (area.width() <= 0.0) return m_xMin;
    const double t = std::clamp((x - area.left()) / area.width(), 0.0, 1.0);
    return m_xMin + t * (m_xMax - m_xMin);
}

void PlotWidget::paintGrid(QPainter& painter, const QRectF& area) const
{
    const double xStep = niceStep(m_xMax - m_xMin, std::max(2, int(area.width() / 70)));
    const double yStep = niceStep(m_yMax - m_yMin, std::max(2, int(area.height() / 40)));

    painter.setFont(font());
    const QFontMetricsF metrics(painter.font());

    for (double x = std::ceil(m_xMin / xStep) * xStep; x <= m_xMax + xStep * 1e-6; x += xStep) {
        const double px = toWidget(area, QPointF(x, m_yMin)).x();
        // Zero gets a stronger line: on a bump sweep it is the design position,
        // and it is the thing every curve is read against.
        const bool zero = std::abs(x) < xStep * 1e-6;
        painter.setPen(QPen(zero ? kAxisColor : kGridColor, zero ? 1.0 : 1.0,
                            zero ? Qt::SolidLine : Qt::DotLine));
        painter.drawLine(QPointF(px, area.top()), QPointF(px, area.bottom()));

        painter.setPen(kAxisColor);
        const QString text = tickText(x, xStep);
        painter.drawText(QPointF(px - metrics.horizontalAdvance(text) / 2.0,
                                 area.bottom() + metrics.ascent() + 4.0),
                         text);
    }

    for (double y = std::ceil(m_yMin / yStep) * yStep; y <= m_yMax + yStep * 1e-6; y += yStep) {
        const double py = toWidget(area, QPointF(m_xMin, y)).y();
        const bool zero = std::abs(y) < yStep * 1e-6;
        painter.setPen(QPen(zero ? kAxisColor : kGridColor, 1.0,
                            zero ? Qt::SolidLine : Qt::DotLine));
        painter.drawLine(QPointF(area.left(), py), QPointF(area.right(), py));

        painter.setPen(kAxisColor);
        const QString text = tickText(y, yStep);
        painter.drawText(
            QPointF(area.left() - metrics.horizontalAdvance(text) - 6.0, py + metrics.ascent() / 2.0
                                                                            - 1.0),
            text);
    }

    painter.setPen(kAxisColor);
    painter.drawRect(area);
}

void PlotWidget::paintSeries(QPainter& painter, const QRectF& area) const
{
    painter.setRenderHint(QPainter::Antialiasing, true);
    for (const Series& series : m_series) {
        QPainterPath path;
        bool started = false;
        for (const QPointF& point : series.points) {
            const QPointF widgetPoint = toWidget(area, point);
            if (!started) {
                path.moveTo(widgetPoint);
                started = true;
            } else {
                path.lineTo(widgetPoint);
            }
        }
        painter.setPen(QPen(series.color, 2.0));
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(path);
    }
    painter.setRenderHint(QPainter::Antialiasing, false);
}

void PlotWidget::paintMarkerAndReadout(QPainter& painter, const QRectF& area) const
{
    const double input = m_hovering ? m_hover : m_marker;
    const AxleSample* sample = m_result.nearest(input);
    if (!sample) return;

    const double px = toWidget(area, QPointF(sample->input, m_yMin)).x();
    if (px < area.left() - 1.0 || px > area.right() + 1.0) return;

    painter.setPen(QPen(kMarkerColor, 1.0, Qt::DashLine));
    painter.drawLine(QPointF(px, area.top()), QPointF(px, area.bottom()));

    const QFontMetricsF metrics(painter.font());
    QStringList lines;
    lines << QStringLiteral("%1 %2").arg(QString::number(sample->input, 'f', 2),
                                         sweepInputUnit(m_result.kind));
    for (const Series& series : m_series) {
        const bool left = series.color == kLeftColor;
        double value = 0.0;
        if (!sweepMeasureValue(*sample, m_measure, left, &value)) continue;
        painter.setPen(QPen(series.color, 2.0));
        painter.drawEllipse(toWidget(area, QPointF(sample->input, value)), 3.0, 3.0);
        lines << QStringLiteral("%1  %2 %3")
                     .arg(series.label, QString::number(value, 'f', 3),
                          sweepMeasureUnit(m_measure));
    }

    double width = 0.0;
    for (const QString& line : lines) width = std::max(width, metrics.horizontalAdvance(line));
    const QRectF box(area.left() + 8.0, area.top() + 6.0, width + 12.0,
                     lines.size() * metrics.height() + 8.0);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(255, 255, 255, 216));
    painter.drawRect(box);
    painter.setPen(palette().color(QPalette::Text));
    double y = box.top() + metrics.ascent() + 4.0;
    for (const QString& line : lines) {
        painter.drawText(QPointF(box.left() + 6.0, y), line);
        y += metrics.height();
    }
}

void PlotWidget::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    const QRectF area = plotArea();
    if (area.width() < 20.0 || area.height() < 20.0) return;

    if (!m_hasData) {
        painter.setPen(palette().color(QPalette::Disabled, QPalette::Text));
        painter.drawText(rect(), Qt::AlignCenter,
                         m_result.isEmpty()
                             ? tr("Run a sweep to see a curve.")
                             : tr("Nothing to plot: this axle has no %1.")
                                   .arg(sweepMeasureLabel(m_measure).toLower()));
        return;
    }

    paintGrid(painter, area);
    paintSeries(painter, area);
    paintMarkerAndReadout(painter, area);

    painter.setPen(palette().color(QPalette::Text));
    painter.drawText(QRectF(area.left(), 2.0, area.width(), kTopMargin - 4.0),
                     Qt::AlignLeft | Qt::AlignVCenter,
                     QStringLiteral("%1 [%2]").arg(sweepMeasureLabel(m_measure),
                                                   sweepMeasureUnit(m_measure)));
    painter.drawText(QRectF(area.left(), rect().bottom() - 18.0, area.width(), 16.0),
                     Qt::AlignHCenter | Qt::AlignVCenter,
                     QStringLiteral("%1 [%2]").arg(sweepInputLabel(m_result.kind),
                                                   sweepInputUnit(m_result.kind)));
}

void PlotWidget::mousePressEvent(QMouseEvent* event)
{
    if (!m_hasData) return;
    emit markerMoved(fromWidgetX(plotArea(), event->position().x()));
}

void PlotWidget::mouseMoveEvent(QMouseEvent* event)
{
    if (!m_hasData) return;
    m_hover = fromWidgetX(plotArea(), event->position().x());
    m_hovering = true;
    if (event->buttons() & Qt::LeftButton) emit markerMoved(m_hover);
    update();
}

void PlotWidget::leaveEvent(QEvent*)
{
    m_hovering = false;
    update();
}

} // namespace suspkin
