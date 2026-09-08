#include "app/HardpointModel.h"

#include <QLocale>

#include <utility>

namespace suspkin {
namespace {

/// Enough to read a millimetre coordinate at the precision anyone works to,
/// without turning the column into a wall of digits. The stored value keeps its
/// full double precision either way.
constexpr int kDisplayDecimals = 3;

int axisOf(int column) { return column - HardpointModel::XColumn; }

bool isCoordinateColumn(int column)
{
    return column >= HardpointModel::XColumn && column <= HardpointModel::ZColumn;
}

} // namespace

HardpointModel::HardpointModel(QObject* parent) : QAbstractTableModel(parent) {}

void HardpointModel::setTable(HardpointTable table)
{
    beginResetModel();
    m_table = std::move(table);
    endResetModel();
}

void HardpointModel::clear()
{
    setTable(HardpointTable{});
}

int HardpointModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_table.points.size());
}

int HardpointModel::columnCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : ColumnCount;
}

QVariant HardpointModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() >= rowCount()) return {};
    const Hardpoint& point = m_table.points[static_cast<std::size_t>(index.row())];

    switch (role) {
    case Qt::DisplayRole:
        if (index.column() == NameColumn) return point.name;
        return QLocale().toString(point.coord[axisOf(index.column())], 'f', kDisplayDecimals);

    case Qt::EditRole:
        // The unrounded value, so opening an editor and closing it again cannot
        // truncate a coordinate that was never touched.
        if (index.column() == NameColumn) return point.name;
        return point.coord[axisOf(index.column())];

    case Qt::ToolTipRole:
        return QStringLiteral("%1\nX %2\nY %3\nZ %4 (mm)")
            .arg(point.name)
            .arg(point.coord[0], 0, 'f', 4)
            .arg(point.coord[1], 0, 'f', 4)
            .arg(point.coord[2], 0, 'f', 4);

    case Qt::TextAlignmentRole:
        if (index.column() == NameColumn) return int(Qt::AlignLeft | Qt::AlignVCenter);
        return int(Qt::AlignRight | Qt::AlignVCenter);

    default:
        return {};
    }
}

QVariant HardpointModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (role != Qt::DisplayRole) return {};
    if (orientation == Qt::Vertical) return section + 1;

    switch (section) {
    case NameColumn: return tr("Hardpoint");
    case XColumn: return tr("X [mm]");
    case YColumn: return tr("Y [mm]");
    case ZColumn: return tr("Z [mm]");
    default: return {};
    }
}

Qt::ItemFlags HardpointModel::flags(const QModelIndex& index) const
{
    Qt::ItemFlags result = QAbstractTableModel::flags(index);
    if (index.isValid() && isCoordinateColumn(index.column())) result |= Qt::ItemIsEditable;
    return result;
}

bool HardpointModel::setData(const QModelIndex& index, const QVariant& value, int role)
{
    if (role != Qt::EditRole || !index.isValid() || index.row() >= rowCount()) return false;
    if (!isCoordinateColumn(index.column())) return false;

    bool ok = false;
    const double parsed = value.toDouble(&ok);
    if (!ok) return false;

    double& stored = m_table.points[static_cast<std::size_t>(index.row())].coord[axisOf(index.column())];
    // Returning false for a no-op keeps the document from being marked dirty by
    // someone opening an editor and pressing Enter.
    if (stored == parsed) return false;

    stored = parsed;
    emit dataChanged(index, index, { Qt::DisplayRole, Qt::EditRole, Qt::ToolTipRole });
    emit coordinateChanged(index.row());
    return true;
}

} // namespace suspkin
