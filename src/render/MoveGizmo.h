#pragma once

#include "render/Camera.h"

#include <QColor>
#include <QPointF>
#include <QRectF>
#include <QSize>
#include <QString>
#include <QVector3D>

#include <array>

class QPainter;

namespace suspkin {

/// The three arrows a selected hardpoint gets, and what dragging one means.
///
/// The gizmo is laid out once a frame -- the point and a tip along each axis,
/// projected through the camera -- and that one layout answers everything:
/// where the arrows are drawn, which one the pointer is on, and how far along
/// its axis a drag has taken the point. It holds no GL and no widget, so what
/// "dragging 40 pixels along Y" comes to in millimetres is checkable without a
/// graphics context. See test_move_gizmo.
///
/// A drag is solved in space -- where the pointer's ray pierces the plane the
/// arm lies in -- rather than as pixels along the arrow. Under perspective the
/// two are not the same thing: an axis leaning away from the eye covers fewer
/// pixels the further along it you get, and a point dragged at a flat rate of
/// millimetres per pixel would slide out from under the pointer.
///
/// An arm is a fixed length on screen whatever the zoom, so the gizmo is the
/// same size on a wheel bearing and on a whole chassis. An arm pointing nearly
/// at the eye is the exception: it draws short and stops being draggable,
/// because a pixel along it would be worth an unbounded number of millimetres.
class MoveGizmo {
public:
    /// X, Y, Z -- the same order as Hardpoint::coord, so an axis index is a
    /// coordinate index and neither has to be translated into the other.
    static constexpr int kAxisCount = 3;
    /// How long an arm is on screen, in logical pixels.
    static constexpr qreal kArmLengthPx = 78.0;
    /// How near the pointer has to come to an arm to take hold of it.
    static constexpr qreal kPickReachPx = 9.0;
    /// An arm shorter than this on screen is pointing at the eye; it is drawn
    /// as a stub and cannot be dragged.
    static constexpr qreal kEdgeOnPx = 18.0;
    /// How much of an arm nearest the hub belongs to the marker underneath it
    /// rather than to the arm. Without it the three arms would meet on top of
    /// the point and a click meant for the marker would grab whichever arm
    /// answered first.
    static constexpr qreal kHubClearancePx = 11.0;

    /// The axis colours, shared with the navigation gizmo so that red, green
    /// and blue mean X, Y and Z everywhere in the window.
    static QColor axisColor(int axis);
    /// "X", "Y" or "Z" -- the letter drawn at the tip, which is also the key
    /// that types a coordinate for that axis.
    static QString axisLabel(int axis);

    /// One arm: where its head is, which way it moves the point, and how long
    /// it is in the world -- which is a fixed number of pixels on screen, so it
    /// grows as the camera backs away.
    struct Arm {
        QPointF tip;              ///< the arrowhead, in widget coordinates
        QVector3D direction;      ///< the world axis it slides along, unit
        double worldLength = 0.0; ///< what kArmLengthPx comes to at this zoom
        bool draggable = false;   ///< false when it is too edge-on to aim
        /// The plane a drag on this arm is read on: it holds the axis and faces
        /// the eye. Fixed when the gizmo is laid out, and the layout is
        /// captured when the arrow is taken hold of, so the plane cannot wobble
        /// with the pointer -- which is what would otherwise let a sideways
        /// sweep of the mouse slide the point along its own axis.
        QVector3D planeNormal;
    };

    /// The gizmo as one frame sees it. Laid out once and then used for
    /// painting, hit-testing and the drag itself, so the arrow the user grabs
    /// and the direction the point travels cannot come from two different
    /// cameras.
    struct Layout {
        bool visible = false;
        QPointF origin;  ///< the hardpoint itself, in widget coordinates
        QVector3D point; ///< and in the world, which is what a drag moves
        std::array<Arm, kAxisCount> arms{};
        /// What the pointer's ray is found with: the camera this layout was
        /// taken through, kept so a drag is answered against the same one the
        /// arrows were drawn from.
        QMatrix4x4 inverseViewProjection;
        QSize viewport;

        /// The area the gizmo draws in, letters included. What the viewport has
        /// to repaint, so an arrow is never blitted half away.
        QRectF bounds() const;
    };

    /// Lay the gizmo out around @p point. Not visible when the point is behind
    /// the eye or so close to it that a screen length cannot be measured.
    static Layout layoutAt(const Camera& camera, const QSize& viewport, const QVector3D& point);

    /// Which arm @p pos is on, or -1. The nearest one wins, so the arms can
    /// cross on screen without either becoming unpickable.
    static int axisAt(const Layout& layout, const QPointF& pos);

    /// How far along its axis, in world units, a drag from @p from to @p to
    /// takes the point. Zero for an arm that cannot be dragged.
    ///
    /// Always measured from where the drag started against the layout that was
    /// captured there: taken step by step against a moving gizmo it would drift
    /// away from the pointer.
    static double dragDistance(const Layout& layout, int axis, const QPointF& from,
                               const QPointF& to);

    /// Where the pointer at @p screen puts the point along @p axis, as a
    /// distance from the layout's own point. The two ends of a drag are two of
    /// these, and the drag is the difference between them.
    static double axisParameterAt(const Layout& layout, int axis, const QPointF& screen);

    static void paint(QPainter& painter, const Layout& layout, int hoveredAxis, int activeAxis);
};

} // namespace suspkin
