#pragma once

#include "geom/Aabb.h"

#include <QVector3D>

#include <cstdint>
#include <vector>

namespace suspkin {

/// Indexed triangle mesh with welded vertices and one normal per face.
///
/// Normals are per-face rather than per-vertex on purpose: an STL describes a
/// faceted body, so the facets are the real geometry and flat shading is the
/// honest way to draw them.
struct TriMesh {
    std::vector<QVector3D> positions;   ///< welded, unique
    std::vector<uint32_t>  indices;     ///< 3 per triangle, indexing positions
    std::vector<QVector3D> faceNormals; ///< 1 per triangle, unit length
    Aabb bounds;

    std::size_t triangleCount() const { return indices.size() / 3; }
    std::size_t vertexCount() const { return positions.size(); }
    bool isEmpty() const { return indices.empty(); }
};

} // namespace suspkin
