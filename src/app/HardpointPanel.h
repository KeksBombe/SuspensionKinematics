#pragma once

#include <QString>
#include <QWidget>

class QLabel;
class QLineEdit;
class QSortFilterProxyModel;

namespace suspkin {

class FrozenColumnView;
class HardpointModel;

/// The hardpoint configuration table: where every point is, what the solver
/// does with it, which bodies meet there, and which bushing acts at it.
///
/// It is a view and nothing else. It holds no coordinates and no configuration
/// of its own -- every cell reads from HardpointModel and every edit is an edit
/// on HardpointModel, which validates it and reports back. Nothing in here
/// knows what a kinematic solve is, and the solver does not know this exists.
///
/// Three things make it usable at the size a real car reaches:
///
/// - The header stays put while the rows scroll, because a table view keeps it
///   outside the scrolled area.
/// - The number and the point name are frozen: a second view of the same model
///   showing only those two columns, laid over the left edge, so the identifier
///   is still there when the far columns are scrolled to.
/// - Only what is on screen is built. A table view creates no per-row widgets,
///   and this one adds none: the chips and dropdowns are delegates, which draw
///   into the cell rather than living in it, and an editor exists only for the
///   cell being edited. Rows are a fixed height for the same reason, so
///   scrolling never has to ask about a row it is not showing.
///
/// Rows can be sorted and filtered, so nothing outside may assume the view's
/// row order matches the model's; the two signals here always speak in model
/// rows, which is what the viewport indexes by.
class HardpointPanel : public QWidget {
    Q_OBJECT

public:
    explicit HardpointPanel(HardpointModel* model, QWidget* parent = nullptr);

    QSize sizeHint() const override;

public slots:
    /// Select @p row, or clear the selection when it is negative.
    void setSelectedRow(int row);

signals:
    /// The selected model row, or -1 when nothing is selected.
    void rowSelected(int row);

protected:
    /// The table's colours are derived from the palette, so it follows the
    /// application between light and dark rather than carrying its own theme.
    void changeEvent(QEvent* event) override;

private:
    void buildTable();
    void applyTheme();
    /// The line under the table: what is wrong with the selected row, or how
    /// much of the table has been filled in.
    void updateStatusLine();
    void showRejection(int row, const QString& reason);
    int currentModelRow() const;

    HardpointModel* m_model = nullptr;
    QSortFilterProxyModel* m_proxy = nullptr;
    FrozenColumnView* m_view = nullptr;
    QLineEdit* m_filter = nullptr;
    QLabel* m_status = nullptr;

    /// The last edit the model refused, held until the user moves on, so the
    /// answer to "why did nothing happen" is still on screen.
    QString m_rejection;

    /// Set while the selection is being driven from outside, so echoing it
    /// straight back out does not fight with whatever caused it.
    bool m_syncing = false;
    /// Set while the stylesheet is being applied, because applying one is
    /// itself a style change.
    bool m_theming = false;
};

} // namespace suspkin
