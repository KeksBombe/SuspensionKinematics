#include "app/HardpointPanel.h"

#include "app/HardpointModel.h"

#include <QDoubleSpinBox>
#include <QHeaderView>
#include <QLineEdit>
#include <QSortFilterProxyModel>
#include <QStyledItemDelegate>
#include <QTableView>
#include <QVBoxLayout>

namespace suspkin {
namespace {

/// Millimetres, comfortably past anything a vehicle-sized model reaches.
constexpr double kCoordinateLimit = 1.0e7;
constexpr int kEditDecimals = 4;

/// The spin box a plain item delegate builds runs 0 to 99.99 with two decimals,
/// which would silently clamp a coordinate like -2068.622 to 0 the first time
/// anybody edited it. This is the whole reason the delegate exists.
class CoordinateDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    QWidget* createEditor(QWidget* parent, const QStyleOptionViewItem& option,
                          const QModelIndex& index) const override
    {
        QWidget* editor = QStyledItemDelegate::createEditor(parent, option, index);
        if (auto* spin = qobject_cast<QDoubleSpinBox*>(editor)) {
            spin->setRange(-kCoordinateLimit, kCoordinateLimit);
            spin->setDecimals(kEditDecimals);
            spin->setKeyboardTracking(false);
            spin->setAccelerated(true);
        }
        return editor;
    }
};

} // namespace

HardpointPanel::HardpointPanel(HardpointModel* model, QWidget* parent)
    : QWidget(parent), m_model(model)
{
    m_filter = new QLineEdit(this);
    m_filter->setClearButtonEnabled(true);
    m_filter->setPlaceholderText(tr("Filter, e.g. F_UCA"));

    m_proxy = new QSortFilterProxyModel(this);
    m_proxy->setSourceModel(m_model);
    m_proxy->setFilterKeyColumn(HardpointModel::NameColumn);
    m_proxy->setFilterCaseSensitivity(Qt::CaseInsensitive);
    // Sorting a coordinate column has to compare numbers, not their formatted
    // text, or -2068.6 sorts before -544.3.
    m_proxy->setSortRole(Qt::EditRole);

    m_view = new QTableView(this);
    m_view->setModel(m_proxy);
    m_view->setItemDelegateForColumn(HardpointModel::XColumn, new CoordinateDelegate(this));
    m_view->setItemDelegateForColumn(HardpointModel::YColumn, new CoordinateDelegate(this));
    m_view->setItemDelegateForColumn(HardpointModel::ZColumn, new CoordinateDelegate(this));
    m_view->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_view->setSelectionMode(QAbstractItemView::SingleSelection);
    m_view->setSortingEnabled(true);
    m_view->setAlternatingRowColors(true);
    m_view->setCornerButtonEnabled(false);
    m_view->verticalHeader()->setVisible(false);
    m_view->horizontalHeader()->setSectionResizeMode(HardpointModel::NameColumn,
                                                     QHeaderView::Stretch);
    for (int column = HardpointModel::XColumn; column <= HardpointModel::ZColumn; ++column)
        m_view->horizontalHeader()->setSectionResizeMode(column, QHeaderView::ResizeToContents);
    // Start in file order; clicking a header still sorts.
    m_view->sortByColumn(-1, Qt::AscendingOrder);
    // One click into a cell starts editing, which is what a coordinate table is for.
    m_view->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::SelectedClicked
                            | QAbstractItemView::EditKeyPressed | QAbstractItemView::AnyKeyPressed);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(6);
    layout->addWidget(m_filter);
    layout->addWidget(m_view, 1);

    connect(m_filter, &QLineEdit::textChanged, m_proxy,
            &QSortFilterProxyModel::setFilterFixedString);

    connect(m_view->selectionModel(), &QItemSelectionModel::currentRowChanged, this,
            [this](const QModelIndex& current) {
                if (m_syncing) return;
                emit rowSelected(current.isValid() ? m_proxy->mapToSource(current).row() : -1);
            });
}

void HardpointPanel::setSelectedRow(int row)
{
    m_syncing = true;
    if (row < 0 || row >= m_model->rowCount()) {
        m_view->selectionModel()->clearSelection();
        m_view->selectionModel()->clearCurrentIndex();
    } else {
        const QModelIndex source = m_model->index(row, HardpointModel::NameColumn);
        const QModelIndex proxy = m_proxy->mapFromSource(source);
        // An active filter can hide the row the viewport just picked; there is
        // nothing sensible to select then, so the table simply stays put.
        if (proxy.isValid()) {
            m_view->selectionModel()->setCurrentIndex(
                proxy, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
            m_view->scrollTo(proxy, QAbstractItemView::EnsureVisible);
        }
    }
    m_syncing = false;
}

} // namespace suspkin
