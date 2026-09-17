#include "render/NavGizmo.h"

#include "render/MoveGizmo.h"

#include <QColor>
#include <QFont>
#include <QPainter>
#include <QVector3D>

#include <algorithm>

namespace suspkin {
namespace {

// Axis order: +X, -X, +Y, -Y, +Z, -Z.
const QVector3D kAxisDirections[NavGizmo::kAxisCount] = {
    { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 },
};
constexpr bool kIsPositive[NavGizmo::kAxisCount] = { true, false, true, false, true, false };
constexpr const char* kAxisLabels[3] = { "X", "Y", "Z" };

/// The balls come in pairs -- +X then -X, and so on -- and a pair shares the
/// colour of its axis with the arrows the selected hardpoint gets.
QColor axisColor(int axis)
{
    return MoveGizmo::axisColor(axis / 2);
}

} // namespace

ViewPreset NavGizmo::presetForAxis(int axis)
{
    // Looking down an axis towards the origin: standing on +X looks at the front
    // of the vehicle, on +Y (left in ISO 8855) at its left side, and so on.
    switch (axis) {
    case 0:  return ViewPreset::Front;
    case 1:  return ViewPreset::Rear;
    case 2:  return ViewPreset::Left;
    case 3:  return ViewPreset::Right;
    case 4:  return ViewPreset::Top;
    default: return ViewPreset::Bottom;
    }
}

std::array<NavGizmo::Ball, NavGizmo::kAxisCount> NavGizmo::layout(const Camera& camera) const
{
    const QVector3D right = camera.right();
    const QVector3D up = camera.up();
    const QVector3D forward = camera.forward();

    const QPointF centre = m_bounds.center();
    const qreal radius = m_bounds.width() / 2.0 - kBallRadius - 1.0;

    std::array<Ball, kAxisCount> balls{};
    for (int i = 0; i < kAxisCount; ++i) {
        const QVector3D& d = kAxisDirections[i];
        // Project the world axis onto the camera basis. Screen Y grows downward,
        // hence the negation.
        const qreal sx = QVector3D::dotProduct(d, right);
        const qreal sy = -QVector3D::dotProduct(d, up);
        balls[i] = Ball{ centre + QPointF(sx * radius, sy * radius),
                         QVector3D::dotProduct(d, forward), i };
    }
    return balls;
}

int NavGizmo::axisAt(const QPointF& pos, const Camera& camera) const
{
    auto balls = layout(camera);
    // Nearest to the viewer first, so a ball in front wins an overlap.
    std::sort(balls.begin(), balls.end(),
              [](const Ball& a, const Ball& b) { return a.depth < b.depth; });

    for (const Ball& ball : balls) {
        const QPointF delta = pos - ball.pos;
        const qreal reach = kBallRadius + 2.0;
        if (QPointF::dotProduct(delta, delta) <= reach * reach) return ball.axis;
    }
    return -1;
}

void NavGizmo::paint(QPainter& painter, const Camera& camera, int hoveredAxis) const
{
    if (m_bounds.isEmpty()) return;

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);

    auto balls = layout(camera);
    // Painter's algorithm: furthest first, so nearer axes overlap them.
    std::sort(balls.begin(), balls.end(),
              [](const Ball& a, const Ball& b) { return a.depth > b.depth; });

    const QPointF centre = m_bounds.center();

    if (hoveredAxis >= 0) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(255, 255, 255, 18));
        painter.drawEllipse(m_bounds);
    }

    QFont font = painter.font();
    font.setBold(true);
    font.setPixelSize(10);
    painter.setFont(font);

    for (const Ball& ball : balls) {
        const QColor colour = axisColor(ball.axis);
        const bool positive = kIsPositive[ball.axis];
        const bool hovered = (ball.axis == hoveredAxis);

        // Spokes only on the positive axes, matching the convention that the
        // lettered end is the one being named.
        if (positive) {
            QPen spoke(colour, 2.0);
            spoke.setCapStyle(Qt::RoundCap);
            painter.setPen(spoke);
            painter.drawLine(centre, ball.pos);
        }

        const qreal r = hovered ? kBallRadius + 1.0 : kBallRadius;
        const QRectF disc(ball.pos.x() - r, ball.pos.y() - r, 2 * r, 2 * r);

        if (positive || hovered) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(hovered ? colour.lighter(118) : colour);
            painter.drawEllipse(disc);

            painter.setPen(QColor(24, 24, 27));
            painter.drawText(disc, Qt::AlignCenter,
                             QString::fromLatin1(kAxisLabels[ball.axis / 2]));
        } else {
            // Hollow ring for the negative end: present, but clearly secondary.
            painter.setPen(QPen(colour, 1.8));
            painter.setBrush(QColor(30, 30, 34, 200));
            painter.drawEllipse(disc);
        }
    }

    painter.restore();
}

} // namespace suspkin
