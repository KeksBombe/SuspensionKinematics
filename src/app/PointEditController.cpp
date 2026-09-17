#include "app/PointEditController.h"

#include "app/CoordinateEntry.h"
#include "app/HardpointModel.h"
#include "render/MoveGizmo.h"
#include "render/ViewportWidget.h"

#include <QLocale>

#include <utility>

namespace suspkin {

PointEditController::PointEditController(ViewportWidget* viewport, HardpointModel* model,
                                         StatusLine status, QObject* parent)
    : QObject(parent), m_viewport(viewport), m_model(model), m_status(std::move(status))
{
    // A child of the viewport: it opens over the marker it is about, and it is
    // gone as soon as the viewport is.
    m_entry = new CoordinateEntry(m_viewport);

    connect(m_viewport, &ViewportWidget::coordinateEntryRequested, this,
            &PointEditController::openCoordinateEntry);
    connect(m_entry, &CoordinateEntry::committed, this,
            [this](int axis, double value) { moveCoordinate(m_entryRow, axis, value); });
    connect(m_entry, &CoordinateEntry::closed, this, [this] {
        m_entryRow = -1;
        // The viewport takes the keyboard back, so the next X, Y or Z lands
        // where the first one did rather than nowhere.
        m_viewport->setFocus(Qt::OtherFocusReason);
    });

    // Nothing on the ribbon says that a selected point can be dragged or typed
    // at, so picking one in the viewport says it. The table's own selection
    // does not: there the coordinate columns are already in front of the user.
    connect(m_viewport, &ViewportWidget::hardpointSelectionEdited, this,
            [this](const QList<int>&, int current) { announceEditing(current); });

    // A drag says where it has got to the whole way along and what it came to
    // at the end. Only the end is an edit; the rest is a readout.
    connect(m_viewport, &ViewportWidget::hardpointDragging, this,
            &PointEditController::showDragPosition);
    connect(m_viewport, &ViewportWidget::hardpointMoved, this,
            [this](int row, int axis, double distance) {
                const HardpointTable& table = m_model->table();
                if (row < 0 || row >= static_cast<int>(table.points.size())) return;
                // Added to the table's own double, not read back off the
                // marker: the marker is a float, and the two coordinates the
                // drag did not touch have to come through it unchanged.
                moveCoordinate(row, axis,
                               table.points[static_cast<std::size_t>(row)].coord[axis] + distance);
            });
}

void PointEditController::dismiss()
{
    m_entry->dismiss();
}

void PointEditController::announceEditing(int current)
{
    if (current < 0 || !m_viewport->pointEditingEnabled()) return;
    const HardpointTable& table = m_model->table();
    if (current >= static_cast<int>(table.points.size())) return;
    m_status(tr("%1 — drag an arrow to move it, or press X, Y or Z to type a coordinate")
                 .arg(table.points[static_cast<std::size_t>(current)].name),
             6000);
}

void PointEditController::openCoordinateEntry(int row, int axis)
{
    const HardpointTable& table = m_model->table();
    if (row < 0 || row >= static_cast<int>(table.points.size())) return;

    QPointF anchor;
    if (!m_viewport->markerPosition(row, &anchor)) return; // off screen: nothing to open beside

    const Hardpoint& point = table.points[static_cast<std::size_t>(row)];
    m_entryRow = row;
    m_entry->openAt(anchor, point.name, axis, point.coord[axis]);
}

void PointEditController::moveCoordinate(int row, int axis, double value)
{
    if (row < 0 || row >= m_model->rowCount()) return;
    if (axis < 0 || axis >= 3) return;

    // Through the model, exactly as the table's own cell does it: what follows
    // -- the parts, the solve, the wheels, the edits file -- hangs off the
    // coordinateChanged() that this produces.
    const QModelIndex index = m_model->index(row, HardpointModel::XColumn + axis);
    m_model->setData(index, value, Qt::EditRole);
}

void PointEditController::showDragPosition(int row, int axis, double distance)
{
    const HardpointTable& table = m_model->table();
    if (row < 0 || row >= static_cast<int>(table.points.size())) return;

    const Hardpoint& point = table.points[static_cast<std::size_t>(row)];
    const QLocale locale;
    m_status(tr("%1  %2 %3 mm  (%4 mm)")
                 .arg(point.name, MoveGizmo::axisLabel(axis),
                      locale.toString(point.coord[axis] + distance, 'f', 3),
                      locale.toString(distance, 'f', 3)),
             0);
}

} // namespace suspkin
