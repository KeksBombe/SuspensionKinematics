#pragma once

#include <QVector3D>

#include <algorithm>
#include <limits>

namespace suspkin {

/// Axis-aligned bounding box. A default-constructed box is empty (min > max),
/// which makes expand() work without a separate "first point" special case.
struct Aabb {
    QVector3D min{  std::numeric_limits<float>::max(),
                    std::numeric_limits<float>::max(),
                    std::numeric_limits<float>::max() };
    QVector3D max{ -std::numeric_limits<float>::max(),
                   -std::numeric_limits<float>::max(),
                   -std::numeric_limits<float>::max() };

    bool isEmpty() const { return min.x() > max.x(); }

    void expand(const QVector3D& p)
    {
        min.setX(std::min(min.x(), p.x()));
        min.setY(std::min(min.y(), p.y()));
        min.setZ(std::min(min.z(), p.z()));
        max.setX(std::max(max.x(), p.x()));
        max.setY(std::max(max.y(), p.y()));
        max.setZ(std::max(max.z(), p.z()));
    }

    QVector3D center() const { return isEmpty() ? QVector3D() : (min + max) * 0.5f; }
    QVector3D extent() const { return isEmpty() ? QVector3D() : (max - min); }

    /// Length of the space diagonal; 0 for an empty box.
    float diagonal() const { return isEmpty() ? 0.0f : (max - min).length(); }
};

} // namespace suspkin
