#pragma once

#include "geom/Aabb.h"
#include "model/Hardpoint.h"

#include <QMatrix4x4>
#include <QString>
#include <QStringList>
#include <QVector3D>

#include <array>
#include <vector>

namespace suspkin {

/// The four places a wheel goes, in the order the dialog lists them.
///
/// It is the corner, not the sign of the hardpoint's y, that decides which side
/// of the car a copy of the model is on. The user says which point is the left
/// front one; a workbook measured in somebody else's frame should still come out
/// with its wheels facing the right way.
enum class WheelCorner { FrontLeft, FrontRight, RearLeft, RearRight };

inline constexpr int kWheelCornerCount = 4;
inline constexpr std::array<WheelCorner, kWheelCornerCount> kWheelCorners = {
    WheelCorner::FrontLeft,
    WheelCorner::FrontRight,
    WheelCorner::RearLeft,
    WheelCorner::RearRight,
};

/// Which side of the car a corner is on, in the ISO 8855 sense: left is +y.
bool isLeftWheel(WheelCorner corner);
/// Which axle, for the same reason: front is +x.
bool isFrontWheel(WheelCorner corner);

/// Stable identifiers for the project file. Round-trip safe: a spec written by
/// one release reads back the same in the next.
QString wheelCornerToString(WheelCorner corner);
WheelCorner wheelCornerFromString(const QString& text,
                                  WheelCorner fallback = WheelCorner::FrontLeft);
/// What the corner is called in front of the user.
QString wheelCornerLabel(WheelCorner corner);

/// Which side of the car the imported models are drawn for.
///
/// A rim is not symmetric -- it is dished, and its offset faces one way -- so
/// one model cannot simply be dropped onto all four centres. The copies on the
/// far side are drawn as its mirror image instead. A model that really is
/// symmetric, or one drawn about its own centre plane, says so with @c Symmetric
/// and is never mirrored.
enum class WheelModelSide { Left, Right, Symmetric };

QString wheelModelSideToString(WheelModelSide side);
WheelModelSide wheelModelSideFromString(const QString& text,
                                        WheelModelSide fallback = WheelModelSide::Left);

/// Where the wheel models go and how they are placed there.
///
/// The user's own choice about their own car, so the project keeps it. It names
/// hardpoints rather than coordinates: a wheel centre that gets edited in the
/// table takes its wheel with it.
struct WheelSpec {
    /// The hardpoint each corner is centred on, empty for a corner this car does
    /// not have or the user did not pick.
    std::array<QString, kWheelCornerCount> points;
    WheelModelSide modelSide = WheelModelSide::Left;
    /// Put the centre of the model's bounding box on the hardpoint. That is what
    /// a wheel drawn about its own origin, or anywhere else, needs. Turn it off
    /// for a model already positioned in vehicle coordinates, which is then
    /// placed by its own origin instead.
    bool alignToCenter = true;

    const QString& point(WheelCorner corner) const;
    void setPoint(WheelCorner corner, const QString& name);

    /// True when no corner has been named, so there is nothing to place.
    bool isEmpty() const;
    int namedCount() const;

    bool operator==(const WheelSpec& other) const;
    bool operator!=(const WheelSpec& other) const { return !(*this == other); }
};

/// One placed copy of the models: which corner it is, and where.
struct WheelPlacement {
    WheelCorner corner = WheelCorner::FrontLeft;
    QString pointName;
    QVector3D center;
    /// Drawn as the mirror image of the model, because this corner is on the
    /// side the model was not drawn for.
    bool mirrored = false;
};

/// The placements @p spec asks for that @p table can supply.
///
/// A corner whose hardpoint is not in the table is dropped and named in
/// @p warnings rather than being an error: a workbook may hold one axle, or the
/// far side may not have been mirrored yet.
std::vector<WheelPlacement> resolveWheels(const WheelSpec& spec, const HardpointTable& table,
                                          QStringList* warnings = nullptr);

/// Model to world for one placement: the model's anchor -- the centre of
/// @p modelBounds, or its own origin -- moved onto the wheel centre, mirrored
/// across the plane through that anchor when the placement is on the far side.
QMatrix4x4 wheelTransform(const WheelPlacement& placement, const Aabb& modelBounds,
                          bool alignToCenter);

/// The box the model occupies once placed. Empty in, empty out.
Aabb transformedBounds(const Aabb& bounds, const QMatrix4x4& transform);

/// Every placed copy of @p modelBounds together, which is what the camera has to
/// frame to have the wheels on screen.
Aabb wheelBounds(const std::vector<WheelPlacement>& placements, const Aabb& modelBounds,
                 bool alignToCenter);

/// A first guess at which hardpoints are the wheel centres.
///
/// Names are matched loosely because there is no convention for them, and the
/// corner each one belongs to is then decided by where the point actually is:
/// +x is forward and +y is left, so the four candidates sort themselves. It is
/// only ever a starting point for the dialog -- the user confirms it.
WheelSpec guessWheelSpec(const HardpointTable& table);

} // namespace suspkin
