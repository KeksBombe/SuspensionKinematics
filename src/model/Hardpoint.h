#pragma once

#include "geom/Aabb.h"

#include <QString>
#include <QVector3D>

#include <cstddef>
#include <vector>

namespace suspkin {

/// One named suspension hardpoint, in the vehicle frame and in millimetres.
///
/// The coordinates are doubles rather than the floats the renderer works in.
/// They arrive from a spreadsheet and are written back to one, and quietly
/// rounding a value the user never touched would surface in their file as a
/// change they did not make.
struct Hardpoint {
    QString name;                 ///< the base name, e.g. "F_LCA_O"
    double coord[3] = { 0.0, 0.0, 0.0 };

    /// The point this one was mirrored from, empty for a point that came out of
    /// the workbook. Kept because it is the only record of where a mirrored
    /// point came from once its coordinates have been edited by hand.
    QString mirrorOf;

    bool isMirrored() const { return !mirrorOf.isEmpty(); }

    double x() const { return coord[0]; }
    double y() const { return coord[1]; }
    double z() const { return coord[2]; }

    /// Renderer-side position. Millimetre magnitudes are far inside float
    /// precision, so nothing visible is lost here.
    QVector3D toVector() const
    {
        return QVector3D(static_cast<float>(coord[0]), static_cast<float>(coord[1]),
                         static_cast<float>(coord[2]));
    }
};

/// An ordered set of hardpoints. The order is the one the source file used, so
/// that a saved workbook still reads the way its author laid it out.
struct HardpointTable {
    std::vector<Hardpoint> points;

    bool isEmpty() const { return points.empty(); }
    std::size_t size() const { return points.size(); }

    /// Index of @p name, or -1. Linear: a hardpoint list is tens of rows, and
    /// keeping an index in step with editing would cost more than it saves.
    int indexOf(const QString& name) const
    {
        for (std::size_t i = 0; i < points.size(); ++i)
            if (points[i].name == name) return static_cast<int>(i);
        return -1;
    }

    const Hardpoint* find(const QString& name) const
    {
        const int index = indexOf(name);
        return index < 0 ? nullptr : &points[static_cast<std::size_t>(index)];
    }

    Aabb bounds() const
    {
        Aabb box;
        for (const Hardpoint& point : points) box.expand(point.toVector());
        return box;
    }
};

} // namespace suspkin
