#include "app/PointDialog.h"

#include "app/ExpressionSpinBox.h"
#include "app/HardpointDelegates.h"

#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace suspkin {
namespace {

/// The same limits the table's coordinate editor has, so a point can be put
/// anywhere a point can be moved to.
constexpr double kCoordinateLimit = 100000.0;
constexpr int kDecimals = 4;

} // namespace

QString freePointName(const HardpointTable& table, const QString& base)
{
    const QString stem = base.trimmed().isEmpty() ? QStringLiteral("P") : base.trimmed();
    if (hardpointNameProblem(stem, table).isEmpty()) return stem;
    for (int n = 2;; ++n) {
        const QString candidate = QStringLiteral("%1_%2").arg(stem).arg(n);
        if (hardpointNameProblem(candidate, table).isEmpty()) return candidate;
    }
}

PointDialog::PointDialog(const HardpointTable& table, const Hardpoint& seed, const QString& note,
                         QWidget* parent)
    : QDialog(parent), m_table(table)
{
    setWindowTitle(tr("Add Point"));

    auto* layout = new QVBoxLayout(this);
    if (!note.isEmpty()) {
        auto* intro = new QLabel(note, this);
        intro->setWordWrap(true);
        layout->addWidget(intro);
    }

    auto* form = new QFormLayout();
    m_name = new QLineEdit(freePointName(table, seed.name), this);
    m_name->selectAll();
    form->addRow(tr("Name"), m_name);

    const char* axes[3] = { "X", "Y", "Z" };
    for (int axis = 0; axis < 3; ++axis) {
        // The same field the table edits a coordinate in, arithmetic included.
        auto* spin = new ExpressionSpinBox(this);
        spin->setRange(-kCoordinateLimit, kCoordinateLimit);
        spin->setDecimals(kDecimals);
        spin->setSuffix(tr(" mm"));
        spin->setValue(seed.coord[axis]);
        m_coord[axis] = spin;
        form->addRow(QLatin1String(axes[axis]), spin);
    }
    layout->addLayout(form);

    m_problem = new QLabel(this);
    m_problem->setWordWrap(true);
    // The colour the table marks a refused edit in, so the two read as one rule.
    m_problem->setStyleSheet(
        QStringLiteral("color: %1;").arg(issueColor(ConfigIssueLevel::Error, palette()).name()));
    layout->addWidget(m_problem);

    m_buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    m_buttons->button(QDialogButtonBox::Ok)->setText(tr("Add"));
    connect(m_buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(m_buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(m_buttons);

    connect(m_name, &QLineEdit::textChanged, this, &PointDialog::refresh);
    refresh();
}

Hardpoint PointDialog::point() const
{
    Hardpoint point;
    point.name = m_name->text().trimmed();
    for (int axis = 0; axis < 3; ++axis) {
        // A sum typed into a field and then OK'd straight away has not been
        // read yet: the field only interprets what is in it when it is left.
        m_coord[axis]->interpretText();
        point.coord[axis] = m_coord[axis]->value();
    }
    return point;
}

void PointDialog::refresh()
{
    // Trimmed first, the way the table takes a name: the spaces round a typed
    // name are not what the user meant, and are not worth refusing it over.
    const QString problem = hardpointNameProblem(m_name->text().trimmed(), m_table);
    m_problem->setText(problem);
    m_problem->setVisible(!problem.isEmpty());
    m_buttons->button(QDialogButtonBox::Ok)->setEnabled(problem.isEmpty());
}

} // namespace suspkin
