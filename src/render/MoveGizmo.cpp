#include "render/MoveGizmo.h"

#include <QFont>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>

namespace suspkin {
namespace {

const QVector3D kAxisDirections[MoveGizmo::kAxisCount] = {
    { 1, 0, 0 },
    { 0, 1, 0 },
    { 0, 0, 1 },
};

constexpr qreal kShaftWidthPx = 2.4;
constexpr qreal kHeadLengthPx = 13.0;
constexpr qreal kHeadWidthPx = 8.0;
constexpr qreal kHubRadiusPx = 3.2;
/// Room past the arrowhead for the letter, and what bounds() has to allow for.
constexpr qreal kLabelGapPx = 9.0;
constexpr qreal kLabelRoomPx = 16.0;

qreal lengthOf(const QPointF& v)
{
    return std::hypot(v.x(), v.y());
}

/// How many pixels one world unit covers at @p point's depth.
///
/// Measured across the view, where perspective is flat over the few pixels this
/// asks about, so the answer holds for every direction the arms can take.
double pixelsPerUnitAt(const QMatrix4x4& viewProjection, const QSize& viewport,
                       const QVector3D& point, const QVector3D& across, const QPointF& origin)
{
    QPointF offset;
    float depth = 0.0f;
    if (!Camera::projectTo(viewProjection, point + across, viewport, &offset, &depth)) return 0.0;
    return lengthOf(offset - origin);
}

/// The plane that holds @p direction and faces @p eye as squarely as it can.
///
/// Null when the axis runs at the eye: every plane through it is then edge-on
/// and there is no sensible answer -- the same case kEdgeOnPx rules out.
QVector3D dragPlaneNormal(const QVector3D& point, const QVector3D& direction, const QVector3D& eye)
{
    const QVector3D toEye = (eye - point).normalized();
    const QVector3D normal = toEye - direction * QVector3D::dotProduct(direction, toEye);
    return normal.length() < 1e-4f ? QVector3D{} : normal.normalized();
}

MoveGizmo::Arm armAlong(const QMatrix4x4& viewProjection, const QSize& viewport,
                        const QVector3D& point, const QVector3D& direction, double armLength,
                        const QVector3D& eye, const QPointF& origin)
{
    MoveGizmo::Arm arm;
    arm.direction = direction;
    arm.worldLength = armLength;
    arm.tip = origin;
    arm.planeNormal = dragPlaneNormal(point, direction, eye);

    QPointF tip;
    float depth = 0.0f;
    if (!Camera::projectTo(viewProjection, point + direction * static_cast<float>(armLength),
                           viewport, &tip, &depth))
        return arm; // the head is behind the eye: nothing to draw, nothing to grab

    arm.tip = tip;
    // How much of the arm the screen shows is also how well it can be aimed
    // along: an arm running at the eye is a few pixels of arrow that would move
    // the point by metres.
    arm.draggable = !arm.planeNormal.isNull() && lengthOf(tip - origin) >= MoveGizmo::kEdgeOnPx;
    return arm;
}

/// The point @p screen picks out at @p ndcZ, back in the world.
QVector3D unproject(const QMatrix4x4& inverseViewProjection, const QSize& viewport,
                    const QPointF& screen, float ndcZ)
{
    const QVector4D ndc(static_cast<float>(2.0 * screen.x() / viewport.width() - 1.0),
                        static_cast<float>(1.0 - 2.0 * screen.y() / viewport.height()), ndcZ, 1.0f);
    const QVector4D world = inverseViewProjection * ndc;
    if (std::abs(world.w()) < 1e-12f) return {};
    return world.toVector3D() / world.w();
}

/// Where @p pos falls against the arm, in pixels along it and away from it.
struct Reach {
    qreal distancePx = 0.0; ///< from the arm's line
    qreal alongPx = 0.0;    ///< 0 at the hardpoint, the arm's length at the head
    qreal lengthPx = 0.0;   ///< the arm as it is on screen
};

Reach reachTo(const QPointF& origin, const QPointF& tip, const QPointF& pos)
{
    const QPointF axis = tip - origin;
    const qreal length = lengthOf(axis);
    if (length < 1e-3) return { lengthOf(pos - origin), 0.0, 0.0 };

    const QPointF unit = axis / length;
    const qreal along = QPointF::dotProduct(pos - origin, unit);
    const QPointF nearest = origin + unit * std::clamp(along, 0.0, length);
    return { lengthOf(pos - nearest), along, length };
}

void paintArm(QPainter& painter, const QPointF& origin, const MoveGizmo::Arm& arm, int axis,
              bool hovered, bool active)
{
    const QPointF along = arm.tip - origin;
    const qreal onScreen = lengthOf(along);
    if (onScreen < 2.0) return; // pointing at the eye; the hub alone says it is there

    const QPointF unit = along / onScreen;
    const QPointF normal(-unit.y(), unit.x());

    QColor colour = MoveGizmo::axisColor(axis);
    if (active || hovered) colour = colour.lighter(active ? 145 : 125);
    // An arm that cannot be dragged is still worth drawing -- it says which way
    // the axis goes -- but it must not look like a handle.
    if (!arm.draggable) colour.setAlpha(110);

    const QPointF neck = arm.tip - unit * kHeadLengthPx;

    // A dark backing under the shaft, so an arrow stays readable over pale
    // geometry as well as over the dark ground.
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(QColor(12, 14, 18, 130), kShaftWidthPx + 2.4, Qt::SolidLine, Qt::RoundCap));
    painter.drawLine(origin, neck);

    painter.setPen(QPen(colour, active ? kShaftWidthPx + 1.0 : kShaftWidthPx, Qt::SolidLine,
                        Qt::RoundCap));
    painter.drawLine(origin, neck);

    QPainterPath head;
    head.moveTo(arm.tip);
    head.lineTo(neck + normal * (kHeadWidthPx / 2.0));
    head.lineTo(neck - normal * (kHeadWidthPx / 2.0));
    head.closeSubpath();
    painter.setPen(QPen(QColor(12, 14, 18, 130), 1.0));
    painter.setBrush(colour);
    painter.drawPath(head);

    const QPointF labelAt = arm.tip + unit * (kHeadLengthPx * 0.5 + kLabelGapPx);
    const QRectF labelRect(labelAt - QPointF(kLabelRoomPx / 2.0, kLabelRoomPx / 2.0),
                           QSizeF(kLabelRoomPx, kLabelRoomPx));
    QFont font = painter.font();
    font.setBold(true);
    font.setPointSizeF(std::max(7.5, font.pointSizeF() - 0.5));
    painter.setFont(font);
    painter.setPen(QPen(QColor(12, 14, 18, 150), 1.0));
    painter.drawText(labelRect.translated(0.7, 0.7), Qt::AlignCenter, MoveGizmo::axisLabel(axis));
    painter.setPen(colour);
    painter.drawText(labelRect, Qt::AlignCenter, MoveGizmo::axisLabel(axis));
}

} // namespace

QColor MoveGizmo::axisColor(int axis)
{
    switch (axis) {
    case 0:  return QColor(226, 88, 96);  // X, red
    case 1:  return QColor(154, 205, 60); // Y, green
    default: return QColor(70, 150, 225); // Z, blue
    }
}

QString MoveGizmo::axisLabel(int axis)
{
    // Not translated: X, Y and Z are the axes of the coordinate frame the whole
    // program is written in, and they are the keys that type a coordinate.
    switch (axis) {
    case 0:  return QStringLiteral("X");
    case 1:  return QStringLiteral("Y");
    default: return QStringLiteral("Z");
    }
}

QRectF MoveGizmo::Layout::bounds() const
{
    if (!visible) return {};

    QRectF area(origin, QSizeF(1.0, 1.0));
    for (const Arm& arm : arms) area = area.united(QRectF(arm.tip, QSizeF(1.0, 1.0)));
    const qreal room = kHeadLengthPx + kLabelGapPx + kLabelRoomPx;
    return area.adjusted(-room, -room, room, room);
}

MoveGizmo::Layout MoveGizmo::layoutAt(const Camera& camera, const QSize& viewport,
                                      const QVector3D& point)
{
    Layout layout;
    if (viewport.width() <= 0 || viewport.height() <= 0) return layout;

    const float aspect =
        static_cast<float>(viewport.width()) / static_cast<float>(viewport.height());
    const QMatrix4x4 viewProjection = camera.viewProjectionMatrix(aspect);

    float depth = 0.0f;
    if (!Camera::projectTo(viewProjection, point, viewport, &layout.origin, &depth)) return layout;

    const double pixelsPerUnit =
        pixelsPerUnitAt(viewProjection, viewport, point, camera.right(), layout.origin);
    if (pixelsPerUnit <= 1e-9) return layout; // on top of the eye: no scale to work in

    const double armLength = kArmLengthPx / pixelsPerUnit;
    for (int axis = 0; axis < kAxisCount; ++axis)
        layout.arms[static_cast<std::size_t>(axis)] =
            armAlong(viewProjection, viewport, point, kAxisDirections[axis], armLength,
                     camera.eye(), layout.origin);

    layout.point = point;
    layout.viewport = viewport;
    layout.inverseViewProjection = viewProjection.inverted();
    layout.visible = true;
    return layout;
}

int MoveGizmo::axisAt(const Layout& layout, const QPointF& pos)
{
    if (!layout.visible) return -1;

    int best = -1;
    qreal bestDistance = kPickReachPx;
    for (int axis = 0; axis < kAxisCount; ++axis) {
        const Arm& arm = layout.arms[static_cast<std::size_t>(axis)];
        if (!arm.draggable) continue;

        const Reach reach = reachTo(layout.origin, arm.tip, pos);
        // Past the arrowhead is still the arrow -- that is where the letter is
        // -- but the hub end belongs to the marker underneath it, which is what
        // a click there is nearly always meant for.
        if (reach.alongPx < kHubClearancePx) continue;
        if (reach.alongPx > reach.lengthPx + kHeadLengthPx + kLabelGapPx) continue;
        if (reach.distancePx >= bestDistance) continue;

        bestDistance = reach.distancePx;
        best = axis;
    }
    return best;
}

double MoveGizmo::axisParameterAt(const Layout& layout, int axis, const QPointF& screen)
{
    // Where the pointer's ray pierces the arm's own plane, measured along the
    // arm from the point it belongs to. Exact under perspective, which pixels
    // along the arrow are not.
    const Arm& arm = layout.arms[static_cast<std::size_t>(axis)];
    const QVector3D near = unproject(layout.inverseViewProjection, layout.viewport, screen, -1.0f);
    const QVector3D far = unproject(layout.inverseViewProjection, layout.viewport, screen, 1.0f);

    const QVector3D ray = far - near;
    const float crossing = QVector3D::dotProduct(arm.planeNormal, ray);
    // The ray is running along the plane rather than through it: there is no
    // crossing to read a distance from.
    if (std::abs(crossing) < 1e-9f) return 0.0;

    const float travel = QVector3D::dotProduct(arm.planeNormal, layout.point - near) / crossing;
    const QVector3D hit = near + ray * travel;
    return static_cast<double>(QVector3D::dotProduct(hit - layout.point, arm.direction));
}

double MoveGizmo::dragDistance(const Layout& layout, int axis, const QPointF& from,
                               const QPointF& to)
{
    if (!layout.visible || axis < 0 || axis >= kAxisCount) return 0.0;
    if (!layout.arms[static_cast<std::size_t>(axis)].draggable) return 0.0;

    return axisParameterAt(layout, axis, to) - axisParameterAt(layout, axis, from);
}

void MoveGizmo::paint(QPainter& painter, const Layout& layout, int hoveredAxis, int activeAxis)
{
    if (!layout.visible) return;

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);

    // Furthest arm first, so the one coming towards the viewer is drawn on top
    // of the one going away from them where they cross.
    std::array<int, kAxisCount> order{ 0, 1, 2 };
    std::sort(order.begin(), order.end(), [&](int a, int b) {
        return lengthOf(layout.arms[static_cast<std::size_t>(a)].tip - layout.origin)
             < lengthOf(layout.arms[static_cast<std::size_t>(b)].tip - layout.origin);
    });

    for (const int axis : order) {
        const bool active = (axis == activeAxis);
        paintArm(painter, layout.origin, layout.arms[static_cast<std::size_t>(axis)], axis,
                 axis == hoveredAxis && activeAxis < 0, active);
    }

    // The hub last: it is the point itself, and nothing should cover it.
    painter.setPen(QPen(QColor(12, 14, 18, 170), 1.2));
    painter.setBrush(QColor(245, 247, 250, 220));
    painter.drawEllipse(layout.origin, kHubRadiusPx, kHubRadiusPx);
    painter.restore();
}

} // namespace suspkin
