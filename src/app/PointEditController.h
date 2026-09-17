#pragma once

#include <QObject>
#include <QString>

#include <functional>

namespace suspkin {

class CoordinateEntry;
class HardpointModel;
class ViewportWidget;

/// The two ways a point is moved in the viewport itself: the arrows on the
/// selected marker, and the field X, Y or Z opens over it.
///
/// Both end in HardpointModel::setData() on a coordinate column, exactly as the
/// table's own cell does, so a point dragged and a point typed are the same
/// edit -- the parts, the solve, the wheels and the project all follow from the
/// model's own signal, and nothing here knows about any of them.
class PointEditController : public QObject {
    Q_OBJECT

public:
    /// Says something in the status line for @p milliseconds; 0 until replaced.
    using StatusLine = std::function<void(const QString& text, int milliseconds)>;

    PointEditController(ViewportWidget* viewport, HardpointModel* model, StatusLine status,
                        QObject* parent);

    /// Close the coordinate field without taking what was typed: the point it
    /// was opened on may be about to move or go.
    void dismiss();

private:
    /// Open the coordinate field on @p axis of the point at @p row, holding
    /// what the table has for it.
    void openCoordinateEntry(int row, int axis);
    /// Write one coordinate of one point through the table.
    void moveCoordinate(int row, int axis, double value);
    /// Say where a marker being dragged has got to. A readout, not an edit --
    /// the table is untouched until the drag ends.
    void showDragPosition(int row, int axis, double distance);
    /// Say that the point just picked can be dragged or typed at.
    void announceEditing(int current);

    ViewportWidget* m_viewport = nullptr;
    HardpointModel* m_model = nullptr;
    StatusLine m_status;
    /// The field X, Y and Z open over the viewport. A child of the viewport, so
    /// it sits over the marker it is about.
    CoordinateEntry* m_entry = nullptr;
    /// The row that field is open on, because the selection is not allowed to
    /// answer for it: what was typed belongs to the point it was opened on.
    int m_entryRow = -1;
};

} // namespace suspkin
