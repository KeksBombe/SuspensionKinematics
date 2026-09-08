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

    Aabb bounds() const
    {
        Aabb box;
        for (const Hardpoint& point : points) box.expand(point.toVector());
        return box;
    }
};

} // namespace suspkin
