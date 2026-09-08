#include "render/Camera.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace suspkin {
namespace {

constexpr float kDegToRad = static_cast<float>(std::numbers::pi) / 180.0f;
/// Never exactly 90 degrees: at the pole the view direction is parallel to the
/// world up and the right vector collapses.
constexpr float kMaxElevationDeg = 89.9f;
constexpr float kOrbitDegPerPixel = 0.4f;
constexpr float kZoomFactorPerStep = 0.9f;

} // namespace

QVector3D Camera::eye() const
{
    const float az = m_azimuthDeg * kDegToRad;
    const float el = m_elevationDeg * kDegToRad;
    const float ce = std::cos(el);
    return m_pivot + m_distance * QVector3D(ce * std::cos(az), ce * std::sin(az), std::sin(el));
}

QVector3D Camera::forward() const { return (m_pivot - eye()).normalized(); }
QVector3D Camera::right() const { return QVector3D::crossProduct(forward(), worldUp()).normalized(); }
QVector3D Camera::up() const { return QVector3D::crossProduct(right(), forward()); }

void Camera::orbit(float dxPixels, float dyPixels)
{
    // The camera moves opposite the drag, so the model appears to follow the cursor.
    m_azimuthDeg -= dxPixels * kOrbitDegPerPixel;
    m_elevationDeg += dyPixels * kOrbitDegPerPixel;
    m_elevationDeg = std::clamp(m_elevationDeg, -kMaxElevationDeg, kMaxElevationDeg);
    // Keep azimuth bounded so precision does not drift away over a long drag.
    m_azimuthDeg = std::fmod(m_azimuthDeg, 360.0f);
}

void Camera::pan(float dxPixels, float dyPixels, int viewportHeightPixels)
{
    if (viewportHeightPixels <= 0) return;

    // World units per pixel at the pivot's depth, so the geometry under the
    // cursor tracks it one to one.
    const float unitsPerPixel = 2.0f * m_distance * std::tan(0.5f * m_fovYDeg * kDegToRad)
                              / static_cast<float>(viewportHeightPixels);
    // +dy because screen Y grows downward while the camera up vector grows up.
    m_pivot += (-right() * dxPixels + up() * dyPixels) * unitsPerPixel;
}

void Camera::zoom(float wheelSteps)
{
    // Dolly, not FOV. Zooming by FOV warps perspective, which would make visual
    // judgement of suspension angles unreliable.
    m_distance *= std::pow(kZoomFactorPerStep, wheelSteps);
    m_distance = std::clamp(m_distance, 1e-3f * m_sceneRadius, 1e3f * m_sceneRadius);
}

void Camera::fitTo(const Aabb& bounds, float aspect)
{
    if (bounds.isEmpty()) return;

    m_pivot = bounds.center();
    const float radius = std::max(0.5f * bounds.diagonal(), 1e-6f);
    m_sceneRadius = radius;

    // Fit against whichever axis is tighter, so a narrow window does not clip.
    const float tanHalfY = std::tan(0.5f * m_fovYDeg * kDegToRad);
    const float tanHalf = (aspect > 0.0f) ? std::min(tanHalfY, aspect * tanHalfY) : tanHalfY;
    const float halfFov = std::atan(tanHalf);

    constexpr float kMargin = 1.1f;
    m_distance = kMargin * radius / std::max(std::sin(halfFov), 1e-4f);
}

void Camera::applyPreset(ViewPreset preset)
{
    switch (preset) {
    case ViewPreset::Front:  m_azimuthDeg =    0.0f; m_elevationDeg = 0.0f; break;
    case ViewPreset::Rear:   m_azimuthDeg =  180.0f; m_elevationDeg = 0.0f; break;
    case ViewPreset::Left:   m_azimuthDeg =   90.0f; m_elevationDeg = 0.0f; break;
    case ViewPreset::Right:  m_azimuthDeg =  -90.0f; m_elevationDeg = 0.0f; break;
    // Azimuth 180 for the plan view so vehicle forward (+X) points up the screen
    // and the vehicle's right side sits on the right, as a plan view is drawn.
    case ViewPreset::Top:    m_azimuthDeg =  180.0f; m_elevationDeg =  kMaxElevationDeg; break;
    case ViewPreset::Bottom: m_azimuthDeg =    0.0f; m_elevationDeg = -kMaxElevationDeg; break;
    case ViewPreset::Isometric: m_azimuthDeg = -45.0f; m_elevationDeg = 30.0f; break;
    }
}

QMatrix4x4 Camera::viewMatrix() const
{
    QMatrix4x4 m;
    m.lookAt(eye(), m_pivot, worldUp());
    return m;
}

QMatrix4x4 Camera::projectionMatrix(float aspect) const
{
    // Derive the planes from the current distance and scene size every frame.
    // A fixed near/far pair cannot serve both a 10 mm bracket and a 3 m chassis
    // without losing most of the depth buffer's precision.
    const float radius = std::max(m_sceneRadius, 1e-6f);
    const float nearPlane = std::max(1e-4f * radius, 0.5f * (m_distance - radius));
    const float farPlane = std::max(m_distance + 3.0f * radius, nearPlane * 1.001f);

    QMatrix4x4 m;
    m.perspective(m_fovYDeg, (aspect > 0.0f) ? aspect : 1.0f, nearPlane, farPlane);
    return m;
}

} // namespace suspkin
