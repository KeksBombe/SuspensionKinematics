#pragma once

#include "geom/MeshTopology.h"
#include "geom/TriMesh.h"
#include "render/Camera.h"
#include "render/DisplayMode.h"
#include "render/GpuMesh.h"
#include "render/ModeSelector.h"
#include "render/NavGizmo.h"

#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLWidget>
#include <QImage>
#include <QPoint>
#include <QString>

#include <memory>

class QOpenGLShaderProgram;

namespace suspkin {

/// The 3D viewport: an OpenGL 3.3 core-profile surface that draws one mesh as a
/// shaded solid, optionally with its triangle tessellation on top, plus the
/// navigation gizmo and the mode selector in the top-right corner.
class ViewportWidget : public QOpenGLWidget, protected QOpenGLFunctions_3_3_Core {
    Q_OBJECT

public:
    explicit ViewportWidget(QWidget* parent = nullptr);
    ~ViewportWidget() override;

    void setMesh(TriMesh mesh, EdgeSet edges);
    void clearMesh();
    bool hasMesh() const { return !m_mesh.isEmpty(); }

    void setDisplayMode(DisplayMode mode);
    DisplayMode displayMode() const { return m_mode; }

    void fitToView();
    void applyPreset(ViewPreset preset);

signals:
    /// GL vendor/renderer/version once the context is live, or the reason it is not.
    void contextReady(const QString& description);
    /// The mode changed from inside the viewport (the overlay buttons).
    void displayModeChanged(DisplayMode mode);

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
    void renderScene();
    /// Paint the corner furniture with the raster engine and return it.
    QImage renderChrome() const;
    bool buildPrograms();
    void layoutChrome();
    float aspect() const;

    TriMesh m_mesh;
    EdgeSet m_edges;
    GpuMesh m_gpu;
    bool m_uploadPending = false;

    Camera m_camera;
    DisplayMode m_mode = DisplayMode::Solid;

    std::unique_ptr<QOpenGLShaderProgram> m_solidProgram;
    std::unique_ptr<QOpenGLShaderProgram> m_lineProgram;

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
