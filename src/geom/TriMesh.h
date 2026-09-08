#pragma once

#include "geom/Aabb.h"

#include <QVector3D>

#include <cstdint>
#include <vector>

namespace suspkin {

/// Indexed triangle mesh with welded vertices and normals.
///
/// Two normal representations, because the two sources differ in kind. An STL
/// describes a genuinely faceted body, so its facets are the real geometry and
/// flat shading is the honest way to draw them -- that is @c faceNormals. A STEP
/// body is analytic surfaces that merely got tessellated, so its true normal
/// varies across every triangle; @c cornerNormals carries those exact values and,
/// when present, takes precedence.
struct TriMesh {
    std::vector<QVector3D> positions;     ///< welded, unique
    std::vector<uint32_t>  indices;       ///< 3 per triangle, indexing positions
    std::vector<QVector3D> faceNormals;   ///< 1 per triangle, unit length
    std::vector<QVector3D> cornerNormals; ///< optional: 3 per triangle, parallel to indices
    Aabb bounds;

    /// True when exact per-vertex normals are available (a tessellated B-Rep).
    bool hasCornerNormals() const { return cornerNormals.size() == indices.size(); }

    std::size_t triangleCount() const { return indices.size() / 3; }
    std::size_t vertexCount() const { return positions.size(); }
    bool isEmpty() const { return indices.empty(); }
};

} // namespace suspkin
