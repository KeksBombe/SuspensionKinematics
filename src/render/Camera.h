#pragma once

#include "geom/Aabb.h"

#include <QMatrix4x4>
#include <QPointF>
#include <QSize>
#include <QVector3D>

namespace suspkin {

enum class ViewPreset { Front, Rear, Left, Right, Top, Bottom, Isometric };

/// The camera's whole orientation, in the terms it actually stores rather than
/// as a matrix. A project keeps this so reopening it puts the user back exactly
/// where they were looking, and a matrix would not survive a later change to
/// how the camera derives one.
struct CameraState {
    QVector3D pivot{ 0.0f, 0.0f, 0.0f };
    float distance = 5.0f;
    float azimuthDeg = -45.0f;
    float elevationDeg = 30.0f;
    float fovYDeg = 45.0f;
    float sceneRadius = 1.0f;
};

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
    QMatrix4x4 viewProjectionMatrix(float aspect) const;

    /// Where @p world lands in a widget of @p viewport pixels, and how deep it
    /// is in clip space. False when it falls behind the eye or outside the
    /// depth range, where there is nothing to draw and nothing to click.
    ///
    /// Static, and taking the matrix rather than deriving one, because the
    /// callers that matter project a whole table of points against one camera:
    /// the markers, their labels, and the arrows on the selected one. They all
    /// have to land in the same place, so there is one of these rather than a
    /// copy of the arithmetic per caller.
    static bool projectTo(const QMatrix4x4& viewProjection, const QVector3D& world,
                          const QSize& viewport, QPointF* screen, float* depth);

    CameraState state() const;
    /// Restore a saved view. Values that would make the camera unusable -- a
    /// non-positive distance or radius, an elevation at the pole -- are clamped
    /// rather than rejected, so a hand-edited project file cannot produce a
    /// black viewport.
    void setState(const CameraState& state);

private:
    QVector3D m_pivot{ 0.0f, 0.0f, 0.0f };
    float m_distance     = 5.0f;
    float m_azimuthDeg   = -45.0f; ///< about +Z, measured from +X
    float m_elevationDeg = 30.0f;  ///< from the XY plane
    float m_fovYDeg      = 45.0f;
    float m_sceneRadius  = 1.0f;   ///< set by fitTo(); scales zoom limits and near/far
};

} // namespace suspkin
