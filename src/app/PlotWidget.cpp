#include "app/PlotWidget.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>
#include <iterator>

namespace suspkin {
namespace {

/// Room for the tick labels and the axis titles. Fixed rather than measured:
/// the labels are short numbers and a fixed margin keeps the axes from twitching
/// as the curve is scrolled.
constexpr int kLeftMargin = 62;
constexpr int kRightMargin = 14;
constexpr int kTopMargin = 26;
constexpr int kBottomMargin = 40;

/// One per axle, front first. Saturated enough to read on a dark ground and a
/// light one, and the first two far enough apart in hue to tell apart for the
/// commonest colour blindness. The wheels of one axle are told apart by the
/// line, not the colour: solid for the left, dashed for the right.
const QColor kAxleColors[] = {
    QColor(58, 124, 216),
    QColor(214, 106, 42),
    QColor(46, 158, 92),
    QColor(156, 92, 200),
};

constexpr Qt::PenStyle kLeftStyle = Qt::SolidLine;
constexpr Qt::PenStyle kRightStyle = Qt::DashLine;

/// @p a moved @p t of the way to @p b. How every neutral here is made: from the
/// palette's own ground and text, so a dark theme gets a dark plot with a faint
/// grid rather than a pale grid painted onto it.
QColor mix(const QColor& a, const QColor& b, float t)
{
    return QColor::fromRgbF(a.redF() + (b.redF() - a.redF()) * t,
                            a.greenF() + (b.greenF() - a.greenF()) * t,
                            a.blueF() + (b.blueF() - a.blueF()) * t);
}

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
/// reads "1.5", a 5 step reads "10" and a 0.002 step reads "0.004" -- never a
/// column of labels that all say the same thing.
QString tickText(double value, double step)
{
    const int decimals = std::clamp(int(std::ceil(-std::log10(step) - 1e-9)), 0, 6);
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

QColor PlotWidget::axleColor(int colorIndex)
{
    constexpr int count = int(std::size(kAxleColors));
    return kAxleColors[((colorIndex % count) + count) % count];
}

void PlotWidget::setSweeps(const QList<PlotSweep>& sweeps)
{
    m_sweeps = sweeps;
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

void PlotWidget::setSides(SweepSides sides)
{
    if (m_sides == sides) return;
    m_sides = sides;
    rebuild();
    update();
}

void PlotWidget::setPlaceholder(const QString& text)
{
    if (m_placeholder == text) return;
    m_placeholder = text;
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
    for (int i = 0; i < m_sweeps.size(); ++i) m_series += seriesOf(i);
    if (m_series.isEmpty()) return;
    fitRange();
    m_hasData = true;
}

QList<PlotWidget::Series> PlotWidget::seriesOf(int sweepIndex) const
{
    const PlotSweep& sweep = m_sweeps[sweepIndex];
    const QString axle = sweep.result.axleLabel.isEmpty() ? sweep.result.axleToken
                                                          : sweep.result.axleLabel;
    struct SideSpec {
        bool left;
        Qt::PenStyle style;
        QString label;
    };
    QList<SideSpec> sides;
    if (sweepMeasureIsPerSide(m_measure)) {
        // The range is taken from what is drawn, so a side left out does not
        // leave its curve's room behind in the plot either.
        if (m_sides != SweepSides::Right)
            sides.append({ true, kLeftStyle, tr("%1 left").arg(axle).trimmed() });
        if (m_sides != SweepSides::Left)
            sides.append({ false, kRightStyle, tr("%1 right").arg(axle).trimmed() });
    } else {
        sides.append({ true, kLeftStyle, axle.isEmpty() ? sweepMeasureLabel(m_measure) : axle });
    }

    QList<Series> result;
    for (const SideSpec& side : sides) {
        Series series;
        series.color = axleColor(sweep.colorIndex);
        series.style = side.style;
        series.label = side.label;
        series.sweep = sweepIndex;
        series.left = side.left;
        for (const AxleSample& sample : sweep.result.samples) {
            double value = 0.0;
            if (sweepMeasureValue(sample, m_measure, side.left, &value))
                series.points.append(QPointF(sample.input, value));
        }
        if (!series.points.isEmpty()) result.append(series);
    }
    return result;
}

void PlotWidget::fitRange()
{
    double xMin = m_series.front().points.front().x();
    double xMax = xMin;
    double yMin = m_series.front().points.front().y();
    double yMax = yMin;
    for (const Series& series : std::as_const(m_series))
        for (const QPointF& point : series.points) {
            xMin = std::min(xMin, point.x());
            xMax = std::max(xMax, point.x());
            yMin = std::min(yMin, point.y());
            yMax = std::max(yMax, point.y());
        }

    if (xMax - xMin < 1e-9) {
        xMin -= 1.0;
        xMax += 1.0;
    }

    // A curve that moves less than its measure is worth over the whole sweep is
    // drawn flat, in a band that wide, rather than stretched until the solver's
    // rounding fills the plot -- which is how a roll centre sitting on the
    // centreline through a bump used to come out as a wild line of spikes under
    // an axis that read "0.000" at every tick.
    const double resolution = sweepMeasureResolution(m_measure);
    if (yMax - yMin < resolution) {
        const double middle = 0.5 * (yMin + yMax);
        yMin = middle - 0.5 * resolution;
        yMax = middle + 0.5 * resolution;
    } else {
        const double pad = (yMax - yMin) * 0.08;
        yMin -= pad;
        yMax += pad;
    }

    m_xMin = xMin;
    m_xMax = xMax;
    m_yMin = yMin;
    m_yMax = yMax;
}

const SweepResult* PlotWidget::leadingResult() const
{
    for (const PlotSweep& sweep : m_sweeps)
        if (!sweep.result.isEmpty()) return &sweep.result;
    return nullptr;
}

QString PlotWidget::emptyText() const
{
    const SweepResult* lead = leadingResult();
    if (!lead) return m_placeholder.isEmpty() ? tr("Run a sweep to see a curve.") : m_placeholder;
    if (m_measure == SweepMeasure::Ackermann && lead->kind != SweepKind::Steer)
        return tr("Ackermann is read off a steer sweep.");
    if (m_measure == SweepMeasure::Ackermann)
        return tr("Nothing to plot: Ackermann needs both wheels of an axle, and a second axle "
                  "in the table to take the wheelbase to.");
    return m_sweeps.size() == 1
               ? tr("Nothing to plot: this axle has no %1.")
                     .arg(sweepMeasureLabel(m_measure).toLower())
               : tr("Nothing to plot: none of the axles shown has a %1.")
                     .arg(sweepMeasureLabel(m_measure).toLower());
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
    const QColor base = palette().color(QPalette::Base);
    const QColor text = palette().color(QPalette::Text);
    const QColor grid = mix(base, text, 0.16f);
    const QColor axis = mix(base, text, 0.45f);
    const QColor label = mix(base, text, 0.7f);

    const double xStep = niceStep(m_xMax - m_xMin, std::max(2, int(area.width() / 70)));
    const double yStep = niceStep(m_yMax - m_yMin, std::max(2, int(area.height() / 40)));

    painter.setFont(font());
    const QFontMetricsF metrics(painter.font());

    for (double x = std::ceil(m_xMin / xStep) * xStep; x <= m_xMax + xStep * 1e-6; x += xStep) {
        const double px = toWidget(area, QPointF(x, m_yMin)).x();
        // Zero gets a stronger line: on a bump sweep it is the design position,
        // and it is the thing every curve is read against.
        const bool zero = std::abs(x) < xStep * 1e-6;
        painter.setPen(QPen(zero ? axis : grid, 1.0, zero ? Qt::SolidLine : Qt::DotLine));
        painter.drawLine(QPointF(px, area.top()), QPointF(px, area.bottom()));

        painter.setPen(label);
        const QString tick = tickText(x, xStep);
        painter.drawText(QPointF(px - metrics.horizontalAdvance(tick) / 2.0,
                                 area.bottom() + metrics.ascent() + 4.0),
                         tick);
    }

    for (double y = std::ceil(m_yMin / yStep) * yStep; y <= m_yMax + yStep * 1e-6; y += yStep) {
        const double py = toWidget(area, QPointF(m_xMin, y)).y();
        const bool zero = std::abs(y) < yStep * 1e-6;
        painter.setPen(QPen(zero ? axis : grid, 1.0, zero ? Qt::SolidLine : Qt::DotLine));
        painter.drawLine(QPointF(area.left(), py), QPointF(area.right(), py));

        painter.setPen(label);
        const QString tick = tickText(y, yStep);
        painter.drawText(
            QPointF(area.left() - metrics.horizontalAdvance(tick) - 6.0, py + metrics.ascent() / 2.0
                                                                            - 1.0),
            tick);
    }

    painter.setPen(axis);
    painter.setBrush(Qt::NoBrush);
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
        painter.setPen(QPen(series.color, 2.0, series.style));
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(path);
    }
    painter.setRenderHint(QPainter::Antialiasing, false);
}

void PlotWidget::paintMarkerAndReadout(QPainter& painter, const QRectF& area) const
{
    const double input = m_hovering ? m_hover : m_marker;
    const SweepResult* lead = leadingResult();
    const AxleSample* sample = lead ? lead->nearest(input) : nullptr;
    if (!sample) return;

    const double px = toWidget(area, QPointF(sample->input, m_yMin)).x();
    if (px < area.left() - 1.0 || px > area.right() + 1.0) return;

    const QColor base = palette().color(QPalette::Base);
    const QColor text = palette().color(QPalette::Text);
    painter.setPen(QPen(mix(base, text, 0.55f), 1.0, Qt::DashLine));
    painter.drawLine(QPointF(px, area.top()), QPointF(px, area.bottom()));

    struct Line {
        QString text;
        QColor swatch; ///< invalid for the line that is not a series
        Qt::PenStyle style = Qt::SolidLine;
    };
    QList<Line> lines;
    lines.append({ QStringLiteral("%1 %2").arg(QString::number(sample->input, 'f', 2),
                                               sweepInputUnit(lead->kind)),
                   QColor() });
    painter.setRenderHint(QPainter::Antialiasing, true);
    for (const Series& series : m_series) {
        const AxleSample* own = m_sweeps[series.sweep].result.nearest(input);
        double value = 0.0;
        if (!own || !sweepMeasureValue(*own, m_measure, series.left, &value)) continue;
        painter.setPen(QPen(series.color, 2.0));
        painter.setBrush(base);
        painter.drawEllipse(toWidget(area, QPointF(own->input, value)), 3.0, 3.0);
        lines.append({ QStringLiteral("%1  %2 %3")
                           .arg(series.label, sweepValueText(value),
                                sweepMeasureUnit(m_measure)),
                       series.color, series.style });
    }
    painter.setRenderHint(QPainter::Antialiasing, false);

    const QFontMetricsF metrics(painter.font());
    constexpr double kSwatch = 12.0;
    double width = 0.0;
    for (const Line& line : lines) width = std::max(width, metrics.horizontalAdvance(line.text));
    width += kSwatch + 6.0;
    // On the side away from the line, so it never sits over the stretch of the
    // curve that is being read.
    const bool right = px < area.center().x();
    const double boxWidth = width + 12.0;
    const QRectF box(right ? area.right() - 8.0 - boxWidth : area.left() + 8.0, area.top() + 6.0,
                     boxWidth, lines.size() * metrics.height() + 8.0);

    // The window's own colours, which are a readable pair on any theme. This
    // used to be a white box written on in the palette's text colour: fine on
    // a light desktop and white on white on a dark one.
    const QColor window = palette().color(QPalette::Window);
    QColor fill = window;
    fill.setAlpha(235);
    painter.setPen(QPen(mix(window, palette().color(QPalette::WindowText), 0.25f), 1.0));
    painter.setBrush(fill);
    painter.drawRect(box);

    double y = box.top() + 4.0;
    for (const Line& line : lines) {
        const double baseline = y + metrics.ascent();
        if (line.swatch.isValid()) {
            painter.setPen(QPen(line.swatch, 2.0, line.style));
            const double mid = y + metrics.height() / 2.0;
            painter.drawLine(QPointF(box.left() + 6.0, mid), QPointF(box.left() + 6.0 + kSwatch, mid));
        }
        painter.setPen(palette().color(QPalette::WindowText));
        painter.drawText(QPointF(box.left() + 6.0 + kSwatch + 6.0, baseline), line.text);
        y += metrics.height();
    }
}

void PlotWidget::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    const QRectF area = plotArea();
    if (area.width() < 20.0 || area.height() < 20.0) return;

    if (!m_hasData) {
        const QString text = emptyText();
        painter.setPen(palette().color(QPalette::Disabled, QPalette::Text));
        painter.drawText(QRectF(rect()).adjusted(16.0, 0.0, -16.0, 0.0),
                         Qt::AlignCenter | Qt::TextWordWrap,
                         QStringLiteral("%1\n\n%2").arg(sweepMeasureLabel(m_measure), text));
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
    // Every axle is swept with one spec, so the first says what the input is.
    const SweepKind kind = leadingResult()->kind;
    painter.drawText(QRectF(area.left(), rect().bottom() - 18.0, area.width(), 16.0),
                     Qt::AlignHCenter | Qt::AlignVCenter,
                     QStringLiteral("%1 [%2]").arg(sweepInputLabel(kind), sweepInputUnit(kind)));
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
