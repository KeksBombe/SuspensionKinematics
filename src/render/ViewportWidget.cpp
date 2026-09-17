#include "render/ViewportWidget.h"

#include <QCursor>
#include <QFontMetricsF>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QOpenGLShaderProgram>
#include <QImage>
#include <QPainter>
#include <QVector4D>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace suspkin {
namespace {

constexpr float kWheelStepsPerNotch = 1.0f / 120.0f; // Qt reports eighths of a degree

// Layout of the top-right corner furniture, in logical pixels.
constexpr int kCornerMargin = 10;
constexpr int kGizmoSize = 84;
constexpr int kGizmoGap = 8;
/// Movement past this many pixels turns a gizmo click into an orbit drag.
constexpr int kClickSlopPixels = 4;

const QVector3D kSurfaceColor{ 0.62f, 0.66f, 0.72f };
// The wheel bodies. Darker than imported geometry so a wheel does not read as
// part of the chassis, and the two of them far enough apart that a rim inside a
// tyre is still a rim.
const QVector3D kWheelColor{ 0.33f, 0.34f, 0.37f };
const QVector3D kRimColor{ 0.78f, 0.80f, 0.84f };
// Light lines on the dark ground, slightly translucent: a CAD export tessellates
// a flat face into a fan of slivers, so blending lets dense regions read as tone
// instead of saturating to a solid mass.
const QVector4D kEdgeColor{ 0.82f, 0.86f, 0.92f, 0.75f };

// Hardpoint markers. Amber reads as "annotation" next to the neutral grey of
// imported geometry, and neither colour is one a shaded surface produces.
const QVector3D kPointColor{ 1.00f, 0.74f, 0.28f };
const QVector3D kHoverColor{ 1.00f, 0.90f, 0.62f };
const QVector3D kSelectedColor{ 0.42f, 0.86f, 1.00f };
const QVector3D kPointRimColor{ 0.10f, 0.09f, 0.07f };
constexpr float kPointSizePx = 11.0f;
constexpr float kHighlightSizePx = 15.0f;
/// How much of a marker still shows through the geometry in front of it.
constexpr float kOccludedAlpha = 0.30f;
/// Click and hover tolerance, in logical pixels, around a marker's centre.
constexpr qreal kPickRadiusPx = 10.0;

// The parts drawn between the markers, by kind. Chosen to stay apart from the
// amber of a marker and the neutral grey of imported geometry, and to group by
// function: everything that is a two-force member shares one colour, and each
// body that carries load in bending gets its own.
const QVector3D kWishboneColor{ 0.42f, 0.72f, 0.98f };
const QVector3D kLinkColor{ 0.55f, 0.88f, 0.52f };
const QVector3D kUprightColor{ 0.98f, 0.55f, 0.38f };
const QVector3D kRockerColor{ 0.80f, 0.62f, 0.98f };
const QVector3D kDamperColor{ 0.98f, 0.86f, 0.36f };
const QVector3D kAntiRollColor{ 0.36f, 0.88f, 0.82f };
const QVector3D kWheelPartColor{ 0.72f, 0.76f, 0.82f };
const QVector3D kOtherPartColor{ 0.80f, 0.80f, 0.80f };
/// Wide enough to read as structure next to the tessellation overlay. GL 3.3
/// core leaves anything above 1.0 up to the driver, so this is a request.
constexpr float kLinkWidthPx = 2.5f;
/// How much of a part still shows through the geometry in front of it. Lower
/// than a marker's: a whole wishbone behind a chassis panel is a lot of ink.
constexpr float kOccludedLinkAlpha = 0.22f;

const QVector3D& partColor(PartKind kind)
{
    switch (kind) {
    case PartKind::Wishbone: return kWishboneColor;
    case PartKind::Link:     return kLinkColor;
    case PartKind::Upright:  return kUprightColor;
    case PartKind::Rocker:   return kRockerColor;
    case PartKind::Damper:   return kDamperColor;
    case PartKind::AntiRoll: return kAntiRollColor;
    case PartKind::Wheel:    return kWheelPartColor;
    case PartKind::Other:    break;
    }
    return kOtherPartColor;
}

constexpr qreal kLabelPadX = 5.0;
constexpr qreal kLabelPadY = 1.0;
/// Gap between a marker and its label, leaving the marker itself uncovered.
constexpr qreal kLabelGap = 10.0;

const QColor kLabelFill{ 18, 20, 24, 200 };
const QColor kLabelText{ 226, 231, 238 };
const QColor kLabelLeader{ 150, 156, 166, 150 };
const QColor kLabelFillSelected{ 12, 42, 56, 225 };
const QColor kLabelTextSelected{ 130, 226, 255 };
const QColor kLabelFillHovered{ 46, 36, 14, 225 };
const QColor kLabelTextHovered{ 255, 214, 140 };

/// The point of @p rect closest to @p from, which is where a leader line should
/// meet the label.
QPointF nearestPointOn(const QRectF& rect, const QPointF& from)
{
    return QPointF(std::clamp(from.x(), rect.left(), rect.right()),
                   std::clamp(from.y(), rect.top(), rect.bottom()));
}

bool addShader(QOpenGLShaderProgram& program, QOpenGLShader::ShaderType type, const QString& path)
{
    if (program.addShaderFromSourceFile(type, path)) return true;
    qWarning("Shader %s failed to compile:\n%s", qPrintable(path), qPrintable(program.log()));
    return false;
}

std::unique_ptr<QOpenGLShaderProgram> buildProgram(const QString& vert, const QString& frag)
{
    auto program = std::make_unique<QOpenGLShaderProgram>();
    if (!addShader(*program, QOpenGLShader::Vertex, vert)) return nullptr;
    if (!addShader(*program, QOpenGLShader::Fragment, frag)) return nullptr;
    if (!program->link()) {
        qWarning("Shader link failed (%s):\n%s", qPrintable(vert), qPrintable(program->log()));
        return nullptr;
    }
    return program;
}

} // namespace

ViewportWidget::ViewportWidget(QWidget* parent) : QOpenGLWidget(parent)
{
    setMinimumSize(320, 240);
    setMouseTracking(true); // needed to highlight gizmo balls on hover
    // X, Y and Z type a coordinate for the selected marker, so the viewport has
    // to be able to hold the keyboard -- and clicking in it is what hands it
    // over, which is the same gesture that selects the marker.
    setFocusPolicy(Qt::StrongFocus);

    layoutChrome();
}

ViewportWidget::~ViewportWidget()
{
    if (!context()) return;
    // GL objects belong to the context, so it has to be current to delete them.
    makeCurrent();
    m_gpu.destroy(*this);
    m_gpuPoints.destroy(*this);
    m_gpuLines.destroy(*this);
    m_wheelGpu.destroy(*this);
    m_rimGpu.destroy(*this);
    m_solidProgram.reset();
    m_lineProgram.reset();
    m_pointProgram.reset();
    doneCurrent();
}

void ViewportWidget::setMesh(TriMesh mesh, EdgeSet edges)
{
    m_mesh = std::move(mesh);
    m_edges = std::move(edges);
    m_uploadPending = true;
    update();
}

void ViewportWidget::clearMesh()
{
    m_mesh = TriMesh{};
    m_edges = EdgeSet{};
    m_uploadPending = true;
    update();
}

void ViewportWidget::setMeshTransform(const QMatrix4x4& transform)
{
    if (transform == m_meshTransform) return;
    m_meshTransform = transform;
    update();
}

void ViewportWidget::setHardpoints(const HardpointTable& table)
{
    m_hardpoints.clear();
    m_hardpointNames.clear();
    m_hardpointBounds = Aabb{};
    m_hardpoints.reserve(table.points.size());

    for (const Hardpoint& point : table.points) {
        const QVector3D position = point.toVector();
        m_hardpoints.push_back(position);
        m_hardpointNames.append(point.name);
        m_hardpointBounds.expand(position);
    }

    // Every index refers to the previous set, so none survives a reload -- a
    // drag in progress least of all.
    if (m_drag == Drag::Move) forgetMove();
    m_hoveredMoveAxis = -1;
    m_selectedPoint = -1;
    m_selection.clear();
    m_hoveredPoint = -1;
    m_pointsUploadPending = true;
    // The linkage is indices into the table too, and the one it was built from
    // has just been replaced. Whoever set the points sets the parts again.
    m_linkage = Linkage{};
    rebuildLinkageVertices();
    update();
}

void ViewportWidget::clearHardpoints()
{
    setHardpoints(HardpointTable{});
}

void ViewportWidget::moveHardpoint(int index, const QVector3D& position)
{
    if (index < 0 || index >= static_cast<int>(m_hardpoints.size())) return;
    m_hardpoints[static_cast<std::size_t>(index)] = position;

    // The bounds only feed fitToView(), so recomputing the whole box for one
    // edit is both simpler and, at the size of a hardpoint list, free.
    m_hardpointBounds = Aabb{};
    for (const QVector3D& p : m_hardpoints) m_hardpointBounds.expand(p);

    m_pointsUploadPending = true;
    // Parts are stored as indices, so the segments are still correct -- but the
    // vertex buffer holds copies of the coordinates, which are not.
    rebuildLinkageVertices();
    update();
}

void ViewportWidget::setHardpointPositions(const std::vector<QVector3D>& positions)
{
    if (positions.size() != m_hardpoints.size()) return;
    m_hardpoints = positions;

    m_hardpointBounds = Aabb{};
    for (const QVector3D& p : m_hardpoints) m_hardpointBounds.expand(p);

    m_pointsUploadPending = true;
    // The parts are indices into the same table, so they still name the right
    // points -- but the vertex buffer holds copies of the coordinates.
    rebuildLinkageVertices();
    update();
}

void ViewportWidget::setLinkage(const Linkage& linkage)
{
    m_linkage = linkage;
    rebuildLinkageVertices();
    update();
}

void ViewportWidget::clearLinkage()
{
    setLinkage(Linkage{});
}

void ViewportWidget::setWheelModels(TriMesh wheel, EdgeSet wheelEdges, TriMesh rim,
                                    EdgeSet rimEdges)
{
    m_wheelMesh = std::move(wheel);
    m_wheelEdges = std::move(wheelEdges);
    m_rimMesh = std::move(rim);
    m_rimEdges = std::move(rimEdges);
    m_wheelUploadPending = true;
    // Every placement is measured from the model's own box, so new models move
    // the wheels even though nobody touched a hardpoint.
    updateWheelBounds();
    update();
}

void ViewportWidget::setWheelPlacements(std::vector<WheelPlacement> placements, bool alignToCenter)
{
    m_wheelPlacements = std::move(placements);
    m_wheelAlignToCenter = alignToCenter;
    // No upload: the meshes have not changed, only the matrices they are drawn
    // with, and those are rebuilt every frame.
    updateWheelBounds();
    update();
}

void ViewportWidget::clearWheels()
{
    setWheelModels(TriMesh{}, EdgeSet{}, TriMesh{}, EdgeSet{});
    setWheelPlacements({}, m_wheelAlignToCenter);
}

void ViewportWidget::setWheelsVisible(bool visible)
{
    if (m_wheelsVisible == visible) return;
    m_wheelsVisible = visible;
    update();
    emit viewChanged();
}

void ViewportWidget::updateWheelBounds()
{
    m_wheelBounds = Aabb{};
    for (const TriMesh* model : { &m_wheelMesh, &m_rimMesh }) {
        const Aabb placed = wheelBounds(m_wheelPlacements, model->bounds, m_wheelAlignToCenter);
        if (placed.isEmpty()) continue;
        m_wheelBounds.expand(placed.min);
        m_wheelBounds.expand(placed.max);
    }
}

void ViewportWidget::setLinkageVisible(bool visible)
{
    if (m_linkageVisible == visible) return;
    m_linkageVisible = visible;
    update();
    emit viewChanged();
}

void ViewportWidget::rebuildLinkageVertices()
{
    m_linkVertices.clear();
    m_linkRanges.clear();
    m_linesUploadPending = true;

    if (m_linkage.isEmpty() || m_hardpoints.empty()) return;

    const int pointCount = static_cast<int>(m_hardpoints.size());
    const auto position = [this](int index) {
        return m_hardpoints[static_cast<std::size_t>(index)];
    };

    // Grouped by kind so that one uniform and one draw call cover each colour.
    // The kinds are an enum over a handful of values, so this is a small fixed
    // number of passes over a list that is tens of parts long.
    for (int raw = 0; raw <= static_cast<int>(PartKind::Other); ++raw) {
        const auto kind = static_cast<PartKind>(raw);
        const int first = static_cast<int>(m_linkVertices.size());

        for (const LinkagePart& part : m_linkage.parts) {
            if (part.kind != kind) continue;
            for (const ResolvedChain& chain : part.chains) {
                const int count = static_cast<int>(chain.points.size());
                if (count < 2) continue;
                const int last = chain.closed && count > 2 ? count : count - 1;
                for (int i = 0; i < last; ++i) {
                    const int a = chain.points[static_cast<std::size_t>(i)];
                    const int b = chain.points[static_cast<std::size_t>((i + 1) % count)];
                    // A stale index cannot draw anything sensible, and reading
                    // one would be worse than dropping the segment.
                    if (a < 0 || b < 0 || a >= pointCount || b >= pointCount) continue;
                    m_linkVertices.push_back(position(a));
                    m_linkVertices.push_back(position(b));
                }
            }
        }

        const int count = static_cast<int>(m_linkVertices.size()) - first;
        if (count > 0) m_linkRanges.push_back(LinkRange{ kind, first, count });
    }
}

void ViewportWidget::setHardpointLabelsVisible(bool visible)
{
    if (m_labelsVisible == visible) return;
    m_labelsVisible = visible;
    update();
    emit viewChanged();
}

void ViewportWidget::setSelectedHardpoint(int index)
{
    setSelectedHardpoints(index >= 0 ? QList<int>{ index } : QList<int>{}, index);
}

void ViewportWidget::setSelectedHardpoints(const QList<int>& selection, int current)
{
    const int count = static_cast<int>(m_hardpoints.size());
    QList<int> kept;
    for (const int index : selection)
        if (index >= 0 && index < count && !kept.contains(index)) kept.append(index);
    const int centred = kept.contains(current) ? current : (kept.isEmpty() ? -1 : kept.last());

    if (kept == m_selection && centred == m_selectedPoint) return;
    m_selection = kept;
    m_selectedPoint = centred;
    update();
    emit viewChanged();
}

void ViewportWidget::setPointEditingEnabled(bool enabled)
{
    if (enabled == m_pointEditingEnabled) return;
    m_pointEditingEnabled = enabled;
    // Whatever was being dragged was being dragged against the old rules.
    if (m_drag == Drag::Move) cancelMove();
    m_hoveredMoveAxis = -1;
    update();
}

bool ViewportWidget::markerPosition(int index, QPointF* screen) const
{
    if (index < 0 || index >= static_cast<int>(m_hardpoints.size())) return false;

    float depth = 0.0f;
    return project(m_camera.viewProjectionMatrix(aspect()),
                   m_hardpoints[static_cast<std::size_t>(index)], screen, &depth);
}

void ViewportWidget::setDisplayMode(DisplayMode mode)
{
    if (m_mode == mode) return;
    m_mode = mode;
    update();
    emit viewChanged();
}

void ViewportWidget::fitToView()
{
    // Everything on screen, not just the mesh: hardpoints are often imported on
    // their own, and they can also reach outside the geometry that is loaded.
    Aabb bounds = m_mesh.bounds;
    if (!m_hardpointBounds.isEmpty()) {
        bounds.expand(m_hardpointBounds.min);
        bounds.expand(m_hardpointBounds.max);
    }
    // A wheel reaches well outside the hardpoints it is pinned to, and cutting
    // the tyres off is exactly what "fit" should not do.
    if (!m_wheelBounds.isEmpty()) {
        bounds.expand(m_wheelBounds.min);
        bounds.expand(m_wheelBounds.max);
    }
    if (bounds.isEmpty()) return;

    m_camera.fitTo(bounds, aspect());
    update();
    emit viewChanged();
}

void ViewportWidget::applyPreset(ViewPreset preset)
{
    m_camera.applyPreset(preset);
    update();
    emit viewChanged();
}

void ViewportWidget::setCameraState(const CameraState& state)
{
    m_camera.setState(state);
    update();
}

float ViewportWidget::aspect() const
{
    // Logical pixels: the ratio is identical in device pixels, and keeping every
    // camera calculation in logical units matches the mouse coordinates Qt sends.
    return static_cast<float>(width()) / static_cast<float>(std::max(1, height()));
}

void ViewportWidget::layoutChrome()
{
    m_modeSelector.setTopRight(QPointF(width() - kCornerMargin, kCornerMargin));
    // The gizmo sits directly beneath the mode selector, sharing its right edge.
    const qreal top = m_modeSelector.bounds().bottom() + kGizmoGap;
    m_gizmo.setBounds(QRectF(width() - kGizmoSize - kCornerMargin, top, kGizmoSize, kGizmoSize));
}

bool ViewportWidget::buildPrograms()
{
    m_solidProgram = buildProgram(QStringLiteral(":/shaders/solid.vert"),
                                  QStringLiteral(":/shaders/solid.frag"));
    m_lineProgram = buildProgram(QStringLiteral(":/shaders/line.vert"),
                                 QStringLiteral(":/shaders/line.frag"));
    m_pointProgram = buildProgram(QStringLiteral(":/shaders/point.vert"),
                                  QStringLiteral(":/shaders/point.frag"));
    return m_solidProgram && m_lineProgram && m_pointProgram;
}

void ViewportWidget::initializeGL()
{
    // Returns false if the context cannot supply 3.3 core entry points. Checking
    // it turns an otherwise baffling crash inside the first glGenVertexArrays
    // into a message that names the actual context.
    if (!initializeOpenGLFunctions()) {
        m_contextError = tr("Could not load OpenGL 3.3 core functions.");
        qCritical("%s", qPrintable(m_contextError));
        emit contextReady(m_contextError);
        return;
    }

    // setProfile()/setVersion() are requests, not guarantees -- some EGL paths
    // hand back a compatibility or GLES context regardless. Verify what arrived.
    const QSurfaceFormat actual = context()->format();
    if (context()->isOpenGLES() || actual.profile() != QSurfaceFormat::CoreProfile
        || actual.version() < qMakePair(3, 3)) {
        m_contextError = tr("Need an OpenGL 3.3 core profile, but got %1 %2.%3 (%4). "
                            "Try QT_QPA_PLATFORM=xcb or QT_OPENGL=software.")
                             .arg(context()->isOpenGLES() ? QStringLiteral("OpenGL ES")
                                                          : QStringLiteral("OpenGL"))
                             .arg(actual.majorVersion())
                             .arg(actual.minorVersion())
                             .arg(actual.profile() == QSurfaceFormat::CoreProfile
                                      ? QStringLiteral("core")
                                      : QStringLiteral("compatibility"));
        qCritical("%s", qPrintable(m_contextError));
        emit contextReady(m_contextError);
        return;
    }
    m_contextError.clear();

    const auto str = [this](GLenum name) {
        const auto* s = reinterpret_cast<const char*>(glGetString(name));
        return QString::fromLatin1(s ? s : "?");
    };
    // Logged so a driver, platform or hybrid-GPU problem diagnoses itself.
    emit contextReady(QStringLiteral("%1 | %2 | GL %3 | %4x MSAA")
                          .arg(str(GL_VENDOR), str(GL_RENDERER), str(GL_VERSION))
                          .arg(actual.samples()));

    // initializeGL can run more than once -- context loss, reparenting, moving
    // between screens -- so everything GL-side is rebuilt here from CPU data.
    buildPrograms();
    m_gpu.destroy(*this);
    m_gpuPoints.destroy(*this);
    m_gpuLines.destroy(*this);
    m_wheelGpu.destroy(*this);
    m_rimGpu.destroy(*this);
    m_uploadPending = !m_mesh.isEmpty();
    m_wheelUploadPending = hasWheelModels();
    m_pointsUploadPending = !m_hardpoints.empty();
    m_linesUploadPending = !m_linkVertices.empty();

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_MULTISAMPLE);
}

void ViewportWidget::resizeGL(int, int)
{
    // Qt sizes and binds the framebuffer and sets the viewport itself; the
    // projection is rebuilt from aspect() every frame. Only the corner furniture
    // needs repositioning.
    layoutChrome();
}

void ViewportWidget::renderScene()
{
    if (!m_contextError.isEmpty()) return;

    if (m_uploadPending) {
        m_gpu.upload(*this, m_mesh, m_edges);
        m_uploadPending = false;
    }
    if (m_pointsUploadPending) {
        m_gpuPoints.upload(*this, m_hardpoints);
        m_pointsUploadPending = false;
    }
    if (m_linesUploadPending) {
        m_gpuLines.upload(*this, m_linkVertices);
        m_linesUploadPending = false;
    }
    if (m_wheelUploadPending) {
        m_wheelGpu.upload(*this, m_wheelMesh, m_wheelEdges);
        m_rimGpu.upload(*this, m_rimMesh, m_rimEdges);
        m_wheelUploadPending = false;
    }

    // Qt's paint engine shares this context and leaves its own state behind.
    // glDepthMask matters most: glClear(GL_DEPTH_BUFFER_BIT) is *masked* by it, so
    // with depth writes disabled the depth buffer silently never clears and the
    // mesh renders in arbitrary triangle order.
    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LESS);
    glDisable(GL_BLEND);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_STENCIL_TEST);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

    glClearColor(0.16f, 0.17f, 0.19f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    // Culling stays off: STL winding is often inconsistent, so the fragment
    // shader flips the normal towards the viewer instead.
    glDisable(GL_CULL_FACE);

    const QMatrix4x4 view = m_camera.viewMatrix();
    const QMatrix4x4 mvp = m_camera.projectionMatrix(aspect()) * view;
    // The two modes are exclusive: a shaded surface, or its triangles. Nothing is
    // drawn twice, so no depth biasing is needed.
    const bool wireframe = (m_mode == DisplayMode::Triangles);

    if (m_gpu.isValid()) {
        // The geometry has a model matrix of its own, the way each wheel does:
        // it is the chassis, and a rolled body takes it along.
        const QMatrix4x4 meshModelView = view * m_meshTransform;
        const QMatrix4x4 meshMvp = mvp * m_meshTransform;

        if (!wireframe && m_solidProgram) {
            m_solidProgram->bind();
            m_solidProgram->setUniformValue("uMvp", meshMvp);
            m_solidProgram->setUniformValue("uModelView", meshModelView);
            m_solidProgram->setUniformValue("uNormalMatrix", meshModelView.normalMatrix());
            m_solidProgram->setUniformValue("uBaseColor", kSurfaceColor);
            m_gpu.drawSolid(*this);
            m_solidProgram->release();
        }

        if (wireframe && m_lineProgram) {
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            m_lineProgram->bind();
            m_lineProgram->setUniformValue("uMvp", meshMvp);
            m_lineProgram->setUniformValue("uColor", kEdgeColor);
            m_gpu.drawLines(*this);
            m_lineProgram->release();
            glDisable(GL_BLEND);
        }
    }

    // Solid bodies like the imported geometry, and drawn in the same mode it is,
    // so the triangle view shows the wheels as triangles too.
    renderWheels(view, mvp);

    // Last, and deliberately outside the mesh guard: hardpoints are frequently
    // the only thing loaded. The parts go under the markers, so a marker stays
    // clickable where several links meet on it.
    renderLinkage(mvp);
    renderHardpoints(mvp);
}

void ViewportWidget::renderWheels(const QMatrix4x4& view, const QMatrix4x4& mvp)
{
    if (!m_wheelsVisible || m_wheelPlacements.empty()) return;

    // One model, drawn once per corner. The mesh is uploaded a single time and
    // the copies differ only in their model matrix, so four wheels cost four
    // draw calls rather than four buffers.
    struct Body {
        const GpuMesh* gpu;
        const TriMesh* mesh;
        QVector3D color;
    };
    const Body bodies[2] = {
        { &m_wheelGpu, &m_wheelMesh, kWheelColor },
        { &m_rimGpu, &m_rimMesh, kRimColor },
    };

    const bool wireframe = (m_mode == DisplayMode::Triangles);
    QOpenGLShaderProgram* program = wireframe ? m_lineProgram.get() : m_solidProgram.get();
    if (!program) return;

    if (wireframe) {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    }
    program->bind();
    if (wireframe) program->setUniformValue("uColor", kEdgeColor);

    for (const WheelPlacement& placement : m_wheelPlacements) {
        for (const Body& body : bodies) {
            if (!body.gpu->isValid()) continue;

            // Per body, not per placement: the wheel and the rim have their own
            // boxes, so the point of each that lands on the hardpoint differs.
            const QMatrix4x4 model =
                wheelTransform(placement, body.mesh->bounds, m_wheelAlignToCenter);
            program->setUniformValue("uMvp", mvp * model);
            if (wireframe) {
                body.gpu->drawLines(*this);
                continue;
            }
            // A mirrored copy has a negative determinant, which flips the
            // winding; nothing here cares, because culling is off and the
            // fragment shader turns the normal towards the viewer. The inverse
            // transpose keeps the normals themselves honest.
            const QMatrix4x4 modelView = view * model;
            program->setUniformValue("uModelView", modelView);
            program->setUniformValue("uNormalMatrix", modelView.normalMatrix());
            program->setUniformValue("uBaseColor", body.color);
            body.gpu->drawSolid(*this);
        }
    }

    program->release();
    if (wireframe) glDisable(GL_BLEND);
}

void ViewportWidget::renderLinkage(const QMatrix4x4& mvp)
{
    if (!m_linkageVisible || !m_lineProgram || !m_gpuLines.isValid()) return;

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glLineWidth(kLinkWidthPx * static_cast<float>(devicePixelRatioF()));

    m_lineProgram->bind();
    m_lineProgram->setUniformValue("uMvp", mvp);

    // Two passes, the same way the markers do it: what the geometry hides is
    // drawn first, dimmed and without writing depth, so a wishbone inside a
    // chassis panel is still findable.
    for (int pass = 0; pass < 2; ++pass) {
        const bool occluded = (pass == 0);
        glDepthFunc(occluded ? GL_GREATER : GL_LESS);
        glDepthMask(occluded ? GL_FALSE : GL_TRUE);
        const float alpha = occluded ? kOccludedLinkAlpha : 1.0f;

        for (const LinkRange& range : m_linkRanges) {
            m_lineProgram->setUniformValue("uColor", QVector4D(partColor(range.kind), alpha));
            m_gpuLines.draw(*this, range.first, range.count);
        }
    }

    m_lineProgram->release();

    glDepthFunc(GL_LESS);
    glDepthMask(GL_TRUE);
    glLineWidth(1.0f);
    glDisable(GL_BLEND);
}

void ViewportWidget::renderHardpoints(const QMatrix4x4& mvp)
{
    if (!m_pointProgram || !m_gpuPoints.isValid()) return;

    // gl_PointSize is in device pixels, so a marker keeps its size on a HiDPI
    // screen instead of shrinking to half of it.
    const float scale = static_cast<float>(devicePixelRatioF());

    glEnable(GL_PROGRAM_POINT_SIZE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    m_pointProgram->bind();
    m_pointProgram->setUniformValue("uMvp", mvp);
    m_pointProgram->setUniformValue("uRimColor", kPointRimColor);

    // Two passes. The first draws only what the geometry hides, dimmed and
    // without writing depth: a hardpoint buried inside a control arm still has
    // to be findable, which is usually the moment somebody goes looking for it.
    for (int pass = 0; pass < 2; ++pass) {
        const bool occluded = (pass == 0);
        glDepthFunc(occluded ? GL_GREATER : GL_LESS);
        glDepthMask(occluded ? GL_FALSE : GL_TRUE);
        const float alpha = occluded ? kOccludedAlpha : 1.0f;

        const auto drawMarker = [&](int index, const QVector3D& colour, float size) {
            m_pointProgram->setUniformValue("uPointSize", size * scale);
            m_pointProgram->setUniformValue("uColor", QVector4D(colour, alpha));
            if (index < 0)
                m_gpuPoints.drawAll(*this);
            else
                m_gpuPoints.drawOne(*this, index);
        };

        drawMarker(-1, kPointColor, kPointSizePx);
        if (m_hoveredPoint >= 0 && !m_selection.contains(m_hoveredPoint))
            drawMarker(m_hoveredPoint, kHoverColor, kHighlightSizePx);
        for (const int index : m_selection) drawMarker(index, kSelectedColor, kHighlightSizePx);
    }

    m_pointProgram->release();

    // Hand the state back the way renderScene() set it up, so the next frame's
    // clear and mesh pass do not inherit a reversed depth test.
    glDepthFunc(GL_LESS);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glDisable(GL_PROGRAM_POINT_SIZE);
}

bool ViewportWidget::project(const QMatrix4x4& viewProjection, const QVector3D& world,
                             QPointF* screen, float* depth) const
{
    // The matrix is passed in rather than derived here because the callers
    // project a whole table against one camera; the arithmetic itself is the
    // camera's, so the markers, the labels and the arrows cannot disagree.
    return Camera::projectTo(viewProjection, world, size(), screen, depth);
}

int ViewportWidget::hardpointAt(const QPoint& pos) const
{
    if (m_hardpoints.empty()) return -1;

    const QMatrix4x4 viewProjection = m_camera.projectionMatrix(aspect()) * m_camera.viewMatrix();
    int best = -1;
    float bestDepth = std::numeric_limits<float>::max();

    for (std::size_t i = 0; i < m_hardpoints.size(); ++i) {
        QPointF screen;
        float depth = 0.0f;
        if (!project(viewProjection, m_hardpoints[i], &screen, &depth)) continue;

        const QPointF delta = screen - QPointF(pos);
        if (QPointF::dotProduct(delta, delta) > kPickRadiusPx * kPickRadiusPx) continue;

        // Where markers overlap, the one nearest the eye wins -- which is what
        // clicking into a cluster is asking for.
        if (depth < bestDepth) {
            bestDepth = depth;
            best = static_cast<int>(i);
        }
    }
    return best;
}

MoveGizmo::Layout ViewportWidget::gizmoLayout() const
{
    // One marker gets the arrows: the one the selection is centred on. A
    // selection is a list in picking order -- it is how a part is built up --
    // and three arrows on each of six points would be a thicket, not a handle.
    if (!m_pointEditingEnabled || m_selectedPoint < 0) return {};
    if (m_selectedPoint >= static_cast<int>(m_hardpoints.size())) return {};

    return MoveGizmo::layoutAt(m_camera, size(),
                               m_hardpoints[static_cast<std::size_t>(m_selectedPoint)]);
}

bool ViewportWidget::beginMove(const MoveGizmo::Layout& layout, int axis, const QPoint& pos)
{
    if (axis < 0 || !layout.visible) return false;

    m_drag = Drag::Move;
    m_moveAxis = axis;
    m_movePoint = m_selectedPoint;
    m_moveLayout = layout;
    m_moveOrigin = m_hardpoints[static_cast<std::size_t>(m_movePoint)];
    m_moveDistance = 0.0;
    m_pressPos = pos;
    setCursor(Qt::ClosedHandCursor);
    return true;
}

void ViewportWidget::dragMoveTo(const QPoint& pos)
{
    if (m_movePoint < 0) return;

    // Measured from where the drag began, against the gizmo as it stood there:
    // step by step against a gizmo that is itself moving, the point would walk
    // away from the pointer.
    m_moveDistance = MoveGizmo::dragDistance(m_moveLayout, m_moveAxis, m_pressPos, pos);
    const QVector3D position =
        m_moveOrigin + m_moveLayout.arms[static_cast<std::size_t>(m_moveAxis)].direction
                           * static_cast<float>(m_moveDistance);

    moveHardpoint(m_movePoint, position);
    emit hardpointDragging(m_movePoint, m_moveAxis, m_moveDistance);
}

void ViewportWidget::finishMove()
{
    const int index = m_movePoint;
    const int axis = m_moveAxis;
    const double distance = m_moveDistance;
    forgetMove();

    // Only a drag that came to something is an edit. Taking hold of an arrow
    // and letting go again must not mark the project dirty.
    if (index >= 0 && distance != 0.0) emit hardpointMoved(index, axis, distance);
}

void ViewportWidget::cancelMove()
{
    if (m_movePoint >= 0) {
        moveHardpoint(m_movePoint, m_moveOrigin);
        emit hardpointDragging(m_movePoint, m_moveAxis, 0.0);
    }
    forgetMove();
}

void ViewportWidget::forgetMove()
{
    m_drag = Drag::None;
    m_moveAxis = -1;
    m_movePoint = -1;
    m_moveDistance = 0.0;
    unsetCursor();
}

std::vector<ViewportWidget::Label> ViewportWidget::layoutLabels() const
{
    std::vector<Label> labels;
    if (m_hardpoints.empty()) return labels;
    // With labels switched off the hovered and selected points still get one --
    // otherwise there is no way to find out what you are pointing at.
    if (!m_labelsVisible && m_selection.isEmpty() && m_hoveredPoint < 0) return labels;

    const QMatrix4x4 viewProjection = m_camera.projectionMatrix(aspect()) * m_camera.viewMatrix();
    const QFontMetricsF metrics(font());
    const QRectF viewport(rect());

    struct Candidate {
        QPointF anchor;
        float depth = 0.0f;
        int index = -1;
        bool pinned = false;
    };
    std::vector<Candidate> candidates;
    candidates.reserve(m_hardpoints.size());

    for (std::size_t i = 0; i < m_hardpoints.size(); ++i) {
        const int index = static_cast<int>(i);
        const bool pinned = (m_selection.contains(index) || index == m_hoveredPoint);
        if (!m_labelsVisible && !pinned) continue;

        QPointF anchor;
        float depth = 0.0f;
        if (!project(viewProjection, m_hardpoints[i], &anchor, &depth)) continue;
        candidates.push_back({ anchor, depth, index, pinned });
    }

    // Pinned first, then nearest: when two labels collide the one in front, or
    // the one the user is pointing at, is the one that survives.
    std::stable_sort(candidates.begin(), candidates.end(),
                     [](const Candidate& a, const Candidate& b) {
                         if (a.pinned != b.pinned) return a.pinned;
                         return a.depth < b.depth;
                     });

    for (const Candidate& candidate : candidates) {
        const QString text = m_hardpointNames.value(candidate.index);
        if (text.isEmpty()) continue;

        const QSizeF size(metrics.horizontalAdvance(text) + 2.0 * kLabelPadX,
                          metrics.height() + 2.0 * kLabelPadY);
        QRectF box(candidate.anchor + QPointF(kLabelGap, -size.height() - kLabelGap), size);

        // Slide a label that would hang off an edge back inside, rather than
        // clipping it against the frame.
        if (box.right() > viewport.right()) box.moveRight(viewport.right());
        if (box.left() < viewport.left()) box.moveLeft(viewport.left());
        if (box.top() < viewport.top()) box.moveTop(candidate.anchor.y() + kLabelGap);
        if (box.bottom() > viewport.bottom()) box.moveBottom(viewport.bottom());

        if (!candidate.pinned) {
            const bool collides = std::any_of(labels.begin(), labels.end(),
                                              [&box](const Label& placed) {
                                                  return placed.rect.intersects(box);
                                              });
            // Forty names stacked on one another is not a drawing, it is a mess.
            if (collides) continue;
        }
        labels.push_back({ box, text, candidate.index, candidate.anchor });
    }
    return labels;
}

QRectF ViewportWidget::chromeArea(const std::vector<Label>& labels,
                                  const MoveGizmo::Layout& gizmo) const
{
    QRectF area = m_modeSelector.bounds().united(m_gizmo.bounds());
    if (gizmo.visible) area = area.united(gizmo.bounds());
    for (const Label& label : labels) {
        area = area.united(label.rect);
        area = area.united(QRectF(label.anchor, QSizeF(1.0, 1.0)));
    }
    return area.adjusted(-2, -2, 2, 2).intersected(QRectF(rect()));
}

QImage ViewportWidget::renderChrome(const QRectF& area, const std::vector<Label>& labels,
                                   const MoveGizmo::Layout& gizmo) const
{
    if (area.isEmpty()) return {};

    const qreal dpr = devicePixelRatioF();
    QImage image(QSize(static_cast<int>(std::ceil(area.width() * dpr)),
                       static_cast<int>(std::ceil(area.height() * dpr))),
                 QImage::Format_ARGB32_Premultiplied);
    image.setDevicePixelRatio(dpr);
    image.fill(Qt::transparent);

    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);
    painter.translate(-area.topLeft());

    for (const Label& label : labels) {
        const bool selected = m_selection.contains(label.index);
        const bool hovered = (label.index == m_hoveredPoint) && !selected;
        const QColor fill = selected ? kLabelFillSelected : hovered ? kLabelFillHovered : kLabelFill;
        const QColor text = selected ? kLabelTextSelected : hovered ? kLabelTextHovered : kLabelText;

        // A leader line, because a label pushed aside to avoid its neighbours is
        // no use if you cannot tell which marker it belongs to.
        painter.setPen(QPen(selected || hovered ? text : kLabelLeader, 1.0));
        painter.drawLine(label.anchor, nearestPointOn(label.rect, label.anchor));

        painter.setPen(Qt::NoPen);
        painter.setBrush(fill);
        painter.drawRoundedRect(label.rect, 3.0, 3.0);
        painter.setPen(text);
        painter.drawText(label.rect, Qt::AlignCenter, label.text);
    }

    // Over the labels, under the corner furniture: the arrows belong to a point
    // in the scene, and the two fixed panels are the window's own.
    MoveGizmo::paint(painter, gizmo, m_hoveredMoveAxis, m_drag == Drag::Move ? m_moveAxis : -1);

    m_gizmo.paint(painter, m_camera, m_hoveredAxis);
    m_modeSelector.paint(painter, m_mode, m_hoveredButton);
    return image;
}

void ViewportWidget::paintGL()
{
    // Raw GL is fenced off so Qt's paint engine can restore its own state around it.
    QPainter painter(this);
    painter.beginNativePainting();
    renderScene();
    painter.endNativePainting();

    // The chrome is rasterised off-screen and blitted as a single image rather
    // than drawn straight onto the GL surface. Qt's OpenGL paint engine silently
    // drops text, gradients and clipped fills here -- verified by rendering the
    // identical calls to a QImage, where they all appear -- so everything goes
    // through drawImage, which is just a textured quad and always works.
    const std::vector<Label> labels = layoutLabels();
    const MoveGizmo::Layout gizmo = gizmoLayout();
    const QRectF area = chromeArea(labels, gizmo);
    painter.drawImage(area.topLeft(), renderChrome(area, labels, gizmo));
}

void ViewportWidget::mousePressEvent(QMouseEvent* event)
{
    m_lastPos = event->position().toPoint();
    m_pressPos = m_lastPos;
    m_pressedAxis = -1;
    m_pressedButton = -1;
    m_pressedPoint = -1;

    if (event->button() == Qt::LeftButton && m_modeSelector.contains(m_lastPos)) {
        m_pressedButton = m_modeSelector.buttonAt(m_lastPos);
        m_drag = Drag::None;
        return;
    }

    if (event->button() == Qt::LeftButton && m_gizmo.contains(m_lastPos)) {
        // Press inside the gizmo: a click snaps to an axis, a drag orbits.
        m_pressedAxis = m_gizmo.axisAt(m_lastPos, m_camera);
        m_drag = Drag::Gizmo;
        return;
    }

    // The arrows sit over the scene, so they are offered the press before the
    // markers are -- otherwise the marker under the hub would swallow it.
    if (event->button() == Qt::LeftButton && !(event->modifiers() & Qt::ShiftModifier)) {
        const MoveGizmo::Layout layout = gizmoLayout();
        if (beginMove(layout, MoveGizmo::axisAt(layout, m_lastPos), m_lastPos)) return;
    }

    if (event->button() == Qt::MiddleButton
        || (event->button() == Qt::LeftButton && (event->modifiers() & Qt::ShiftModifier))) {
        // Shift+left duplicates middle-drag for trackpads with no middle button.
        m_drag = Drag::Pan;
    } else if (event->button() == Qt::LeftButton) {
        // Selection is decided on release, so that a press which turns into a
        // drag still orbits instead of picking whatever it started on.
        m_pressedPoint = hardpointAt(m_lastPos);
        m_drag = Drag::Orbit;
    } else {
        m_drag = Drag::None;
    }
}

void ViewportWidget::mouseMoveEvent(QMouseEvent* event)
{
    const QPoint pos = event->position().toPoint();

    if (m_drag == Drag::None) {
        const int axis = m_gizmo.contains(pos) ? m_gizmo.axisAt(pos, m_camera) : -1;
        const int button = m_modeSelector.buttonAt(pos);
        // The corner furniture sits on top, so it swallows the hover there.
        const bool overChrome = m_gizmo.contains(pos) || m_modeSelector.contains(pos);
        const int moveAxis = overChrome ? -1 : MoveGizmo::axisAt(gizmoLayout(), pos);
        // An arm covers the marker it belongs to, the same way it takes the
        // press: pointing at it is asking to move the point, not to pick one.
        const int point = (overChrome || moveAxis >= 0) ? -1 : hardpointAt(pos);

        if (axis != m_hoveredAxis || button != m_hoveredButton || point != m_hoveredPoint
            || moveAxis != m_hoveredMoveAxis) {
            m_hoveredAxis = axis;
            m_hoveredButton = button;
            m_hoveredPoint = point;
            m_hoveredMoveAxis = moveAxis;
            if (moveAxis >= 0)
                setCursor(Qt::OpenHandCursor);
            else
                setCursor(point >= 0 ? Qt::PointingHandCursor : Qt::ArrowCursor);
            update();
        }
        return;
    }

    const QPoint delta = pos - m_lastPos;
    m_lastPos = pos;

    if (m_drag == Drag::Move) {
        dragMoveTo(pos);
        return; // the camera stays where it is: the point is what is moving
    }

    if (m_drag == Drag::Gizmo) {
        // Only once the pointer has really moved, so a slightly shaky click still
        // reads as a click.
        if ((pos - m_pressPos).manhattanLength() <= kClickSlopPixels) return;
        m_pressedAxis = -1;
        m_drag = Drag::Orbit;
    }

    if (m_drag == Drag::Orbit)
        m_camera.orbit(static_cast<float>(delta.x()), static_cast<float>(delta.y()));
    else if (m_drag == Drag::Pan)
        m_camera.pan(static_cast<float>(delta.x()), static_cast<float>(delta.y()), height());

    update();
    emit viewChanged();
}

void ViewportWidget::mouseReleaseEvent(QMouseEvent* event)
{
    if (m_drag == Drag::Move) {
        finishMove();
        return; // a drag that moved a point picks nothing and orbits nothing
    }

    if (m_pressedButton >= 0
        && m_modeSelector.buttonAt(event->position().toPoint()) == m_pressedButton) {
        const DisplayMode mode = ModeSelector::modeForButton(m_pressedButton);
        setDisplayMode(mode);
        emit displayModeChanged(mode);
    }

    if (m_drag == Drag::Gizmo && m_pressedAxis >= 0
        && (event->position().toPoint() - m_pressPos).manhattanLength() <= kClickSlopPixels) {
        m_camera.applyPreset(NavGizmo::presetForAxis(m_pressedAxis));
        update();
        emit viewChanged();
    }

    // A click that did not turn into an orbit picks a marker -- or, on empty
    // space, clears the selection. With Ctrl held it adds the marker to what is
    // already picked, or takes it away again, which is how a chain of points is
    // picked for a new part: in the order it is drawn.
    if (m_drag == Drag::Orbit && event->button() == Qt::LeftButton && !m_hardpoints.empty()
        && (event->position().toPoint() - m_pressPos).manhattanLength() <= kClickSlopPixels) {
        const bool toggling = event->modifiers() & Qt::ControlModifier;
        QList<int> selection;
        int current = m_pressedPoint;
        if (toggling) {
            selection = m_selection;
            if (m_pressedPoint >= 0 && selection.contains(m_pressedPoint)) {
                selection.removeAll(m_pressedPoint);
                current = selection.isEmpty() ? -1 : selection.last();
            } else if (m_pressedPoint >= 0) {
                selection.append(m_pressedPoint);
            } else {
                current = m_selectedPoint; // Ctrl on empty space changes nothing
            }
        } else if (m_pressedPoint >= 0) {
            selection = { m_pressedPoint };
        }
        setSelectedHardpoints(selection, current);
        emit hardpointSelectionEdited(m_selection, m_selectedPoint);
    }

    m_drag = Drag::None;
    m_pressedAxis = -1;
    m_pressedButton = -1;
    m_pressedPoint = -1;
}

void ViewportWidget::keyPressEvent(QKeyEvent* event)
{
    // Escape puts a drag back where it started. Only a drag: there is nothing
    // else here a press of it could undo.
    if (event->key() == Qt::Key_Escape && m_drag == Drag::Move) {
        cancelMove();
        event->accept();
        return;
    }

    const int axis = [key = event->key()] {
        switch (key) {
        case Qt::Key_X: return 0;
        case Qt::Key_Y: return 1;
        case Qt::Key_Z: return 2;
        default: return -1;
        }
    }();

    // Unmodified, so Ctrl+Z stays whatever Ctrl+Z is, and with a marker to ask
    // about. Anything else goes on to the window, which has its own shortcuts.
    if (axis < 0 || event->modifiers() != Qt::NoModifier || !m_pointEditingEnabled
        || m_selectedPoint < 0 || m_drag != Drag::None) {
        QOpenGLWidget::keyPressEvent(event);
        return;
    }

    emit coordinateEntryRequested(m_selectedPoint, axis);
    event->accept();
}

void ViewportWidget::wheelEvent(QWheelEvent* event)
{
    const int delta = event->angleDelta().y();
    if (delta == 0) return;
    m_camera.zoom(static_cast<float>(delta) * kWheelStepsPerNotch);
    update();
    emit viewChanged();
}

void ViewportWidget::leaveEvent(QEvent* event)
{
    if (m_hoveredAxis != -1 || m_hoveredButton != -1 || m_hoveredPoint != -1
        || m_hoveredMoveAxis != -1) {
        m_hoveredAxis = -1;
        m_hoveredButton = -1;
        m_hoveredPoint = -1;
        m_hoveredMoveAxis = -1;
        unsetCursor();
        update();
    }
    QOpenGLWidget::leaveEvent(event);
}

} // namespace suspkin
