#include "render/ModeSelector.h"

#include <QPainter>
#include <QPainterPath>
#include <QRadialGradient>

namespace suspkin {
namespace {

constexpr qreal kPadding = 4.0;
constexpr qreal kButtonSize = 28.0;
constexpr qreal kSpacing = 2.0;

/// A shaded ball: the surface with no tessellation showing.
void drawSolidIcon(QPainter& painter, const QRectF& box)
{
    const QRectF disc = box.adjusted(5, 5, -5, -5);
    QRadialGradient gradient(disc.center() + QPointF(-disc.width() * 0.22, -disc.height() * 0.26),
                             disc.width() * 0.9);
    gradient.setColorAt(0.0, QColor(255, 255, 255));
    gradient.setColorAt(1.0, QColor(146, 152, 163));
    painter.setPen(Qt::NoPen);
    painter.setBrush(gradient);
    painter.drawEllipse(disc);
}

/// The same ball as bare triangle edges: no surface, matching what the mode draws.
void drawTrianglesIcon(QPainter& painter, const QRectF& box)
{
    const QRectF disc = box.adjusted(5, 5, -5, -5);
    painter.save();
    QPainterPath clip; // keep the mesh inside the ball's silhouette
    clip.addEllipse(disc);
    painter.setClipPath(clip);
    painter.setPen(QPen(QColor(238, 241, 246), 1.0));
    const QPointF c = disc.center();
    const qreal r = disc.width() / 2.0;
    painter.drawLine(QPointF(c.x() - r, c.y() - r * 0.20), QPointF(c.x() + r, c.y() - r * 0.20));
    painter.drawLine(QPointF(c.x() - r, c.y() + r * 0.50), QPointF(c.x() + r, c.y() + r * 0.50));
    painter.drawLine(QPointF(c.x() - r * 0.30, c.y() - r), QPointF(c.x() - r * 0.30, c.y() + r));
    painter.drawLine(QPointF(c.x() - r, c.y() + r), QPointF(c.x() + r * 0.7, c.y() - r));
    painter.restore();

    painter.setPen(QPen(QColor(238, 241, 246), 1.2));
    painter.setBrush(Qt::NoBrush);
    painter.drawEllipse(disc);
}

} // namespace

void ModeSelector::setTopRight(const QPointF& topRight)
{
    const qreal width = 2 * kPadding + kButtonCount * kButtonSize + (kButtonCount - 1) * kSpacing;
    const qreal height = 2 * kPadding + kButtonSize;
    m_bounds = QRectF(topRight.x() - width, topRight.y(), width, height);
}

QRectF ModeSelector::buttonRect(int index) const
{
    return QRectF(m_bounds.left() + kPadding + index * (kButtonSize + kSpacing),
                  m_bounds.top() + kPadding, kButtonSize, kButtonSize);
}

int ModeSelector::buttonAt(const QPointF& pos) const
{
    for (int i = 0; i < kButtonCount; ++i)
        if (buttonRect(i).contains(pos)) return i;
    return -1;
}

void ModeSelector::paint(QPainter& painter, DisplayMode current, int hoveredButton) const
{
    if (m_bounds.isEmpty()) return;

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);

    painter.setPen(QPen(QColor(255, 255, 255, 26), 1.0));
    painter.setBrush(QColor(26, 26, 30, 216));
    painter.drawRoundedRect(m_bounds.adjusted(0.5, 0.5, -0.5, -0.5), 7, 7);

    const int activeButton = (current == DisplayMode::Triangles) ? 1 : 0;
    for (int i = 0; i < kButtonCount; ++i) {
        const QRectF box = buttonRect(i);
        painter.setPen(Qt::NoPen);
        if (i == activeButton) {
            painter.setBrush(QColor(71, 114, 179));
            painter.drawRoundedRect(box, 5, 5);
        } else if (i == hoveredButton) {
            painter.setBrush(QColor(255, 255, 255, 28));
            painter.drawRoundedRect(box, 5, 5);
        }
        if (i == 0)
            drawSolidIcon(painter, box);
        else
            drawTrianglesIcon(painter, box);
    }

    painter.restore();
}

} // namespace suspkin
