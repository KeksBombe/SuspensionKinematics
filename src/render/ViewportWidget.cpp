#include "render/ViewportWidget.h"

#include <QCursor>
#include <QFontMetricsF>
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

    layoutChrome();
}

ViewportWidget::~ViewportWidget()
{
    if (!context()) return;
    // GL objects belong to the context, so it has to be current to delete them.
    makeCurrent();
    m_gpu.destroy(*this);
    m_gpuPoints.destroy(*this);
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

    // Both indices refer to the previous set, so neither survives a reload.
    m_selectedPoint = -1;
    m_hoveredPoint = -1;
    m_pointsUploadPending = true;
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
    update();
}

void ViewportWidget::setHardpointLabelsVisible(bool visible)
{
    if (m_labelsVisible == visible) return;
    m_labelsVisible = visible;
    update();
}

void ViewportWidget::setSelectedHardpoint(int index)
{
    const int clamped = (index >= 0 && index < static_cast<int>(m_hardpoints.size())) ? index : -1;
    if (m_selectedPoint == clamped) return;
    m_selectedPoint = clamped;
    update();
}

void ViewportWidget::setDisplayMode(DisplayMode mode)
{
    if (m_mode == mode) return;
    m_mode = mode;
    update();
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
    if (bounds.isEmpty()) return;

    m_camera.fitTo(bounds, aspect());
    update();
}

void ViewportWidget::applyPreset(ViewPreset preset)
{
    m_camera.applyPreset(preset);
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
    m_uploadPending = !m_mesh.isEmpty();
    m_pointsUploadPending = !m_hardpoints.empty();

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
        if (!wireframe && m_solidProgram) {
            m_solidProgram->bind();
            m_solidProgram->setUniformValue("uMvp", mvp);
            m_solidProgram->setUniformValue("uModelView", view);
            m_solidProgram->setUniformValue("uNormalMatrix", view.normalMatrix());
            m_solidProgram->setUniformValue("uBaseColor", kSurfaceColor);
            m_gpu.drawSolid(*this);
            m_solidProgram->release();
        }

        if (wireframe && m_lineProgram) {
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            m_lineProgram->bind();
            m_lineProgram->setUniformValue("uMvp", mvp);
            m_lineProgram->setUniformValue("uColor", kEdgeColor);
            m_gpu.drawLines(*this);
            m_lineProgram->release();
            glDisable(GL_BLEND);
        }
    }

    // Last, and deliberately outside the mesh guard: hardpoints are frequently
    // the only thing loaded.
    renderHardpoints(mvp);
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
        if (m_hoveredPoint >= 0 && m_hoveredPoint != m_selectedPoint)
            drawMarker(m_hoveredPoint, kHoverColor, kHighlightSizePx);
        if (m_selectedPoint >= 0)
            drawMarker(m_selectedPoint, kSelectedColor, kHighlightSizePx);
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
    const QVector4D clip = viewProjection * QVector4D(world, 1.0f);
    if (clip.w() <= 0.0f) return false; // behind the eye

    const QVector3D ndc = clip.toVector3D() / clip.w();
    if (ndc.z() < -1.0f || ndc.z() > 1.0f) return false; // outside the depth range

    *screen = QPointF((static_cast<double>(ndc.x()) * 0.5 + 0.5) * width(),
                      (0.5 - static_cast<double>(ndc.y()) * 0.5) * height());
    *depth = ndc.z();
    return true;
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

std::vector<ViewportWidget::Label> ViewportWidget::layoutLabels() const
{
    std::vector<Label> labels;
    if (m_hardpoints.empty()) return labels;
    // With labels switched off the hovered and selected points still get one --
    // otherwise there is no way to find out what you are pointing at.
    if (!m_labelsVisible && m_selectedPoint < 0 && m_hoveredPoint < 0) return labels;

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
        const bool pinned = (index == m_selectedPoint || index == m_hoveredPoint);
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

QRectF ViewportWidget::chromeArea(const std::vector<Label>& labels) const
{
    QRectF area = m_modeSelector.bounds().united(m_gizmo.bounds());
    for (const Label& label : labels) {
        area = area.united(label.rect);
        area = area.united(QRectF(label.anchor, QSizeF(1.0, 1.0)));
    }
    return area.adjusted(-2, -2, 2, 2).intersected(QRectF(rect()));
}

QImage ViewportWidget::renderChrome(const QRectF& area, const std::vector<Label>& labels) const
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
        const bool selected = (label.index == m_selectedPoint);
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
    const QRectF area = chromeArea(labels);
    painter.drawImage(area.topLeft(), renderChrome(area, labels));
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
        const int point = overChrome ? -1 : hardpointAt(pos);

        if (axis != m_hoveredAxis || button != m_hoveredButton || point != m_hoveredPoint) {
            m_hoveredAxis = axis;
            m_hoveredButton = button;
            m_hoveredPoint = point;
            setCursor(point >= 0 ? Qt::PointingHandCursor : Qt::ArrowCursor);
            update();
        }
        return;
    }

    const QPoint delta = pos - m_lastPos;
    m_lastPos = pos;

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
}

void ViewportWidget::mouseReleaseEvent(QMouseEvent* event)
{
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
    }

    // A click that did not turn into an orbit picks a marker -- or, on empty
    // space, clears the selection.
    if (m_drag == Drag::Orbit && event->button() == Qt::LeftButton && !m_hardpoints.empty()
        && (event->position().toPoint() - m_pressPos).manhattanLength() <= kClickSlopPixels) {
        setSelectedHardpoint(m_pressedPoint);
        emit hardpointClicked(m_pressedPoint);
    }

    m_drag = Drag::None;
    m_pressedAxis = -1;
    m_pressedButton = -1;
    m_pressedPoint = -1;
}

void ViewportWidget::wheelEvent(QWheelEvent* event)
{
    const int delta = event->angleDelta().y();
    if (delta == 0) return;
    m_camera.zoom(static_cast<float>(delta) * kWheelStepsPerNotch);
    update();
}

void ViewportWidget::leaveEvent(QEvent* event)
{
    if (m_hoveredAxis != -1 || m_hoveredButton != -1 || m_hoveredPoint != -1) {
        m_hoveredAxis = -1;
        m_hoveredButton = -1;
        m_hoveredPoint = -1;
        unsetCursor();
        update();
    }
    QOpenGLWidget::leaveEvent(event);
}

} // namespace suspkin
