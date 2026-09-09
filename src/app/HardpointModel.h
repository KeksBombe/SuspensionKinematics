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
/// The name is deliberately read-only. It is the key the workbook is written
/// back through -- and the key the configuration itself is stored under -- so
/// letting it be edited here would either rename cells the user never looked at
/// or quietly break the round trip.
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
