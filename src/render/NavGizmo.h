#pragma once

#include "render/Camera.h"

#include <QPointF>
#include <QRectF>

#include <array>

class QPainter;

namespace suspkin {

/// Navigation gizmo drawn in the viewport's top-right corner.
///
/// Six axis balls orbit a common centre, following the camera. Positive axes are
/// filled and lettered, negative axes are hollow rings. Clicking a ball snaps to
/// that view; dragging the gizmo orbits freely.
class NavGizmo {
public:
    static constexpr int kAxisCount = 6;   ///< +X, -X, +Y, -Y, +Z, -Z
    static constexpr qreal kBallRadius = 9.5;

    void setBounds(const QRectF& bounds) { m_bounds = bounds; }
    QRectF bounds() const { return m_bounds; }
    bool contains(const QPointF& pos) const { return m_bounds.contains(pos); }

    void paint(QPainter& painter, const Camera& camera, int hoveredAxis) const;

    /// Index of the ball under @p pos, or -1. Nearest ball wins.
    int axisAt(const QPointF& pos, const Camera& camera) const;

    static ViewPreset presetForAxis(int axis);

private:
    struct Ball {
        QPointF pos;
        float depth;   ///< along the view direction; larger is further away
        int axis;
    };
    std::array<Ball, kAxisCount> layout(const Camera& camera) const;

    QRectF m_bounds;
};

} // namespace suspkin
