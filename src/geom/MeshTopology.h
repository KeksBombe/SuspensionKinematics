#pragma once

#include "geom/TriMesh.h"

#include <QVector3D>

#include <cstdint>
#include <vector>

namespace suspkin {

/// Default crease threshold for feature-edge extraction, in degrees.
inline constexpr float kDefaultCreaseAngleDeg = 30.0f;

/// Weld a raw STL triangle soup (3 corners per triangle, nothing shared) into an
/// indexed mesh.
///
/// Face normals are recomputed from the winding and the normals stored in the
/// file are ignored entirely: exporters frequently write zero, unnormalised, or
/// winding-inconsistent normals. Zero-area triangles are dropped and counted in
/// @p droppedDegenerate.
/// @param cornerNormals optional exact per-corner normals, 3 per input triangle,
///        filtered alongside the triangles that survive.
TriMesh weldSoup(const std::vector<QVector3D>& corners, int* droppedDegenerate = nullptr,
                 const std::vector<QVector3D>* cornerNormals = nullptr);

/// Line index buffers for wireframe rendering: 2 indices per edge.
struct EdgeSet {
    std::vector<uint32_t> all;      ///< every unique edge in the mesh
    std::vector<uint32_t> feature;  ///< boundary edges plus creases above the threshold

    std::size_t allCount() const { return all.size() / 2; }
    std::size_t featureCount() const { return feature.size() / 2; }
};

/// Build the unique-edge and feature-edge line buffers.
///
/// An edge is a feature edge when it has only one adjacent face (a boundary or
/// non-manifold edge) or when the dihedral angle between its two adjacent faces
/// exceeds @p creaseAngleDeg. CAD-exported STL is heavily tessellated, so
/// drawing every triangle edge hides the shape; feature edges recover the
/// silhouette and the real part features.
EdgeSet buildEdges(const TriMesh& mesh, float creaseAngleDeg = kDefaultCreaseAngleDeg);

} // namespace suspkin
