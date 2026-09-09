#include "app/HardpointPanel.h"

#include "app/HardpointDelegates.h"
#include "app/HardpointModel.h"

#include <QEvent>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QResizeEvent>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSortFilterProxyModel>
#include <QTableView>
#include <QVBoxLayout>

#include <vector>

namespace suspkin {
namespace {

/// Every row the same height, which is both how the table reads and what keeps
/// scrolling cheap: the view never has to ask a row it is not showing how tall
/// it would like to be.
constexpr int kRowHeight = 26;

/// How wide each column opens. Interactive from then on -- an engineer with
/// long body names and a wide screen should be able to say so.
struct ColumnWidth {
    int column;
    int width;
};

/// @p a mixed @p t of the way towards @p b. Used to derive the striping and the
/// selection wash from whatever the palette actually is, so they land in the
/// right place in either theme without a second set of constants.
QColor blend(const QColor& a, const QColor& b, float t)
{
    return QColor::fromRgbF(a.redF() + (b.redF() - a.redF()) * t,
                            a.greenF() + (b.greenF() - a.greenF()) * t,
                            a.blueF() + (b.blueF() - a.blueF()) * t);
}

constexpr ColumnWidth kColumnWidths[] = {
    { HardpointModel::IndexColumn, 44 },  { HardpointModel::NameColumn, 190 },
    { HardpointModel::XColumn, 84 },      { HardpointModel::YColumn, 84 },
    { HardpointModel::ZColumn, 84 },      { HardpointModel::TypeColumn, 140 },
    { HardpointModel::Part1Column, 150 }, { HardpointModel::Part2Column, 150 },
    { HardpointModel::BushingColumn, 74 },
};

} // namespace

/// A table view whose leftmost columns do not scroll away.
///
/// It is a second view of the same model and the same selection, showing only
/// those columns and laid over the left edge of this one. Sharing the model is
/// what makes it correct rather than a copy that has to be kept in step: a sort,
/// a filter, an edit or a selection happens once and both views are showing it.
class FrozenColumnView : public QTableView {
public:
    explicit FrozenColumnView(int frozenColumns, QWidget* parent = nullptr);

    void setModel(QAbstractItemModel* model) override;
    void scrollTo(const QModelIndex& index, ScrollHint hint = EnsureVisible) override;

    QTableView* frozen() const { return m_frozen; }
    /// Take the frozen copy's widths and height from this view again. Called
    /// after anything that changes them wholesale.
    void refreshFrozenLayout();

protected:
    void resizeEvent(QResizeEvent* event) override;
    QModelIndex moveCursor(CursorAction action, Qt::KeyboardModifiers modifiers) override;

private:
    int frozenWidth() const;
    void updateFrozenGeometry();

    QTableView* m_frozen = nullptr;
    int m_frozenColumns = 1;
};

FrozenColumnView::FrozenColumnView(int frozenColumns, QWidget* parent)
    : QTableView(parent), m_frozen(new QTableView(this)), m_frozenColumns(frozenColumns)
{
    // Named so the stylesheet can give it the one border that says the columns
    // to its right are sliding underneath.
    m_frozen->setObjectName(QStringLiteral("frozenColumns"));
    m_frozen->setFocusPolicy(Qt::NoFocus);
    m_frozen->setFrameShape(QFrame::NoFrame);
    m_frozen->setShowGrid(false);
    m_frozen->setAlternatingRowColors(true);
    m_frozen->setWordWrap(false);
    m_frozen->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_frozen->setSelectionMode(QAbstractItemView::SingleSelection);
    // Both frozen columns are read-only, so there is nothing to edit in here.
    m_frozen->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_frozen->verticalHeader()->hide();
    m_frozen->horizontalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    m_frozen->horizontalHeader()->setHighlightSections(false);
    m_frozen->horizontalHeader()->setSectionsClickable(true);
    m_frozen->horizontalHeader()->setSortIndicatorShown(true);
    m_frozen->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_frozen->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_frozen->setVerticalScrollMode(ScrollPerPixel);
    m_frozen->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    m_frozen->verticalHeader()->setDefaultSectionSize(kRowHeight);

    setHorizontalScrollMode(ScrollPerPixel);
    setVerticalScrollMode(ScrollPerPixel);
    viewport()->stackUnder(m_frozen);

    connect(horizontalHeader(), &QHeaderView::sectionResized, this,
            [this](int column, int, int size) {
                if (column >= m_frozenColumns) return;
                m_frozen->setColumnWidth(column, size);
                updateFrozenGeometry();
            });
    connect(verticalHeader(), &QHeaderView::sectionResized, this,
            [this](int row, int, int size) { m_frozen->setRowHeight(row, size); });

    // One vertical position between the two, whichever of them is scrolled.
    connect(verticalScrollBar(), &QScrollBar::valueChanged, m_frozen->verticalScrollBar(),
            &QScrollBar::setValue);
    connect(m_frozen->verticalScrollBar(), &QScrollBar::valueChanged, verticalScrollBar(),
            &QScrollBar::setValue);

    // Sorting belongs to the shared model, so a click on either header sorts
    // both; only the indicator has to be told where it went.
    connect(horizontalHeader(), &QHeaderView::sortIndicatorChanged, this,
            [this](int column, Qt::SortOrder order) {
                const QSignalBlocker blocker(m_frozen->horizontalHeader());
                m_frozen->horizontalHeader()->setSortIndicator(column, order);
            });
    connect(m_frozen->horizontalHeader(), &QHeaderView::sectionClicked, this, [this](int column) {
        const QHeaderView* header = horizontalHeader();
        const bool ascending = header->sortIndicatorSection() != column
                               || header->sortIndicatorOrder() != Qt::AscendingOrder;
        sortByColumn(column, ascending ? Qt::AscendingOrder : Qt::DescendingOrder);
    });
}

void FrozenColumnView::setModel(QAbstractItemModel* model)
{
    QTableView::setModel(model);
    m_frozen->setModel(model);
    if (!model) return;

    // One selection for both, so a row picked in either is the row shown in the
    // other without anything having to be forwarded.
    m_frozen->setSelectionModel(selectionModel());
    for (int column = 0; column < model->columnCount(); ++column)
        m_frozen->setColumnHidden(column, column >= m_frozenColumns);

    refreshFrozenLayout();
    m_frozen->show();
}

void FrozenColumnView::refreshFrozenLayout()
{
    for (int column = 0; column < m_frozenColumns; ++column)
        m_frozen->setColumnWidth(column, columnWidth(column));
    m_frozen->verticalHeader()->setDefaultSectionSize(verticalHeader()->defaultSectionSize());
    updateFrozenGeometry();
}

int FrozenColumnView::frozenWidth() const
{
    int width = 0;
    for (int column = 0; column < m_frozenColumns; ++column) width += columnWidth(column);
    return width;
}

void FrozenColumnView::updateFrozenGeometry()
{
    const int left = (verticalHeader()->isVisible() ? verticalHeader()->width() : 0) + frameWidth();
    m_frozen->setGeometry(left, frameWidth(), frozenWidth(),
                          viewport()->height() + horizontalHeader()->height());
}

void FrozenColumnView::resizeEvent(QResizeEvent* event)
{
    QTableView::resizeEvent(event);
    updateFrozenGeometry();
}

QModelIndex FrozenColumnView::moveCursor(CursorAction action, Qt::KeyboardModifiers modifiers)
{
    const QModelIndex current = QTableView::moveCursor(action, modifiers);
    // Arrowing left out of the first scrolling column would otherwise leave the
    // cursor sitting underneath the frozen columns, out of sight.
    if (action == MoveLeft && current.column() >= m_frozenColumns
        && visualRect(current).left() < frozenWidth()) {
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() + visualRect(current).left()
                                        - frozenWidth());
    }
    return current;
}

void FrozenColumnView::scrollTo(const QModelIndex& index, ScrollHint hint)
{
    // A frozen column is always in view horizontally, but its row still has to
    // be brought into sight, so the scroll is asked for against the first
    // column that does move.
    if (index.isValid() && index.column() < m_frozenColumns && model()) {
        QTableView::scrollTo(model()->index(index.row(), m_frozenColumns), hint);
        return;
    }
    QTableView::scrollTo(index, hint);
}

// ---------------------------------------------------------------------------
// The panel
// ---------------------------------------------------------------------------

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
    // text, or -2068.6 sorts before -544.3. Every other column answers the same
    // role with something that sorts the way it reads.
    m_proxy->setSortRole(Qt::EditRole);

    buildTable();

    m_status = new QLabel(this);
    m_status->setWordWrap(true);
    m_status->setTextInteractionFlags(Qt::TextSelectableByMouse);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(6);
    layout->addWidget(m_filter);
    layout->addWidget(m_view, 1);
    layout->addWidget(m_status);

    connect(m_filter, &QLineEdit::textChanged, m_proxy,
            &QSortFilterProxyModel::setFilterFixedString);

    connect(m_view->selectionModel(), &QItemSelectionModel::currentRowChanged, this,
            [this](const QModelIndex& current) {
                // Moving on is the acknowledgement of a refused edit.
                m_rejection.clear();
                updateStatusLine();
                if (m_syncing) return;
                emit rowSelected(current.isValid() ? m_proxy->mapToSource(current).row() : -1);
            });

    // The store is what says whether anything is wrong; the panel only reports
    // it. Every path that can change that ends up here.
    connect(m_model, &HardpointModel::editRejected, this, &HardpointPanel::showRejection);
    connect(m_model, &HardpointModel::configChanged, this, [this](int) {
        m_rejection.clear();
        updateStatusLine();
    });
    connect(m_model, &QAbstractItemModel::modelReset, this, [this] {
        m_rejection.clear();
        updateStatusLine();
    });
    connect(m_model, &QAbstractItemModel::dataChanged, this,
            [this](const QModelIndex&, const QModelIndex&) { updateStatusLine(); });

    applyTheme();
    updateStatusLine();
}

void HardpointPanel::buildTable()
{
    // The number and the name stay put; everything to their right scrolls.
    m_view = new FrozenColumnView(HardpointModel::XColumn, this);
    m_view->setModel(m_proxy);

    m_view->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_view->setSelectionMode(QAbstractItemView::SingleSelection);
    m_view->setSortingEnabled(true);
    m_view->setAlternatingRowColors(true);
    m_view->setShowGrid(false);
    m_view->setWordWrap(false);
    m_view->setTextElideMode(Qt::ElideRight);
    m_view->setFrameShape(QFrame::NoFrame);
    m_view->setCornerButtonEnabled(false);
    m_view->verticalHeader()->setVisible(false);
    m_view->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    m_view->verticalHeader()->setDefaultSectionSize(kRowHeight);
    m_view->setMinimumWidth(300);

    // A cell draws itself through a delegate rather than holding a widget, so a
    // table of five hundred points costs the same as one of twenty: only the
    // rows on screen are painted, and only the cell being edited has an editor.
    auto* coordinates = new CoordinateDelegate(this);
    for (int column = HardpointModel::XColumn; column <= HardpointModel::ZColumn; ++column)
        m_view->setItemDelegateForColumn(column, coordinates);

    auto* names = new PointNameDelegate(this);
    m_view->setItemDelegateForColumn(HardpointModel::NameColumn, names);
    // The frozen copy shows the same column, so it draws it the same way.
    m_view->frozen()->setItemDelegateForColumn(HardpointModel::NameColumn, names);

    m_view->setItemDelegateForColumn(HardpointModel::TypeColumn, new PointTypeDelegate(this));
    auto* bodies = new BodyDelegate(this);
    m_view->setItemDelegateForColumn(HardpointModel::Part1Column, bodies);
    m_view->setItemDelegateForColumn(HardpointModel::Part2Column, bodies);
    m_view->setItemDelegateForColumn(HardpointModel::BushingColumn, new BushingDelegate(this));

    QHeaderView* header = m_view->horizontalHeader();
    header->setSectionResizeMode(QHeaderView::Interactive);
    header->setHighlightSections(false);
    header->setStretchLastSection(false);
    for (const ColumnWidth& entry : kColumnWidths) m_view->setColumnWidth(entry.column, entry.width);
    m_view->refreshFrozenLayout();

    // Start in file order; clicking a header still sorts.
    m_view->sortByColumn(-1, Qt::AscendingOrder);
    // One click into a cell starts editing, which is what a table like this is
    // for -- and for the dropdown columns it is the difference between one
    // click and three.
    m_view->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::SelectedClicked
                            | QAbstractItemView::EditKeyPressed | QAbstractItemView::AnyKeyPressed);
}

QSize HardpointPanel::sizeHint() const
{
    // Wide enough that the coordinates and the first body column are there
    // without scrolling, which is the layout most of the work happens in.
    return QSize(620, 460);
}

void HardpointPanel::applyTheme()
{
    m_theming = true;

    const QPalette pal = palette();
    const QColor base = pal.color(QPalette::Base);
    const bool dark = base.lightness() < 128;

    // Striping, rules and the selection are all derived from the window's own
    // colours rather than picked, so the table sits in whatever theme it is
    // dropped into.
    const QColor text = pal.color(QPalette::Text);
    const QColor stripe = blend(base, text, dark ? 0.055f : 0.035f);
    const QColor line = blend(base, text, dark ? 0.22f : 0.16f);
    const QColor muted = pal.color(QPalette::Disabled, QPalette::Text);

    // A wash rather than a solid bar. The chips have to stay legible on the
    // selected row, and a full-strength highlight would swallow them.
    const QColor wash = blend(base, pal.color(QPalette::Highlight), dark ? 0.34f : 0.20f);

    // Colour goes through the palette, which is what the cells and the
    // delegates are actually painted from; the stylesheet is left to say only
    // what a palette cannot -- borders, padding, and the one rule that marks
    // where the frozen columns end.
    QPalette viewPalette = pal;
    viewPalette.setColor(QPalette::Base, base);
    viewPalette.setColor(QPalette::AlternateBase, stripe);
    viewPalette.setColor(QPalette::Highlight, wash);
    viewPalette.setColor(QPalette::HighlightedText, text);
    m_view->setPalette(viewPalette);
    m_view->frozen()->setPalette(viewPalette);

    setStyleSheet(QStringLiteral(
                      "QTableView { border: none; outline: none; }"
                      "QTableView::item { padding: 0px 6px; border: none; }"
                      "QTableView#frozenColumns { border-right: 1px solid %1; }"
                      "QHeaderView { background-color: %2; border: none; }"
                      "QHeaderView::section { background-color: %2; color: %3; padding: 6px 8px;"
                      " border: none; border-bottom: 1px solid %1; }"
                      "QHeaderView::section:hover { color: %4; }"
                      "QLineEdit { padding: 5px 8px; border: 1px solid %1; border-radius: 4px; }")
                      .arg(line.name(), base.name(), muted.name(), text.name()));

    m_theming = false;
}

void HardpointPanel::changeEvent(QEvent* event)
{
    QWidget::changeEvent(event);
    if (m_theming) return;
    if (event->type() == QEvent::PaletteChange || event->type() == QEvent::ApplicationPaletteChange
        || event->type() == QEvent::ThemeChange) {
        applyTheme();
        updateStatusLine();
    }
}

int HardpointPanel::currentModelRow() const
{
    const QModelIndex current = m_view->selectionModel()->currentIndex();
    return current.isValid() ? m_proxy->mapToSource(current).row() : -1;
}

void HardpointPanel::showRejection(int row, const QString& reason)
{
    m_rejection = reason;
    // The row the edit was refused on may not be the selected one -- an edit
    // commits when focus leaves it -- so say which point it was about.
    const int rows = m_model->rowCount();
    if (row >= 0 && row < rows) {
        m_rejection = QStringLiteral("%1: %2").arg(
            m_model->index(row, HardpointModel::NameColumn).data().toString(), reason);
    }
    updateStatusLine();
}

void HardpointPanel::updateStatusLine()
{
    QString text = m_rejection;
    ConfigIssueLevel level = ConfigIssueLevel::Error;

    if (text.isEmpty()) {
        const std::vector<ConfigIssue> issues = m_model->issuesAt(currentModelRow());
        if (!issues.empty()) {
            text = issueMessages(issues).join(QLatin1Char(' '));
            level = hasError(issues) ? ConfigIssueLevel::Error : ConfigIssueLevel::Warning;
        }
    }

    if (!text.isEmpty()) {
        m_status->setStyleSheet(
            QStringLiteral("color: %1;").arg(issueColor(level, palette()).name()));
        m_status->setText(text);
        return;
    }

    const int rows = m_model->rowCount();
    QString summary = tr("No hardpoints imported yet.");
    if (rows > 0) {
        summary = tr("%1 point(s)  -  %2 typed").arg(rows).arg(m_model->configuredCount());
        const int flagged = m_model->issueCount();
        if (flagged > 0) summary += tr("  -  %1 needing attention").arg(flagged);
    }
    m_status->setStyleSheet(
        QStringLiteral("color: %1;").arg(palette().color(QPalette::Disabled, QPalette::Text).name()));
    m_status->setText(summary);
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
    updateStatusLine();
}

} // namespace suspkin
