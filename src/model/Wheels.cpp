#include "model/Wheels.h"

#include <QCoreApplication>

#include <algorithm>
#include <cmath>

namespace suspkin {
namespace {

QString tr(const char* text) { return QCoreApplication::translate("Wheels", text); }

/// A name with everything a workbook uses to separate words taken out, so
/// "F_Wheel_Center", "f wheel centre" and "FWheelCenter" all match the same way.
QString squashed(const QString& name)
{
    QString out;
    out.reserve(name.size());
    for (const QChar c : name) {
        if (c.isLetterOrNumber()) out.append(c.toLower());
    }
    return out;
}

/// Whether a name reads as a wheel centre. Loose on purpose: there is no
/// convention, and the user confirms the guess in the dialog anyway.
bool looksLikeWheelCenter(const QString& name)
{
    const QString key = squashed(name);
    if (key.contains(QLatin1String("wheel")) && key.contains(QLatin1String("cent"))) return true;
    // The workbooks this was written for are German as often as not.
    return key.contains(QLatin1String("rad")) && key.contains(QLatin1String("mitte"));
}

} // namespace

// ---------------------------------------------------------------------------
// Corners and sides
// ---------------------------------------------------------------------------

bool isLeftWheel(WheelCorner corner)
{
    return corner == WheelCorner::FrontLeft || corner == WheelCorner::RearLeft;
}

bool isFrontWheel(WheelCorner corner)
{
    return corner == WheelCorner::FrontLeft || corner == WheelCorner::FrontRight;
}

QString wheelCornerToString(WheelCorner corner)
{
    switch (corner) {
    case WheelCorner::FrontLeft:  return QStringLiteral("frontLeft");
    case WheelCorner::FrontRight: return QStringLiteral("frontRight");
    case WheelCorner::RearLeft:   return QStringLiteral("rearLeft");
    case WheelCorner::RearRight:  break;
    }
    return QStringLiteral("rearRight");
}

WheelCorner wheelCornerFromString(const QString& text, WheelCorner fallback)
{
    for (const WheelCorner corner : kWheelCorners) {
        if (wheelCornerToString(corner).compare(text, Qt::CaseInsensitive) == 0) return corner;
    }
    return fallback;
}

QString wheelCornerLabel(WheelCorner corner)
{
    switch (corner) {
    case WheelCorner::FrontLeft:  return tr("Front left");
    case WheelCorner::FrontRight: return tr("Front right");
    case WheelCorner::RearLeft:   return tr("Rear left");
    case WheelCorner::RearRight:  break;
    }
    return tr("Rear right");
}

QString wheelModelSideToString(WheelModelSide side)
{
    switch (side) {
    case WheelModelSide::Left:      return QStringLiteral("left");
    case WheelModelSide::Right:     return QStringLiteral("right");
    case WheelModelSide::Symmetric: break;
    }
    return QStringLiteral("symmetric");
}

WheelModelSide wheelModelSideFromString(const QString& text, WheelModelSide fallback)
{
    if (text.compare(QLatin1String("left"), Qt::CaseInsensitive) == 0)
        return WheelModelSide::Left;
    if (text.compare(QLatin1String("right"), Qt::CaseInsensitive) == 0)
        return WheelModelSide::Right;
    if (text.compare(QLatin1String("symmetric"), Qt::CaseInsensitive) == 0)
        return WheelModelSide::Symmetric;
    return fallback;
}

// ---------------------------------------------------------------------------
// WheelSpec
// ---------------------------------------------------------------------------

const QString& WheelSpec::point(WheelCorner corner) const
{
    return points[static_cast<std::size_t>(corner)];
}

void WheelSpec::setPoint(WheelCorner corner, const QString& name)
{
    points[static_cast<std::size_t>(corner)] = name;
}

bool WheelSpec::isEmpty() const { return namedCount() == 0; }

int WheelSpec::namedCount() const
{
    int count = 0;
    for (const QString& name : points) {
        if (!name.isEmpty()) ++count;
    }
    return count;
}

bool WheelSpec::operator==(const WheelSpec& other) const
{
    return points == other.points && modelSide == other.modelSide
           && alignToCenter == other.alignToCenter;
}

// ---------------------------------------------------------------------------
// Placing
// ---------------------------------------------------------------------------

std::vector<WheelPlacement> resolveWheels(const WheelSpec& spec, const HardpointTable& table,
                                          QStringList* warnings)
{
    std::vector<WheelPlacement> placements;
    placements.reserve(kWheelCornerCount);

    for (const WheelCorner corner : kWheelCorners) {
        const QString& name = spec.point(corner);
        if (name.isEmpty()) continue; // a corner the user did not pick

        const Hardpoint* point = table.find(name);
        if (!point) {
            // Not an error: the workbook may have been replaced by one that
            // names its points differently, and the rest still draws.
            if (warnings) {
                *warnings << tr("The %1 wheel is centred on \"%2\", which is not in the "
                                "hardpoint table.")
                                 .arg(wheelCornerLabel(corner).toLower(), name);
            }
            continue;
        }

        WheelPlacement placement;
        placement.corner = corner;
        placement.pointName = name;
        placement.center = point->toVector();
        switch (spec.modelSide) {
        case WheelModelSide::Left:      placement.mirrored = !isLeftWheel(corner); break;
        case WheelModelSide::Right:     placement.mirrored = isLeftWheel(corner); break;
        case WheelModelSide::Symmetric: placement.mirrored = false; break;
        }
        placements.push_back(placement);
    }

    return placements;
}

void orientWheels(std::vector<WheelPlacement>& placements, const WheelRotations& rotations)
{
    for (WheelPlacement& placement : placements) {
        const auto it = rotations.constFind(placement.pointName);
        if (it == rotations.constEnd()) continue;
        placement.rotation = *it;
    }
}

QMatrix4x4 wheelTransform(const WheelPlacement& placement, const Aabb& modelBounds,
                          bool alignToCenter)
{
    // The point of the model that lands on the hardpoint, and the plane the far
    // side is mirrored about. An empty box has no centre to use, so such a model
    // is placed by its origin whatever was asked for.
    const QVector3D anchor =
        (alignToCenter && !modelBounds.isEmpty()) ? modelBounds.center() : QVector3D();

    QMatrix4x4 transform;
    transform.translate(placement.center);
    // The upright's own turn, about the wheel centre: steering and camber, and
    // on the far side the far side's, which is why it is applied after the
    // mirror rather than being mirrored with the model.
    if (!placement.rotation.isIdentity()) transform.rotate(placement.rotation);
    // Y, because that is what "the other side of the car" negates in ISO 8855.
    if (placement.mirrored) transform.scale(1.0f, -1.0f, 1.0f);
    transform.translate(-anchor);
    return transform;
}

Aabb transformedBounds(const Aabb& bounds, const QMatrix4x4& transform)
{
    Aabb out;
    if (bounds.isEmpty()) return out;

    // All eight corners, because a mirror or any other rotation moves which of
    // them is the extreme one.
    for (int corner = 0; corner < 8; ++corner) {
        const QVector3D point((corner & 1) ? bounds.max.x() : bounds.min.x(),
                              (corner & 2) ? bounds.max.y() : bounds.min.y(),
                              (corner & 4) ? bounds.max.z() : bounds.min.z());
        out.expand(transform.map(point));
    }
    return out;
}

Aabb wheelBounds(const std::vector<WheelPlacement>& placements, const Aabb& modelBounds,
                 bool alignToCenter)
{
    Aabb out;
    if (modelBounds.isEmpty()) return out;

    for (const WheelPlacement& placement : placements) {
        const Aabb placed =
            transformedBounds(modelBounds, wheelTransform(placement, modelBounds, alignToCenter));
        if (placed.isEmpty()) continue;
        out.expand(placed.min);
        out.expand(placed.max);
    }
    return out;
}

// ---------------------------------------------------------------------------
// Guessing
// ---------------------------------------------------------------------------

WheelSpec guessWheelSpec(const HardpointTable& table)
{
    WheelSpec spec;

    std::vector<const Hardpoint*> candidates;
    for (const Hardpoint& point : table.points) {
        if (looksLikeWheelCenter(point.name)) candidates.push_back(&point);
    }
    if (candidates.empty()) return spec;

    // Which axle a candidate is on comes from where it is, not from its name:
    // +x is forward. With every candidate at the same station there is nothing
    // to tell front from rear, so they all go to the front row and the user
    // moves them.
    float minX = candidates.front()->toVector().x();
    float maxX = minX;
    for (const Hardpoint* point : candidates) {
        minX = std::min(minX, point->toVector().x());
        maxX = std::max(maxX, point->toVector().x());
    }
    const float midX = (minX + maxX) * 0.5f;
    const bool splitByAxle = (maxX - minX) > 1.0f; // millimetres

    for (const Hardpoint* point : candidates) {
        const QVector3D position = point->toVector();
        const bool front = !splitByAxle || position.x() >= midX;
        const bool left = position.y() >= 0.0f; // +y is left
        const WheelCorner corner = front ? (left ? WheelCorner::FrontLeft : WheelCorner::FrontRight)
                                         : (left ? WheelCorner::RearLeft : WheelCorner::RearRight);
        // First one wins: a table with two candidates for a corner is a table
        // this cannot guess, and overwriting would just pick the last row.
        if (spec.point(corner).isEmpty()) spec.setPoint(corner, point->name);
    }

    return spec;
}

} // namespace suspkin
