#include "app/PartDialogs.h"

#include "io/LinkageTemplate.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

namespace suspkin {
namespace {

constexpr PartKind kKinds[] = { PartKind::Other,   PartKind::Link,     PartKind::Wishbone,
                                PartKind::Upright, PartKind::Rocker,   PartKind::Damper,
                                PartKind::AntiRoll, PartKind::Wheel };

enum PartColumn { LabelColumn, KindColumn, DrawnColumn, PointsColumn, PartColumnCount };

} // namespace

QString partKindLabel(PartKind kind)
{
    switch (kind) {
    case PartKind::Wishbone: return QObject::tr("Wishbone");
    case PartKind::Link: return QObject::tr("Link");
    case PartKind::Upright: return QObject::tr("Upright");
    case PartKind::Rocker: return QObject::tr("Rocker");
    case PartKind::Damper: return QObject::tr("Damper");
    case PartKind::AntiRoll: return QObject::tr("Anti-roll bar");
    case PartKind::Wheel: return QObject::tr("Wheel");
    case PartKind::Other: return QObject::tr("Other");
    }
    return QObject::tr("Other");
}

// ---------------------------------------------------------------------------
// New part from the selection
// ---------------------------------------------------------------------------

NewPartDialog::NewPartDialog(const LinkageTemplate& templ, const QStringList& points,
                             QWidget* parent)
    : QDialog(parent), m_templ(templ)
{
    setWindowTitle(tr("New Part from Selection"));

    auto* layout = new QVBoxLayout(this);
    auto* intro = new QLabel(tr("The points are joined in this order. The part is drawn once, "
                                "through exactly these points -- it is not repeated for the "
                                "other corners or mirrored to the other side."),
                             this);
    intro->setWordWrap(true);
    layout->addWidget(intro);

    auto* form = new QFormLayout();
    m_label = new QLineEdit(tr("New part"), this);
    m_label->selectAll();
    form->addRow(tr("Label"), m_label);

    m_kind = new QComboBox(this);
    for (const PartKind kind : kKinds) m_kind->addItem(partKindLabel(kind), partKindToString(kind));
    m_kind->setToolTip(tr("What the part is drawn as. A link and a wishbone are drawn in "
                          "different colours; nothing else about the solve changes."));
    form->addRow(tr("Kind"), m_kind);
    layout->addLayout(form);

    auto* order = new QHBoxLayout();
    m_points = new QListWidget(this);
    m_points->addItems(points);
    m_points->setDragDropMode(QAbstractItemView::InternalMove);
    m_points->setCurrentRow(0);
    order->addWidget(m_points, 1);
    auto* buttons = new QVBoxLayout();
    auto* up = new QPushButton(tr("Up"), this);
    auto* down = new QPushButton(tr("Down"), this);
    buttons->addWidget(up);
    buttons->addWidget(down);
    buttons->addStretch(1);
    order->addLayout(buttons);
    layout->addLayout(order, 1);

    m_closed = new QCheckBox(tr("Close the chain, last point back to the first"), this);
    layout->addWidget(m_closed);

    m_id = new QLabel(this);
    m_id->setWordWrap(true);
    layout->addWidget(m_id);

    m_buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    m_buttons->button(QDialogButtonBox::Ok)->setText(tr("Add Part"));
    connect(m_buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(m_buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(m_buttons);

    connect(up, &QPushButton::clicked, this, [this] { move(-1); });
    connect(down, &QPushButton::clicked, this, [this] { move(+1); });
    connect(m_label, &QLineEdit::textChanged, this, &NewPartDialog::refresh);
    refresh();
    resize(420, 420);
}

void NewPartDialog::move(int step)
{
    const int row = m_points->currentRow();
    const int target = row + step;
    if (row < 0 || target < 0 || target >= m_points->count()) return;
    QListWidgetItem* item = m_points->takeItem(row);
    m_points->insertItem(target, item);
    m_points->setCurrentRow(target);
}

void NewPartDialog::refresh()
{
    // Three points make a closed chain a shape; two closed back on themselves
    // are the same segment twice.
    m_closed->setEnabled(m_points->count() >= 3);
    if (m_points->count() < 3) m_closed->setChecked(false);

    const bool named = !m_label->text().trimmed().isEmpty();
    m_buttons->button(QDialogButtonBox::Ok)->setEnabled(named && m_points->count() >= 2);
    m_id->setText(tr("Written into the project's linkage template as \"%1\".")
                      .arg(uniquePartId(m_templ, m_label->text())));
}

PartTemplate NewPartDialog::part() const
{
    PartTemplate part;
    part.label = m_label->text().trimmed();
    part.id = uniquePartId(m_templ, part.label);
    part.kind = partKindFromString(m_kind->currentData().toString());
    part.perCorner = false;
    ChainTemplate chain;
    for (int i = 0; i < m_points->count(); ++i) chain.points << m_points->item(i)->text();
    chain.closed = m_closed->isEnabled() && m_closed->isChecked();
    part.chains.push_back(chain);
    return part;
}

// ---------------------------------------------------------------------------
// Editing the template's parts
// ---------------------------------------------------------------------------

EditPartsDialog::EditPartsDialog(const LinkageTemplate& templ, QWidget* parent)
    : QDialog(parent), m_templ(templ)
{
    setWindowTitle(tr("Edit Parts"));

    auto* layout = new QVBoxLayout(this);
    auto* intro = new QLabel(tr("The parts the project's linkage template draws. Double-click a "
                                "label to rename it. Nothing is written until OK, and then only "
                                "what changed."),
                             this);
    intro->setWordWrap(true);
    layout->addWidget(intro);

    m_table = new QTableWidget(static_cast<int>(templ.parts.size()), PartColumnCount, this);
    m_table->setHorizontalHeaderLabels(
        { tr("Label"), tr("Kind"), tr("Drawn"), tr("Points") });
    m_table->verticalHeader()->hide();
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
    m_table->horizontalHeader()->setStretchLastSection(true);

    for (int row = 0; row < static_cast<int>(templ.parts.size()); ++row) {
        const PartTemplate& part = templ.parts[static_cast<std::size_t>(row)];

        auto* label = new QTableWidgetItem(part.label);
        // The id rides along with the row: it is the key the patch finds the
        // part by, whatever its label has been changed to.
        label->setData(Qt::UserRole, part.id);
        label->setToolTip(tr("Id: %1").arg(part.id));
        m_table->setItem(row, LabelColumn, label);

        QStringList chains;
        for (const ChainTemplate& chain : part.chains) {
            chains << chain.points.join(QStringLiteral(" - "))
                          + (chain.closed ? tr(" (closed)") : QString());
        }
        const QPair<int, QString> readOnly[] = {
            { KindColumn, partKindLabel(part.kind) },
            { DrawnColumn, part.perCorner ? tr("Every corner, both sides") : tr("Once") },
            { PointsColumn, chains.join(QStringLiteral("; ")) },
        };
        for (const auto& [column, text] : readOnly) {
            auto* item = new QTableWidgetItem(text);
            item->setFlags(item->flags() & ~Qt::ItemIsEditable);
            item->setToolTip(text);
            m_table->setItem(row, column, item);
        }
    }
    m_table->resizeColumnsToContents();
    layout->addWidget(m_table, 1);

    auto* actions = new QHBoxLayout();
    m_rename = new QPushButton(tr("Rename"), this);
    m_delete = new QPushButton(tr("Delete"), this);
    actions->addWidget(m_rename);
    actions->addWidget(m_delete);
    actions->addStretch(1);
    layout->addLayout(actions);

    m_buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(m_buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(m_buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(m_buttons);

    connect(m_rename, &QPushButton::clicked, this, [this] {
        const int row = m_table->currentRow();
        if (row >= 0) m_table->editItem(m_table->item(row, LabelColumn));
    });
    connect(m_delete, &QPushButton::clicked, this, [this] {
        const int row = m_table->currentRow();
        if (row < 0 || m_table->rowCount() <= 1) return;
        m_removed << m_table->item(row, LabelColumn)->data(Qt::UserRole).toString();
        m_table->removeRow(row);
        refreshButtons();
    });
    connect(m_table, &QTableWidget::currentCellChanged, this, [this] { refreshButtons(); });
    if (m_table->rowCount() > 0) m_table->selectRow(0);
    refreshButtons();
    resize(720, 440);
}

void EditPartsDialog::refreshButtons()
{
    const bool picked = m_table->currentRow() >= 0;
    m_rename->setEnabled(picked);
    // A template with no parts is one the reader refuses, so the last one stays.
    m_delete->setEnabled(picked && m_table->rowCount() > 1);
    m_delete->setToolTip(m_table->rowCount() > 1
                             ? QString()
                             : tr("A template needs at least one part."));
}

QHash<QString, QString> EditPartsDialog::relabelled() const
{
    QHash<QString, QString> changed;
    for (int row = 0; row < m_table->rowCount(); ++row) {
        const QTableWidgetItem* item = m_table->item(row, LabelColumn);
        const QString id = item->data(Qt::UserRole).toString();
        const QString label = item->text().trimmed();
        for (const PartTemplate& part : m_templ.parts) {
            if (part.id != id) continue;
            // An emptied label is not a rename: the part would read as its id.
            if (!label.isEmpty() && label != part.label) changed.insert(id, label);
            break;
        }
    }
    return changed;
}

QStringList EditPartsDialog::removed() const { return m_removed; }

} // namespace suspkin
