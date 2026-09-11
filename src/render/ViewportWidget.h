#pragma once

#include "geom/MeshTopology.h"
#include "geom/TriMesh.h"
#include "model/Hardpoint.h"
#include "model/Linkage.h"
#include "model/Wheels.h"
#include "render/Camera.h"
#include "render/DisplayMode.h"
#include "render/GpuLines.h"
#include "render/GpuMesh.h"
#include "render/GpuPoints.h"
#include "render/ModeSelector.h"
#include "render/NavGizmo.h"

#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLWidget>
#include <QImage>
#include <QMatrix4x4>
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
/// hardpoint markers and their labels, a copy of the wheel and rim models at
/// each wheel centre, plus the navigation gizmo and the mode selector in the
/// top-right corner.
class ViewportWidget : public QOpenGLWidget, protected QOpenGLFunctions_3_3_Core {
    Q_OBJECT

public:
    explicit ViewportWidget(QWidget* parent = nullptr);
    ~ViewportWidget() override;

    void setMesh(TriMesh mesh, EdgeSet edges);
    void clearMesh();
    bool hasMesh() const { return !m_mesh.isEmpty(); }

    /// Draw the imported geometry moved by @p transform rather than where its
    /// file put it. It is the chassis, so when the body rolls it goes with it;
    /// identity the rest of the time. The mesh itself is untouched -- this is a
    /// model matrix, so a live slider costs nothing but a repaint.
    void setMeshTransform(const QMatrix4x4& transform);

    /// Replace the hardpoints on screen. Selection is dropped, because the
    /// indices it refers to belong to the previous set.
    void setHardpoints(const HardpointTable& table);
    void clearHardpoints();
    bool hasHardpoints() const { return !m_hardpoints.empty(); }

    /// Move a single marker, leaving the rest of the buffer alone. This is the
    /// path taken while a coordinate is being typed, so it has to stay cheap.
    void moveHardpoint(int index, const QVector3D& position);

    /// Move every marker at once, keeping the names, the selection and the parts.
    ///
    /// This is how the solver puts the suspension somewhere other than where the
    /// workbook has it: the table is the same table, only posed. Going through
    /// setHardpoints() instead would drop the linkage and the selection on every
    /// frame of a travel slider, which is exactly what must not happen.
    /// @p positions has to be as long as the table that was set; a mismatch is
    /// ignored rather than half-applied.
    void setHardpointPositions(const std::vector<QVector3D>& positions);

    /// Replace the parts drawn between the markers. The indices it holds are
    /// into the hardpoint table, so it has to be set after the points it refers
    /// to, never before.
    void setLinkage(const Linkage& linkage);
    void clearLinkage();
    bool hasLinkage() const { return !m_linkage.isEmpty(); }

    void setHardpointLabelsVisible(bool visible);
    bool hardpointLabelsVisible() const { return m_labelsVisible; }

    /// Replace the wheel and rim models drawn at the wheel centres. Either may
    /// be empty. Kept apart from the placements because this is the expensive
    /// half -- it re-uploads two meshes -- and the placements are not.
    void setWheelModels(TriMesh wheel, EdgeSet wheelEdges, TriMesh rim, EdgeSet rimEdges);
    /// Where the copies of those models go. A handful of matrices, so this is
    /// the path a wheel centre being typed into the table takes.
    void setWheelPlacements(std::vector<WheelPlacement> placements, bool alignToCenter);
    void clearWheels();
    bool hasWheels() const { return !m_wheelPlacements.empty() && hasWheelModels(); }

    void setLinkageVisible(bool visible);
    bool linkageVisible() const { return m_linkageVisible; }

    void setWheelsVisible(bool visible);
    bool wheelsVisible() const { return m_wheelsVisible; }

    void setSelectedHardpoint(int index);
    int selectedHardpoint() const { return m_selectedPoint; }

    void setDisplayMode(DisplayMode mode);
    DisplayMode displayMode() const { return m_mode; }

    void fitToView();
    void applyPreset(ViewPreset preset);

    /// The camera's saved position, for the project file.
    CameraState cameraState() const { return m_camera.state(); }
    void setCameraState(const CameraState& state);

signals:
    /// GL vendor/renderer/version once the context is live, or the reason it is not.
    void contextReady(const QString& description);
    /// The mode changed from inside the viewport (the overlay buttons).
    void displayModeChanged(DisplayMode mode);
    /// A marker was clicked, or empty space was, which clears to -1.
    void hardpointClicked(int index);
    /// Anything a project remembers about the view changed: the camera moved,
    /// the mode or the labels were toggled, a marker was selected. Emitted on
    /// every orbit step, so anything listening has to be cheap or debounced.
    void viewChanged();

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
    /// Draw each placed copy of the wheel models. @p view is needed as well as
    /// @p mvp because every copy has its own model matrix, and the solid shader
    /// lights in view space.
    void renderWheels(const QMatrix4x4& view, const QMatrix4x4& mvp);
    void renderLinkage(const QMatrix4x4& mvp);
    void renderHardpoints(const QMatrix4x4& mvp);
    /// Turn the linkage's point indices into segment endpoints, grouped so that
    /// one draw call covers each kind of part.
    void rebuildLinkageVertices();
    /// Paint the corner furniture and the labels with the raster engine.
    QImage renderChrome(const QRectF& area, const std::vector<Label>& labels) const;
    std::vector<Label> layoutLabels() const;
    QRectF chromeArea(const std::vector<Label>& labels) const;
    bool buildPrograms();
    void layoutChrome();
    bool hasWheelModels() const { return !m_wheelMesh.isEmpty() || !m_rimMesh.isEmpty(); }
    /// The box the placed wheels occupy, which is what fitToView() has to
    /// include. Recomputed whenever either the models or the placements change.
    void updateWheelBounds();
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
    QMatrix4x4 m_meshTransform;

    /// One kind of part's worth of segment endpoints inside m_linkVertices.
    struct LinkRange {
        PartKind kind = PartKind::Other;
        int first = 0;
        int count = 0;
    };

    std::vector<QVector3D> m_hardpoints;
    QStringList m_hardpointNames;
    Aabb m_hardpointBounds;
    GpuPoints m_gpuPoints;
    bool m_pointsUploadPending = false;

    /// The two wheel bodies. Both are held CPU-side for the same reason the main
    /// mesh is: the context can be lost and rebuilt at any time, and the model
    /// bounds are what every placement is measured from.
    TriMesh m_wheelMesh;
    EdgeSet m_wheelEdges;
    GpuMesh m_wheelGpu;
    TriMesh m_rimMesh;
    EdgeSet m_rimEdges;
    GpuMesh m_rimGpu;
    bool m_wheelUploadPending = false;
    std::vector<WheelPlacement> m_wheelPlacements;
    bool m_wheelAlignToCenter = true;
    bool m_wheelsVisible = true;
    Aabb m_wheelBounds;

    Linkage m_linkage;
    std::vector<QVector3D> m_linkVertices;
    std::vector<LinkRange> m_linkRanges;
    GpuLines m_gpuLines;
    bool m_linesUploadPending = false;
    bool m_linkageVisible = true;
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
