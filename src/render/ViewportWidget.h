#pragma once

#include "geom/MeshTopology.h"
#include "geom/TriMesh.h"
#include "model/Hardpoint.h"
#include "render/Camera.h"
#include "render/DisplayMode.h"
#include "render/GpuMesh.h"
#include "render/GpuPoints.h"
#include "render/ModeSelector.h"
#include "render/NavGizmo.h"

#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLWidget>
#include <QImage>
#include <QPoint>
#include <QRectF>
#include <QString>
#include <QStringList>

#include <memory>
#include <vector>

class QOpenGLShaderProgram;

namespace suspkin {

/// The 3D viewport: an OpenGL 3.3 core-profile surface that draws one mesh as a
/// shaded solid, optionally with its triangle tessellation on top, the
/// hardpoint markers and their labels, plus the navigation gizmo and the mode
/// selector in the top-right corner.
class ViewportWidget : public QOpenGLWidget, protected QOpenGLFunctions_3_3_Core {
    Q_OBJECT

public:
    explicit ViewportWidget(QWidget* parent = nullptr);
    ~ViewportWidget() override;

    void setMesh(TriMesh mesh, EdgeSet edges);
    void clearMesh();
    bool hasMesh() const { return !m_mesh.isEmpty(); }

    /// Replace the hardpoints on screen. Selection is dropped, because the
    /// indices it refers to belong to the previous set.
    void setHardpoints(const HardpointTable& table);
    void clearHardpoints();
    bool hasHardpoints() const { return !m_hardpoints.empty(); }

    /// Move a single marker, leaving the rest of the buffer alone. This is the
    /// path taken while a coordinate is being typed, so it has to stay cheap.
    void moveHardpoint(int index, const QVector3D& position);

    void setHardpointLabelsVisible(bool visible);
    bool hardpointLabelsVisible() const { return m_labelsVisible; }

    void setSelectedHardpoint(int index);
    int selectedHardpoint() const { return m_selectedPoint; }

    void setDisplayMode(DisplayMode mode);
    DisplayMode displayMode() const { return m_mode; }

    void fitToView();
    void applyPreset(ViewPreset preset);

signals:
    /// GL vendor/renderer/version once the context is live, or the reason it is not.
    void contextReady(const QString& description);
    /// The mode changed from inside the viewport (the overlay buttons).
    void displayModeChanged(DisplayMode mode);
    /// A marker was clicked, or empty space was, which clears to -1.
    void hardpointClicked(int index);

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;

    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    /// One label, already placed in widget coordinates.
    struct Label {
        QRectF rect;
        QString text;
        int index = -1;
        QPointF anchor; ///< the marker the leader line points back to
    };

    void renderScene();
    void renderHardpoints(const QMatrix4x4& mvp);
    /// Paint the corner furniture and the labels with the raster engine.
    QImage renderChrome(const QRectF& area, const std::vector<Label>& labels) const;
    std::vector<Label> layoutLabels() const;
    QRectF chromeArea(const std::vector<Label>& labels) const;
    bool buildPrograms();
    void layoutChrome();
    float aspect() const;

    /// Project a world point to widget coordinates. False when it falls behind
    /// the eye or outside the depth range, where there is nothing to draw.
    bool project(const QMatrix4x4& viewProjection, const QVector3D& world, QPointF* screen,
                 float* depth) const;
    /// Index of the marker under @p pos, or -1.
    int hardpointAt(const QPoint& pos) const;

    TriMesh m_mesh;
    EdgeSet m_edges;
    GpuMesh m_gpu;
    bool m_uploadPending = false;

    std::vector<QVector3D> m_hardpoints;
    QStringList m_hardpointNames;
    Aabb m_hardpointBounds;
    GpuPoints m_gpuPoints;
    bool m_pointsUploadPending = false;
    bool m_labelsVisible = true;
    int m_selectedPoint = -1;
    int m_hoveredPoint = -1;
    int m_pressedPoint = -1;

    Camera m_camera;
    DisplayMode m_mode = DisplayMode::Solid;

    std::unique_ptr<QOpenGLShaderProgram> m_solidProgram;
    std::unique_ptr<QOpenGLShaderProgram> m_lineProgram;
    std::unique_ptr<QOpenGLShaderProgram> m_pointProgram;

    /// Non-empty when the context is unusable; the scene pass then does nothing.
    QString m_contextError;

    NavGizmo m_gizmo;
    ModeSelector m_modeSelector;
    int m_hoveredAxis = -1;
    int m_hoveredButton = -1;
    int m_pressedButton = -1;

    enum class Drag { None, Orbit, Pan, Gizmo };
    Drag m_drag = Drag::None;
    QPoint m_lastPos;
    QPoint m_pressPos;
    int m_pressedAxis = -1;
};

} // namespace suspkin
