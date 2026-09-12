#pragma once

#include "geom/TriMesh.h"
#include "geom/Vec3.h"

#include <cstdint>
#include <limits>
#include <optional>
#include <vector>

namespace suspkin {

/// Where a ray met a mesh.
struct RayHit {
    double distance = 0.0; ///< along the ray from its origin, in millimetres
    Vec3 point;
    int triangle = -1;     ///< index into the mesh's triangles
};

/// Questions asked of an imported mesh: where a ray first meets it, and how far
/// a point is from it.
///
/// This is what lets the hardpoint generator put an inboard pivot on the
/// chassis rather than at a guessed distance. The Python tool it ports leaned on
/// Open3D for the same two questions; here they are a bounding volume hierarchy
/// over the triangles, which is a few hundred lines rather than a dependency.
///
/// Built once per mesh -- a sort of the triangles, O(n log n) -- and then each
/// query touches only the handful of boxes near it. Doubles throughout, for the
/// same reason the solver is in doubles: the answer is a hardpoint coordinate.
/// No OpenGL and no widgets, so it lives in the core and is tested headlessly.
class MeshQuery {
public:
    MeshQuery() = default;
    explicit MeshQuery(const TriMesh& mesh);

    bool isEmpty() const { return m_order.empty(); }
    std::size_t triangleCount() const { return m_order.size(); }

    /// The first place the ray from @p origin along @p direction meets the
    /// surface, no further than @p maxDistance. Either face counts: a chassis
    /// tube is met from outside, and its normals may point either way.
    std::optional<RayHit> castRay(const Vec3& origin, const Vec3& direction,
                                  double maxDistance = std::numeric_limits<double>::infinity()) const;

    /// How far @p point is from the nearest point on the surface. Unsigned: an
    /// STL has no reliable inside. Infinite for an empty mesh.
    double distanceTo(const Vec3& point) const;

private:
    struct Node {
        Vec3 min;
        Vec3 max;
        /// A leaf holds @ref count triangles from @ref first in m_order; an
        /// inner node has count zero and its children at @ref first and first+1.
        std::uint32_t first = 0;
        std::uint32_t count = 0;
    };

    void build(std::uint32_t node, std::uint32_t begin, std::uint32_t end,
               const std::vector<Vec3>& centroids);
    Vec3 corner(std::uint32_t triangle, int which) const
    {
        return m_positions[m_indices[3 * triangle + static_cast<std::uint32_t>(which)]];
    }

    std::vector<Vec3> m_positions;
    std::vector<std::uint32_t> m_indices;
    std::vector<std::uint32_t> m_order; ///< triangle indices, grouped by leaf
    std::vector<Node> m_nodes;
};

} // namespace suspkin
