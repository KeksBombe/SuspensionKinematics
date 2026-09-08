#include "render/ViewportWidget.h"

#include <QMouseEvent>
#include <QOpenGLShaderProgram>
#include <QImage>
#include <QPainter>
#include <QVector4D>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
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
    m_solidProgram.reset();
    m_lineProgram.reset();
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

void ViewportWidget::setDisplayMode(DisplayMode mode)
{
    if (m_mode == mode) return;
    m_mode = mode;
    update();
}

void ViewportWidget::fitToView()
{
    if (m_mesh.isEmpty()) return;
    m_camera.fitTo(m_mesh.bounds, aspect());
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
    return m_solidProgram && m_lineProgram;
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
    m_uploadPending = !m_mesh.isEmpty();

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

    if (!m_gpu.isValid()) return;

    const QMatrix4x4 view = m_camera.viewMatrix();
    const QMatrix4x4 mvp = m_camera.projectionMatrix(aspect()) * view;
    // The two modes are exclusive: a shaded surface, or its triangles. Nothing is
    // drawn twice, so no depth biasing is needed.
    const bool wireframe = (m_mode == DisplayMode::Triangles);

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

QImage ViewportWidget::renderChrome() const
{
    const QRectF area = m_modeSelector.bounds().united(m_gizmo.bounds()).adjusted(-2, -2, 2, 2);
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
    const QRectF area = m_modeSelector.bounds().united(m_gizmo.bounds()).adjusted(-2, -2, 2, 2);
    painter.drawImage(area.topLeft(), renderChrome());
}

void ViewportWidget::mousePressEvent(QMouseEvent* event)
{
    m_lastPos = event->position().toPoint();
    m_pressPos = m_lastPos;
    m_pressedAxis = -1;
    m_pressedButton = -1;

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
        if (axis != m_hoveredAxis || button != m_hoveredButton) {
            m_hoveredAxis = axis;
            m_hoveredButton = button;
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
    m_drag = Drag::None;
    m_pressedAxis = -1;
    m_pressedButton = -1;
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
    if (m_hoveredAxis != -1 || m_hoveredButton != -1) {
        m_hoveredAxis = -1;
        m_hoveredButton = -1;
        update();
    }
    QOpenGLWidget::leaveEvent(event);
}

} // namespace suspkin
