#include "geom/MeshQuery.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace suspkin {
namespace {

/// Few enough that a leaf is cheap to test outright, many enough that the tree
/// stays shallow. Four is the usual answer and nothing here argues with it.
constexpr std::uint32_t kLeafSize = 4;

double axisOf(const Vec3& v, int axis) { return axis == 0 ? v.x : (axis == 1 ? v.y : v.z); }

Vec3 minOf(const Vec3& a, const Vec3& b)
{
    return Vec3(std::min(a.x, b.x), std::min(a.y, b.y), std::min(a.z, b.z));
}
Vec3 maxOf(const Vec3& a, const Vec3& b)
{
    return Vec3(std::max(a.x, b.x), std::max(a.y, b.y), std::max(a.z, b.z));
}

/// Where the ray enters and leaves the box, or false when it misses. The slab
/// test, with the reciprocal precomputed by the caller.
bool rayBox(const Vec3& origin, const Vec3& inverse, const Vec3& min, const Vec3& max,
            double limit, double* entry)
{
    double near = 0.0;
    double far = limit;
    for (int axis = 0; axis < 3; ++axis) {
        const double o = axisOf(origin, axis);
        const double inv = axisOf(inverse, axis);
        double t0 = (axisOf(min, axis) - o) * inv;
        double t1 = (axisOf(max, axis) - o) * inv;
        // A ray parallel to this slab gives inf or nan; a nan says nothing
        // about the other two, so it is left out of the comparison.
        if (t0 > t1) std::swap(t0, t1);
        if (!std::isnan(t0)) near = std::max(near, t0);
        if (!std::isnan(t1)) far = std::min(far, t1);
        if (near > far) return false;
    }
    *entry = near;
    return true;
}

/// Moeller-Trumbore, both faces. The distance along the ray, or a negative
/// number for a miss.
double rayTriangle(const Vec3& origin, const Vec3& direction, const Vec3& a, const Vec3& b,
                   const Vec3& c)
{
    const Vec3 ab = b - a;
    const Vec3 ac = c - a;
    const Vec3 p = cross(direction, ac);
    const double det = dot(ab, p);
    // A degenerate triangle, or a ray in its plane: nothing to hit.
    if (std::abs(det) < 1e-18) return -1.0;
    const double inv = 1.0 / det;
    const Vec3 s = origin - a;
    const double u = dot(s, p) * inv;
    if (u < 0.0 || u > 1.0) return -1.0;
    const Vec3 q = cross(s, ab);
    const double v = dot(direction, q) * inv;
    if (v < 0.0 || u + v > 1.0) return -1.0;
    return dot(ac, q) * inv;
}

/// The point of triangle abc nearest @p p -- Ericson, Real-Time Collision
/// Detection, 5.1.5 -- by working out which of its seven regions p falls in.
Vec3 closestOnTriangle(const Vec3& p, const Vec3& a, const Vec3& b, const Vec3& c)
{
    const Vec3 ab = b - a;
    const Vec3 ac = c - a;
    const Vec3 ap = p - a;
    const double d1 = dot(ab, ap);
    const double d2 = dot(ac, ap);
    if (d1 <= 0.0 && d2 <= 0.0) return a;

    const Vec3 bp = p - b;
    const double d3 = dot(ab, bp);
    const double d4 = dot(ac, bp);
    if (d3 >= 0.0 && d4 <= d3) return b;

    const double vc = d1 * d4 - d3 * d2;
    if (vc <= 0.0 && d1 >= 0.0 && d3 <= 0.0) return a + ab * (d1 / (d1 - d3));

    const Vec3 cp = p - c;
    const double d5 = dot(ab, cp);
    const double d6 = dot(ac, cp);
    if (d6 >= 0.0 && d5 <= d6) return c;

    const double vb = d5 * d2 - d1 * d6;
    if (vb <= 0.0 && d2 >= 0.0 && d6 <= 0.0) return a + ac * (d2 / (d2 - d6));

    const double va = d3 * d6 - d5 * d4;
    if (va <= 0.0 && (d4 - d3) >= 0.0 && (d5 - d6) >= 0.0)
        return b + (c - b) * ((d4 - d3) / ((d4 - d3) + (d5 - d6)));

    const double denominator = va + vb + vc;
    if (std::abs(denominator) < 1e-300) return a; // degenerate: any corner will do
    const double v = vb / denominator;
    const double w = vc / denominator;
    return a + ab * v + ac * w;
}

/// Squared distance from @p p to the box, zero inside it.
double boxDistanceSquared(const Vec3& p, const Vec3& min, const Vec3& max)
{
    double total = 0.0;
    for (int axis = 0; axis < 3; ++axis) {
        const double v = axisOf(p, axis);
        const double lo = axisOf(min, axis);
        const double hi = axisOf(max, axis);
        const double d = v < lo ? lo - v : (v > hi ? v - hi : 0.0);
        total += d * d;
    }
    return total;
}

} // namespace

MeshQuery::MeshQuery(const TriMesh& mesh)
{
    if (mesh.indices.size() < 3) return;

    m_positions.reserve(mesh.positions.size());
    for (const QVector3D& position : mesh.positions) m_positions.push_back(Vec3::fromVector(position));
    m_indices = mesh.indices;

    const auto triangles = static_cast<std::uint32_t>(m_indices.size() / 3);
    m_order.resize(triangles);
    std::vector<Vec3> centroids(triangles);
    for (std::uint32_t t = 0; t < triangles; ++t) {
        m_order[t] = t;
        centroids[t] = (corner(t, 0) + corner(t, 1) + corner(t, 2)) / 3.0;
    }

    // A split only happens above four triangles, so every leaf holds at least
    // two and a binary tree over them has fewer nodes than there are triangles.
    // Reserving that keeps the vector from growing while the tree is built.
    m_nodes.reserve(std::size_t(triangles) + 1);
    m_nodes.push_back(Node{});
    build(0, 0, triangles, centroids);
}

void MeshQuery::build(std::uint32_t node, std::uint32_t begin, std::uint32_t end,
                      const std::vector<Vec3>& centroids)
{
    Vec3 min(std::numeric_limits<double>::max(), std::numeric_limits<double>::max(),
             std::numeric_limits<double>::max());
    Vec3 max = -min;
    Vec3 centreMin = min;
    Vec3 centreMax = max;
    for (std::uint32_t i = begin; i < end; ++i) {
        const std::uint32_t t = m_order[i];
        for (int k = 0; k < 3; ++k) {
            min = minOf(min, corner(t, k));
            max = maxOf(max, corner(t, k));
        }
        centreMin = minOf(centreMin, centroids[t]);
        centreMax = maxOf(centreMax, centroids[t]);
    }
    m_nodes[node].min = min;
    m_nodes[node].max = max;

    const std::uint32_t count = end - begin;
    const Vec3 spread = centreMax - centreMin;
    const int axis = (spread.x >= spread.y && spread.x >= spread.z) ? 0 : (spread.y >= spread.z ? 1 : 2);
    if (count <= kLeafSize || axisOf(spread, axis) <= 0.0) {
        // Few enough to test outright -- or every centre in one spot, which no
        // split can separate.
        m_nodes[node].first = begin;
        m_nodes[node].count = count;
        return;
    }

    // Split at the median centre along the widest spread, which keeps the tree
    // balanced whatever the tessellation is doing.
    const std::uint32_t middle = begin + count / 2;
    std::nth_element(m_order.begin() + begin, m_order.begin() + middle, m_order.begin() + end,
                     [&](std::uint32_t a, std::uint32_t b) {
                         return axisOf(centroids[a], axis) < axisOf(centroids[b], axis);
                     });

    const auto left = static_cast<std::uint32_t>(m_nodes.size());
    m_nodes.push_back(Node{});
    m_nodes.push_back(Node{});
    m_nodes[node].first = left;
    m_nodes[node].count = 0;
    build(left, begin, middle, centroids);
    build(left + 1, middle, end, centroids);
}

std::optional<RayHit> MeshQuery::castRay(const Vec3& origin, const Vec3& direction,
                                         double maxDistance) const
{
    if (isEmpty()) return std::nullopt;
    const Vec3 unit = direction.normalized();
    if (unit.lengthSquared() < 0.5) return std::nullopt;

    const Vec3 inverse(1.0 / unit.x, 1.0 / unit.y, 1.0 / unit.z);
    RayHit best;
    best.distance = maxDistance;
    bool found = false;

    std::vector<std::uint32_t> stack;
    stack.reserve(64);
    stack.push_back(0);
    while (!stack.empty()) {
        const Node& node = m_nodes[stack.back()];
        stack.pop_back();

        double entry = 0.0;
        if (!rayBox(origin, inverse, node.min, node.max, best.distance, &entry)) continue;

        if (node.count > 0) {
            for (std::uint32_t i = node.first; i < node.first + node.count; ++i) {
                const std::uint32_t t = m_order[i];
                const double distance =
                    rayTriangle(origin, unit, corner(t, 0), corner(t, 1), corner(t, 2));
                // Not the surface the ray starts on: a pivot ray leaves a ball
                // joint that may sit exactly on a face.
                if (distance > 1e-9 && distance < best.distance) {
                    best.distance = distance;
                    best.triangle = static_cast<int>(t);
                    found = true;
                }
            }
            continue;
        }
        stack.push_back(node.first);
        stack.push_back(node.first + 1);
    }

    if (!found) return std::nullopt;
    best.point = origin + unit * best.distance;
    return best;
}

double MeshQuery::distanceTo(const Vec3& point) const
{
    if (isEmpty()) return std::numeric_limits<double>::infinity();

    double best = std::numeric_limits<double>::infinity();
    std::vector<std::pair<double, std::uint32_t>> stack;
    stack.reserve(64);
    stack.push_back({ boxDistanceSquared(point, m_nodes[0].min, m_nodes[0].max), 0 });

    while (!stack.empty()) {
        const auto [boxDistance, index] = stack.back();
        stack.pop_back();
        if (boxDistance >= best) continue;

        const Node& node = m_nodes[index];
        if (node.count > 0) {
            for (std::uint32_t i = node.first; i < node.first + node.count; ++i) {
                const std::uint32_t t = m_order[i];
                const Vec3 nearest = closestOnTriangle(point, corner(t, 0), corner(t, 1), corner(t, 2));
                best = std::min(best, distanceSquared(point, nearest));
            }
            continue;
        }

        // The nearer child last, so it is the next one looked at -- and the
        // farther one is then usually pruned without being opened.
        const Node& a = m_nodes[node.first];
        const Node& b = m_nodes[node.first + 1];
        const double da = boxDistanceSquared(point, a.min, a.max);
        const double db = boxDistanceSquared(point, b.min, b.max);
        if (da < db) {
            stack.push_back({ db, node.first + 1 });
            stack.push_back({ da, node.first });
        } else {
            stack.push_back({ da, node.first });
            stack.push_back({ db, node.first + 1 });
        }
    }
    return std::sqrt(best);
}

} // namespace suspkin
