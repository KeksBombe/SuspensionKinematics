#pragma once

#include <QWidget>

class QLineEdit;
class QSortFilterProxyModel;
class QTableView;

namespace suspkin {

class HardpointModel;

/// The hardpoint list: a filter box over an editable table of coordinates.
///
/// Rows can be sorted and filtered, so nothing outside may assume the view's
/// row order matches the model's; the two signals here always speak in model
/// rows, which is what the viewport indexes by.
class HardpointPanel : public QWidget {
    Q_OBJECT

public:
    explicit HardpointPanel(HardpointModel* model, QWidget* parent = nullptr);

public slots:
    /// Select @p row, or clear the selection when it is negative.
    void setSelectedRow(int row);

signals:
    /// The selected model row, or -1 when nothing is selected.
    void rowSelected(int row);

private:
    HardpointModel* m_model = nullptr;
    QSortFilterProxyModel* m_proxy = nullptr;
    QTableView* m_view = nullptr;
    QLineEdit* m_filter = nullptr;

    /// Set while the selection is being driven from outside, so echoing it
    /// straight back out does not fight with whatever caused it.
    bool m_syncing = false;
};

} // namespace suspkin
