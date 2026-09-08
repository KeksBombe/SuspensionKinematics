#pragma once

#include "geom/Aabb.h"

#include <QMatrix4x4>
#include <QVector3D>

namespace suspkin {

enum class ViewPreset { Front, Rear, Left, Right, Top, Bottom, Isometric };

/// Turntable camera orbiting a pivot, with a permanently fixed world up.
///
/// A turntable rather than an arcball: an arcball lets the model roll to an
/// arbitrary orientation, which destroys any sense of which way is up -- and
/// "up" is exactly what camber and caster are read against. It also matches
/// what SolidWorks, Fusion and Onshape do, so it needs no learning.
///
/// Coordinate convention: ISO 8855 / DIN 70000 -- X forward, Y left, Z up,
/// right-handed. This is the convention the vehicle-dynamics literature uses, so
/// the sign of camber and toe will come out consistent when the solver lands.
class Camera {
public:
    static QVector3D worldUp() { return QVector3D(0.0f, 0.0f, 1.0f); }

    void orbit(float dxPixels, float dyPixels);
    void pan(float dxPixels, float dyPixels, int viewportHeightPixels);
    void zoom(float wheelSteps);

    /// Frame @p bounds, accounting for whichever screen axis is tighter.
    void fitTo(const Aabb& bounds, float aspect);
    void applyPreset(ViewPreset preset);

    QVector3D eye() const;
    QVector3D forward() const;
    QVector3D right() const;
    QVector3D up() const;

    QMatrix4x4 viewMatrix() const;
    QMatrix4x4 projectionMatrix(float aspect) const;

private:
    QVector3D m_pivot{ 0.0f, 0.0f, 0.0f };
    float m_distance     = 5.0f;
    float m_azimuthDeg   = -45.0f; ///< about +Z, measured from +X
    float m_elevationDeg = 30.0f;  ///< from the XY plane
    float m_fovYDeg      = 45.0f;
    float m_sceneRadius  = 1.0f;   ///< set by fitTo(); scales zoom limits and near/far
};

} // namespace suspkin
