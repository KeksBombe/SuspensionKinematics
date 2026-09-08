#include "geom/MeshTopology.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <unordered_map>

namespace suspkin {
namespace {

/// A position quantised onto a uniform grid, used as the vertex-welding key.
struct GridKey {
    std::int64_t x, y, z;
    bool operator==(const GridKey& o) const { return x == o.x && y == o.y && z == o.z; }
};

struct GridKeyHash {
    std::size_t operator()(const GridKey& k) const noexcept
    {
        // FNV-1a over the three coordinates: cheap and well spread for the
        // clustered integer triples a quantised mesh produces.
        std::size_t h = 1469598103934665603ULL;
        const auto mix = [&h](std::int64_t v) {
            h ^= static_cast<std::size_t>(v);
            h *= 1099511628211ULL;
        };
        mix(k.x);
        mix(k.y);
        mix(k.z);
        return h;
    }
};

} // namespace

TriMesh weldSoup(const std::vector<QVector3D>& corners, int* droppedDegenerate,
                 const std::vector<QVector3D>* cornerNormals)
{
    TriMesh mesh;
    int dropped = 0;
    const std::size_t triCount = corners.size() / 3;
    if (triCount == 0) {
        if (droppedDegenerate) *droppedDegenerate = 0;
        return mesh;
    }

    // Pass 1: bounds of the raw soup, so the weld tolerance can scale with the
    // model instead of being an absolute number that is wrong for either a 10 mm
    // bracket or a 3 m chassis.
    Aabb soupBounds;
    for (const QVector3D& c : corners) soupBounds.expand(c);

    const float diag = soupBounds.diagonal();
    // 1e-6 of the diagonal sits far below any real feature and far above the
    // float noise that makes two exporters disagree about "the same" vertex.
    const double eps = (diag > 0.0f) ? 1e-6 * static_cast<double>(diag) : 1e-9;
    const double invEps = 1.0 / eps;

    std::unordered_map<GridKey, std::uint32_t, GridKeyHash> lookup;
    lookup.reserve(triCount * 2);

    const auto weld = [&](const QVector3D& p) -> std::uint32_t {
        const GridKey key{ std::llround(static_cast<double>(p.x()) * invEps),
                           std::llround(static_cast<double>(p.y()) * invEps),
                           std::llround(static_cast<double>(p.z()) * invEps) };
        const auto it = lookup.find(key);
        if (it != lookup.end()) return it->second;
        const auto idx = static_cast<std::uint32_t>(mesh.positions.size());
        mesh.positions.push_back(p);
        lookup.emplace(key, idx);
        return idx;
    };

    const bool haveCornerNormals =
        cornerNormals != nullptr && cornerNormals->size() == corners.size();

    mesh.indices.reserve(triCount * 3);
    mesh.faceNormals.reserve(triCount);
    if (haveCornerNormals) mesh.cornerNormals.reserve(triCount * 3);

    for (std::size_t t = 0; t < triCount; ++t) {
        const QVector3D& a = corners[3 * t + 0];
        const QVector3D& b = corners[3 * t + 1];
        const QVector3D& c = corners[3 * t + 2];

        const QVector3D n = QVector3D::crossProduct(b - a, c - a);
        const float lenSq = n.lengthSquared();
        if (!std::isfinite(lenSq) || lenSq <= 0.0f) {
            ++dropped; // zero area, or NaN/Inf coordinates
            continue;
        }

        const std::uint32_t i0 = weld(a);
        const std::uint32_t i1 = weld(b);
        const std::uint32_t i2 = weld(c);
        if (i0 == i1 || i1 == i2 || i0 == i2) {
            ++dropped; // a sliver thinner than the weld tolerance collapsed to a line
            continue;
        }

        mesh.indices.push_back(i0);
        mesh.indices.push_back(i1);
        mesh.indices.push_back(i2);
        mesh.faceNormals.push_back(n.normalized());
        if (haveCornerNormals) {
            mesh.cornerNormals.push_back((*cornerNormals)[3 * t + 0]);
            mesh.cornerNormals.push_back((*cornerNormals)[3 * t + 1]);
            mesh.cornerNormals.push_back((*cornerNormals)[3 * t + 2]);
        }
    }

    for (const QVector3D& p : mesh.positions) mesh.bounds.expand(p);
    if (droppedDegenerate) *droppedDegenerate = dropped;
    return mesh;
}

EdgeSet buildEdges(const TriMesh& mesh, float creaseAngleDeg)
{
    EdgeSet out;
    const std::size_t triCount = mesh.triangleCount();
    if (triCount == 0) return out;

    struct EdgeRec {
        std::uint32_t v0, v1;
        std::int32_t face0, face1;
    };

    // Key packs the ordered index pair, so the two triangles sharing an edge land
    // on the same entry and each edge is emitted exactly once.
    std::unordered_map<std::uint64_t, EdgeRec> edges;
    edges.reserve(triCount * 3);

    const auto addEdge = [&](std::uint32_t a, std::uint32_t b, std::int32_t face) {
        const std::uint32_t lo = std::min(a, b);
        const std::uint32_t hi = std::max(a, b);
        const std::uint64_t key = (static_cast<std::uint64_t>(lo) << 32) | hi;
        const auto it = edges.find(key);
        if (it == edges.end()) {
            edges.emplace(key, EdgeRec{ lo, hi, face, -1 });
        } else if (it->second.face1 < 0) {
            it->second.face1 = face;
        }
        // More than two faces on one edge means a non-manifold mesh; keep the
        // first two and let the crease test judge on those.
    };

    for (std::size_t t = 0; t < triCount; ++t) {
        const auto face = static_cast<std::int32_t>(t);
        const std::uint32_t i0 = mesh.indices[3 * t + 0];
        const std::uint32_t i1 = mesh.indices[3 * t + 1];
        const std::uint32_t i2 = mesh.indices[3 * t + 2];
        addEdge(i0, i1, face);
        addEdge(i1, i2, face);
        addEdge(i2, i0, face);
    }

    const float cosThreshold =
        std::cos(creaseAngleDeg * static_cast<float>(std::numbers::pi) / 180.0f);

    out.all.reserve(edges.size() * 2);
    out.feature.reserve(edges.size() / 2);

    for (const auto& [key, e] : edges) {
        out.all.push_back(e.v0);
        out.all.push_back(e.v1);

        bool isFeature = (e.face1 < 0); // boundary or non-manifold
        if (!isFeature) {
            const float d = QVector3D::dotProduct(mesh.faceNormals[e.face0],
                                                  mesh.faceNormals[e.face1]);
            isFeature = (d < cosThreshold);
        }
        if (isFeature) {
            out.feature.push_back(e.v0);
            out.feature.push_back(e.v1);
        }
    }
    return out;
}

} // namespace suspkin
