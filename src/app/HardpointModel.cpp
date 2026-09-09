#include "app/HardpointModel.h"

#include <QLocale>

#include <cstddef>
#include <utility>

namespace suspkin {
namespace {

/// Enough to read a millimetre coordinate at the precision anyone works to,
/// without turning the column into a wall of digits. The stored value keeps its
/// full double precision either way.
constexpr int kDisplayDecimals = 3;

/// What an empty body or a rigid joint reads as. A blank cell looks like a
/// column that failed to draw; a dash looks like an answer.
const QChar kEmDash(0x2014);

int axisOf(int column) { return column - HardpointModel::XColumn; }

QStringList pointTypeLabels()
{
    QStringList labels;
    for (const PointType type : kPointTypes) labels.append(pointTypeLabel(type));
    return labels;
}

/// The int a Type cell is edited with, back into a type. Anything outside the
/// enum is refused rather than clamped: it can only be a bug on the way in.
bool pointTypeFromInt(int value, PointType* type)
{
    for (const PointType candidate : kPointTypes) {
        if (static_cast<int>(candidate) != value) continue;
        *type = candidate;
        return true;
    }
    return false;
}

} // namespace

HardpointModel::HardpointModel(QObject* parent) : QAbstractTableModel(parent) {}

bool HardpointModel::isCoordinateColumn(int column)
{
    return column >= XColumn && column <= ZColumn;
}

bool HardpointModel::isConfigColumn(int column)
{
    return column >= TypeColumn && column <= BushingColumn;
}

void HardpointModel::setTable(HardpointTable table)
{
    beginResetModel();
    m_table = std::move(table);
    endResetModel();
}

void HardpointModel::clear()
{
    beginResetModel();
    m_table = HardpointTable{};
    m_config.clear();
    endResetModel();
}

void HardpointModel::setConfig(HardpointConfigMap config)
{
    m_config = std::move(config);
    if (rowCount() == 0) return;
    // Not a reset: the rows are the same rows, and resetting would throw away
    // the selection and whatever the user was scrolled to.
    emit dataChanged(index(0, IndexColumn), index(rowCount() - 1, ColumnCount - 1));
}

void HardpointModel::setBodyCatalog(BodyCatalog catalog)
{
    m_catalog = std::move(catalog);
    if (rowCount() == 0) return;
    // The values did not change, but what they are checked against did, so the
    // annotations on them may have.
    emit dataChanged(index(0, IndexColumn), index(rowCount() - 1, ColumnCount - 1),
                     { Qt::ToolTipRole, IssueLevelRole, IssueTextRole });
}

HardpointConfig HardpointModel::configAt(int row) const
{
    if (row < 0 || row >= rowCount()) return {};
    return m_config.value(m_table.points[static_cast<std::size_t>(row)].name);
}

std::vector<ConfigIssue> HardpointModel::issuesAt(int row) const
{
    if (row < 0 || row >= rowCount()) return {};
    const HardpointConfig config = configAt(row);
    // A point nobody has touched is not a point with something wrong with it.
    if (config.isEmpty()) return {};
    return validateHardpointConfig(config, m_catalog);
}

int HardpointModel::configuredCount() const
{
    int count = 0;
    for (const Hardpoint& point : m_table.points)
        if (m_config.value(point.name).type != PointType::Unassigned) ++count;
    return count;
}

int HardpointModel::issueCount() const
{
    int count = 0;
    for (int row = 0; row < rowCount(); ++row)
        if (!issuesAt(row).empty()) ++count;
    return count;
}

int HardpointModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_table.points.size());
}

int HardpointModel::columnCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : ColumnCount;
}

QVariant HardpointModel::configDisplay(const HardpointConfig& config, int column) const
{
    switch (column) {
    case TypeColumn: return pointTypeLabel(config.type);
    case Part1Column: return config.part1.isEmpty() ? QString(kEmDash) : config.part1;
    case Part2Column: return config.part2.isEmpty() ? QString(kEmDash) : config.part2;
    case BushingColumn:
        return config.bushing == kNoBushing ? QString(kEmDash) : QString::number(config.bushing);
    default: return {};
    }
}

QVariant HardpointModel::tooltipFor(int row, int column) const
{
    const Hardpoint& point = m_table.points[static_cast<std::size_t>(row)];
    const HardpointConfig config = configAt(row);

    QString text;
    switch (column) {
    case TypeColumn:
        text = QStringLiteral("%1\n%2").arg(pointTypeLabel(config.type),
                                            pointTypeDescription(config.type));
        break;
    case Part1Column:
    case Part2Column:
        text = tr("The two bodies that meet at %1.\n\nWhat this column may name comes from the "
                  "project's linkage template.")
                   .arg(point.name);
        break;
    case BushingColumn:
        text = tr("The compliance bushing acting at %1, by index into your stiffness and damping "
                  "map. %2 is a rigid joint.")
                   .arg(point.name)
                   .arg(kNoBushing);
        break;
    default:
        text = QStringLiteral("%1\nX %2\nY %3\nZ %4 (mm)")
                   .arg(point.name)
                   .arg(point.coord[0], 0, 'f', 4)
                   .arg(point.coord[1], 0, 'f', 4)
                   .arg(point.coord[2], 0, 'f', 4);
        break;
    }

    const QStringList issues = issueMessages(issuesAt(row));
    if (issues.isEmpty()) return text;
    return text + QStringLiteral("\n\n") + issues.join(QStringLiteral("\n"));
}

QVariant HardpointModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() >= rowCount()) return {};

    const int row = index.row();
    const int column = index.column();
    const Hardpoint& point = m_table.points[static_cast<std::size_t>(row)];
    const HardpointConfig config = configAt(row);

    switch (role) {
    case Qt::DisplayRole:
        switch (column) {
        case IndexColumn: return row + 1;
        case NameColumn: return point.name;
        case XColumn:
        case YColumn:
        case ZColumn: return QLocale().toString(point.coord[axisOf(column)], 'f', kDisplayDecimals);
        default: return configDisplay(config, column);
        }

    case Qt::EditRole:
        // The unrounded value, so opening an editor and closing it again cannot
        // truncate a coordinate that was never touched. It is also the role the
        // view sorts on, so every column answers it with something comparable.
        switch (column) {
        case IndexColumn: return row + 1;
        case NameColumn: return point.name;
        case XColumn:
        case YColumn:
        case ZColumn: return point.coord[axisOf(column)];
        case TypeColumn: return static_cast<int>(config.type);
        case Part1Column: return config.part1;
        case Part2Column: return config.part2;
        case BushingColumn: return config.bushing;
        default: return {};
        }

    case Qt::ToolTipRole: return tooltipFor(row, column);

    case Qt::TextAlignmentRole:
        switch (column) {
        case IndexColumn:
        case BushingColumn: return int(Qt::AlignCenter);
        case XColumn:
        case YColumn:
        case ZColumn: return int(Qt::AlignRight | Qt::AlignVCenter);
        default: return int(Qt::AlignLeft | Qt::AlignVCenter);
        }

    case PointTypeRole: return static_cast<int>(config.type);

    case ChoicesRole:
        if (column == TypeColumn) return pointTypeLabels();
        if (column == Part1Column || column == Part2Column) return m_catalog.bodies;
        return {};

    case IssueLevelRole: {
        const std::vector<ConfigIssue> issues = issuesAt(row);
        if (issues.empty()) return -1;
        return static_cast<int>(hasError(issues) ? ConfigIssueLevel::Error
                                                 : ConfigIssueLevel::Warning);
    }

    case IssueTextRole: return issueMessages(issuesAt(row)).join(QLatin1Char('\n'));

    default: return {};
    }
}

QVariant HardpointModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation == Qt::Vertical) return role == Qt::DisplayRole ? QVariant(section + 1) : QVariant();

    if (role == Qt::DisplayRole) {
        switch (section) {
        case IndexColumn: return QStringLiteral("#");
        case NameColumn: return tr("Point Name");
        case XColumn: return tr("X [mm]");
        case YColumn: return tr("Y [mm]");
        case ZColumn: return tr("Z [mm]");
        case TypeColumn: return tr("Point Type");
        case Part1Column: return tr("Part 1");
        case Part2Column: return tr("Part 2");
        case BushingColumn: return tr("Bushing");
        default: return {};
        }
    }

    if (role == Qt::ToolTipRole) {
        switch (section) {
        case IndexColumn: return tr("The point's place in the workbook, which is the order it is "
                                    "written back in.");
        case NameColumn: return tr("The name the workbook gives this point. It is the key "
                                   "everything else is stored under, so it cannot be edited here.");
        case TypeColumn: return tr("What the solver does with this point: hold it to the chassis, "
                                   "solve for it, or carry it along with a body that moves.");
        case Part1Column:
        case Part2Column: return tr("The two bodies that meet at this point, from the project's "
                                    "linkage template.");
        case BushingColumn: return tr("Which compliance bushing acts here, by index. %1 is a "
                                      "rigid joint.")
                                       .arg(kNoBushing);
        default: return {};
        }
    }

    if (role == Qt::TextAlignmentRole) {
        if (section == IndexColumn || section == BushingColumn) return int(Qt::AlignCenter);
        if (isCoordinateColumn(section)) return int(Qt::AlignRight | Qt::AlignVCenter);
        return int(Qt::AlignLeft | Qt::AlignVCenter);
    }

    return {};
}

Qt::ItemFlags HardpointModel::flags(const QModelIndex& index) const
{
    Qt::ItemFlags result = QAbstractTableModel::flags(index);
    if (!index.isValid()) return result;
    if (isCoordinateColumn(index.column()) || isConfigColumn(index.column()))
        result |= Qt::ItemIsEditable;
    return result;
}

bool HardpointModel::setData(const QModelIndex& index, const QVariant& value, int role)
{
    if (role != Qt::EditRole || !index.isValid() || index.row() >= rowCount()) return false;
    if (isCoordinateColumn(index.column())) return setCoordinate(index, value);
    if (isConfigColumn(index.column())) return setConfigField(index.row(), index.column(), value);
    return false;
}

bool HardpointModel::setCoordinate(const QModelIndex& index, const QVariant& value)
{
    bool ok = false;
    const double parsed = value.toDouble(&ok);
    if (!ok) return false;

    double& stored = m_table.points[static_cast<std::size_t>(index.row())]
                         .coord[axisOf(index.column())];
    // Returning false for a no-op keeps the document from being marked dirty by
    // someone opening an editor and pressing Enter.
    if (stored == parsed) return false;

    stored = parsed;
    emit dataChanged(index, index, { Qt::DisplayRole, Qt::EditRole, Qt::ToolTipRole });
    emit coordinateChanged(index.row());
    return true;
}

bool HardpointModel::setConfigField(int row, int column, const QVariant& value)
{
    const QString& name = m_table.points[static_cast<std::size_t>(row)].name;
    const HardpointConfig current = m_config.value(name);
    HardpointConfig candidate = current;

    switch (column) {
    case TypeColumn: {
        PointType type = PointType::Unassigned;
        if (!pointTypeFromInt(value.toInt(), &type)) return false;
        candidate.type = type;
        break;
    }
    case Part1Column: candidate.part1 = value.toString(); break;
    case Part2Column: candidate.part2 = value.toString(); break;
    case BushingColumn: {
        bool ok = false;
        const int bushing = value.toInt(&ok);
        if (!ok) return false;
        candidate.bushing = bushing;
        break;
    }
    default: return false;
    }

    if (candidate == current) return false; // nothing to store, nothing to save

    // The boundary. Anything that could not mean something is refused here,
    // before the store holds it; anything merely unfinished goes in and comes
    // back out as a warning on the row.
    const std::vector<ConfigIssue> issues = validateHardpointConfig(candidate, m_catalog);
    if (hasError(issues)) {
        for (const ConfigIssue& issue : issues) {
            if (issue.level != ConfigIssueLevel::Error) continue;
            emit editRejected(row, issue.message);
            break;
        }
        return false;
    }

    m_config.insert(name, candidate);
    // One row, not the table: an edit to a cell can change the annotation on
    // its neighbours, and nothing beyond that.
    emit dataChanged(index(row, IndexColumn), index(row, ColumnCount - 1),
                     { Qt::DisplayRole, Qt::EditRole, Qt::ToolTipRole, PointTypeRole,
                       IssueLevelRole, IssueTextRole });
    emit configChanged(row);
    return true;
}

} // namespace suspkin
