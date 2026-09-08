#pragma once

#include "model/Hardpoint.h"

#include <QAbstractTableModel>

namespace suspkin {

/// Table model over a HardpointTable: one row per point, its name and its three
/// coordinates.
///
/// The name is deliberately read-only. It is the key the workbook is written
/// back through, so letting it be edited here would either rename cells the
/// user never looked at or quietly break the round trip.
class HardpointModel : public QAbstractTableModel {
    Q_OBJECT

public:
    enum Column { NameColumn, XColumn, YColumn, ZColumn, ColumnCount };

    explicit HardpointModel(QObject* parent = nullptr);

    void setTable(HardpointTable table);
    const HardpointTable& table() const { return m_table; }
    void clear();

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;
    bool setData(const QModelIndex& index, const QVariant& value, int role) override;

signals:
    /// A coordinate of @p row was edited to a new value.
    void coordinateChanged(int row);

private:
    HardpointTable m_table;
};

} // namespace suspkin
