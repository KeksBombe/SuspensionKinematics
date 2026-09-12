#pragma once

#include "model/Hardpoint.h"
#include "model/HardpointConfig.h"

#include <QAbstractTableModel>

#include <vector>

namespace suspkin {

/// The one store behind the hardpoint configuration table: where every point
/// is, and what every point is for.
///
/// Everything the table shows comes out of here, and every edit goes back in
/// through setData(). Nothing else writes to it, and no cell keeps a copy of
/// what it is showing, so there is exactly one place a value can be wrong.
/// An accepted edit reports itself as dataChanged() over that row alone, which
/// is what keeps a keystroke from repainting the whole table.
///
/// An edit to a configuration cell is checked by validateHardpointConfig()
/// *before* it reaches the stored table. That check is the boundary: a change
/// that could not mean anything is refused and announced through editRejected(),
/// and one that is merely unfinished is stored and reported as a warning on the
/// row. Neither one leaves the store holding something the solver could not
/// read.
///
/// The name can be edited, and is guarded the same way a configuration cell is:
/// hardpointNameProblem() refuses an empty name, one already taken, or one the
/// workbook would read back as somebody else's coordinate, and the refusal goes
/// out through editRejected(). It is the key everything else is stored under,
/// so a rename carries the point's configuration and anything mirrored from it
/// across to the new name. What a rename means to the workbook -- the old rows
/// left behind, the new name arriving as a new point -- is not decided here:
/// it falls out of diffHardpoints(), which sees a name gone and a name added.
///
/// Rows are inserted and removed through beginInsertRows() and
/// beginRemoveRows() rather than a reset, so the sort and filter the panel has
/// in front of this model stay where the user left them.
class HardpointModel : public QAbstractTableModel {
    Q_OBJECT

public:
    enum Column {
        IndexColumn,   ///< the point's place in the table, 1-based
        NameColumn,    ///< the identifier; frozen in the view
        XColumn,
        YColumn,
        ZColumn,
        TypeColumn,    ///< the solver constraint, as a chip
        Part1Column,   ///< the two bodies that meet here
        Part2Column,
        BushingColumn, ///< which compliance bushing acts at this joint
        ColumnCount,
    };

    /// What the delegates ask for beyond the standard roles. Keeping them here
    /// rather than letting a delegate reach into the model is what lets the
    /// same chip and dropdown be pointed at any column that offers them.
    enum Role {
        PointTypeRole = Qt::UserRole + 1, ///< the PointType, as an int
        ChoicesRole,                      ///< QStringList this cell may be set to
        IssueLevelRole,                   ///< worst ConfigIssueLevel on the row, or -1
        IssueTextRole,                    ///< every issue on the row, one per line
    };

    explicit HardpointModel(QObject* parent = nullptr);

    void setTable(HardpointTable table);
    const HardpointTable& table() const { return m_table; }
    /// Drops the points and their configuration together: a table with no
    /// points has nothing left to describe.
    void clear();

    /// Put @p point in at @p row, which is clamped to the table. The caller has
    /// already checked the name; a name that is taken is refused here too,
    /// because two points under one key cannot both be written back.
    bool insertPoint(int row, const Hardpoint& point);
    /// Take out every row in @p rows, and the configuration stored under each
    /// of their names -- a description of a point that is not there is not
    /// something to keep.
    void removePoints(std::vector<int> rows);
    /// Rename the point at @p row. Refused, through editRejected(), for any name
    /// hardpointNameProblem() has something to say about.
    bool renamePoint(int row, const QString& name);

    /// The configuration for every point, keyed by name.
    void setConfig(HardpointConfigMap config);
    const HardpointConfigMap& config() const { return m_config; }
    HardpointConfig configAt(int row) const;

    /// The bodies the Part columns may name. Setting it re-checks every row,
    /// because a template that no longer describes a body has just made every
    /// row naming it wrong.
    void setBodyCatalog(BodyCatalog catalog);
    const BodyCatalog& bodyCatalog() const { return m_catalog; }

    /// What is wrong with @p row, worst first. Recomputed rather than cached:
    /// it is a handful of string comparisons, and a cache is one more thing
    /// that can disagree with the store.
    std::vector<ConfigIssue> issuesAt(int row) const;

    /// How many points have been given a type, and how many have something
    /// worth saying about them. For the panel's summary line.
    int configuredCount() const;
    int issueCount() const;

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;
    bool setData(const QModelIndex& index, const QVariant& value, int role) override;

    static bool isCoordinateColumn(int column);
    static bool isConfigColumn(int column);

signals:
    /// A coordinate of @p row was edited to a new value.
    void coordinateChanged(int row);
    /// The configuration of @p row was edited: its type, a body, or its bushing.
    void configChanged(int row);
    /// The point at @p row is now called @p to rather than @p from. Everything
    /// resolved by name -- the parts, the solve, the wheels -- has to look again.
    void pointRenamed(int row, const QString& from, const QString& to);
    /// An edit was refused because it could not mean anything. The row is the
    /// model row; the reason is meant to be shown to whoever typed it.
    void editRejected(int row, const QString& reason);

private:
    bool setCoordinate(const QModelIndex& index, const QVariant& value);
    bool setConfigField(int row, int column, const QVariant& value);
    QVariant configDisplay(const HardpointConfig& config, int column) const;
    QVariant tooltipFor(int row, int column) const;

    HardpointTable m_table;
    HardpointConfigMap m_config;
    BodyCatalog m_catalog;
};

} // namespace suspkin
