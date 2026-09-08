#pragma once

#include "render/DisplayMode.h"

#include <QPointF>
#include <QRectF>

class QPainter;

namespace suspkin {

/// Segmented Solid / Triangles control drawn in the viewport's top-right corner.
///
/// Painted rather than built from real buttons so that it lands in the same
/// framebuffer as the 3D scene: one surface to composite, and a screenshot of the
/// viewport shows the whole UI.
class ModeSelector {
public:
    static constexpr int kButtonCount = 2;

    void setTopRight(const QPointF& topRight);
    QRectF bounds() const { return m_bounds; }
    bool contains(const QPointF& pos) const { return m_bounds.contains(pos); }

    void paint(QPainter& painter, DisplayMode current, int hoveredButton) const;

    /// 0 = Solid, 1 = Triangles, -1 = not on a button.
    int buttonAt(const QPointF& pos) const;
    static DisplayMode modeForButton(int button)
    {
        return button == 1 ? DisplayMode::Triangles : DisplayMode::Solid;
    }

private:
    QRectF buttonRect(int index) const;
    QRectF m_bounds;
};

} // namespace suspkin
