#include "app/MirrorDialog.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QTextBrowser>
#include <QVBoxLayout>

namespace suspkin {
namespace {

/// How many name pairs the preview lists before it stops and says how many more
/// there are. Long enough to see a pattern, short enough not to scroll.
constexpr int kPreviewLimit = 12;

QString escaped(const QString& text) { return text.toHtmlEscaped(); }

} // namespace

MirrorDialog::MirrorDialog(const HardpointTable& table, int selection, const MirrorSpec& spec,
                           QWidget* parent)
    : QDialog(parent), m_table(table), m_selection(selection)
{
    setWindowTitle(tr("Mirror hardpoints"));

    m_axis = new QComboBox(this);
    // Y first and by default: the vehicle's longitudinal plane is y = 0, so this
    // is what "the other side" means in the frame this tool works in.
    m_axis->addItem(tr("Y - left / right (the other side of the car)"),
                    int(MirrorAxis::Y));
    m_axis->addItem(tr("X - front / rear"), int(MirrorAxis::X));
    m_axis->addItem(tr("Z - up / down"), int(MirrorAxis::Z));
    m_axis->setCurrentIndex(m_axis->findData(int(spec.axis)));

    m_allRows = new QRadioButton(tr("All %1 hardpoints").arg(table.size()), this);
    m_selectedRow = new QRadioButton(this);
    const bool hasSelection = selection >= 0 && selection < static_cast<int>(table.size());
    m_selectedRow->setText(hasSelection
                               ? tr("Only \"%1\"")
                                     .arg(table.points[static_cast<std::size_t>(selection)].name)
                               : tr("Only the selected hardpoint"));
    m_selectedRow->setEnabled(hasSelection);
    (hasSelection ? m_selectedRow : m_allRows)->setChecked(true);
    // The selection is the more careful default when there is one: mirroring a
    // whole table by accident is a lot of rows to undo by hand.
    auto* scopeGroup = new QButtonGroup(this);
    scopeGroup->addButton(m_allRows);
    scopeGroup->addButton(m_selectedRow);

    m_suffix = new QRadioButton(tr("Add a suffix"), this);
    m_prefix = new QRadioButton(tr("Add a prefix"), this);
    m_replace = new QRadioButton(tr("Replace text in the name"), this);
    auto* namingGroup = new QButtonGroup(this);
    namingGroup->addButton(m_suffix);
    namingGroup->addButton(m_prefix);
    namingGroup->addButton(m_replace);
    switch (spec.naming) {
    case MirrorNaming::Suffix: m_suffix->setChecked(true); break;
    case MirrorNaming::Prefix: m_prefix->setChecked(true); break;
    case MirrorNaming::Replace: m_replace->setChecked(true); break;
    }

    m_affix = new QLineEdit(spec.affix, this);
    m_affix->setPlaceholderText(tr("_R"));
    m_find = new QLineEdit(spec.findText, this);
    m_find->setPlaceholderText(tr("L_"));
    m_replaceWith = new QLineEdit(spec.replaceText, this);
    m_replaceWith->setPlaceholderText(tr("R_"));
    m_caseSensitive = new QCheckBox(tr("Match case"), this);
    m_caseSensitive->setChecked(spec.caseSensitive);

    m_updateExisting = new QCheckBox(tr("Update points that already exist under the new name"), this);
    m_updateExisting->setChecked(spec.updateExisting);
    m_skipMirrored = new QCheckBox(tr("Skip points that are themselves mirrors"), this);
    m_skipMirrored->setChecked(spec.skipMirrored);

    m_preview = new QTextBrowser(this);
    m_preview->setMinimumHeight(180);

    m_summary = new QLabel(this);
    m_summary->setWordWrap(true);

    auto* scopeBox = new QGroupBox(tr("What to mirror"), this);
    auto* scopeLayout = new QVBoxLayout(scopeBox);
    scopeLayout->addWidget(m_allRows);
    scopeLayout->addWidget(m_selectedRow);
    auto* axisForm = new QFormLayout;
    axisForm->addRow(tr("Mirror about:"), m_axis);
    scopeLayout->addLayout(axisForm);

    auto* namingBox = new QGroupBox(tr("How the mirrored points are named"), this);
    auto* namingLayout = new QFormLayout(namingBox);
    namingLayout->addRow(m_suffix, m_affix);
    namingLayout->addRow(m_prefix);
    namingLayout->addRow(m_replace);
    auto* replaceRow = new QWidget(namingBox);
    auto* replaceLayout = new QHBoxLayout(replaceRow);
    replaceLayout->setContentsMargins(0, 0, 0, 0);
    replaceLayout->addWidget(new QLabel(tr("Find:"), replaceRow));
    replaceLayout->addWidget(m_find, 1);
    replaceLayout->addWidget(new QLabel(tr("With:"), replaceRow));
    replaceLayout->addWidget(m_replaceWith, 1);
    replaceLayout->addWidget(m_caseSensitive);
    namingLayout->addRow(replaceRow);
    namingLayout->addRow(m_updateExisting);
    namingLayout->addRow(m_skipMirrored);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Mirror"));

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(scopeBox);
    layout->addWidget(namingBox);
    layout->addWidget(new QLabel(tr("Preview"), this));
    layout->addWidget(m_preview, 1);
    layout->addWidget(m_summary);
    layout->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    const auto refresh = [this] { refreshPreview(); };
    connect(m_axis, &QComboBox::currentIndexChanged, this, refresh);
    connect(m_affix, &QLineEdit::textChanged, this, refresh);
    connect(m_find, &QLineEdit::textChanged, this, refresh);
    connect(m_replaceWith, &QLineEdit::textChanged, this, refresh);
    for (QCheckBox* box : { m_caseSensitive, m_updateExisting, m_skipMirrored })
        connect(box, &QCheckBox::toggled, this, refresh);
    for (QRadioButton* button :
         { m_allRows, m_selectedRow, m_suffix, m_prefix, m_replace })
        connect(button, &QRadioButton::toggled, this, refresh);

    refreshPreview();
    resize(680, 640);
}

MirrorSpec MirrorDialog::spec() const
{
    MirrorSpec spec;
    spec.axis = static_cast<MirrorAxis>(m_axis->currentData().toInt());
    spec.naming = m_prefix->isChecked()    ? MirrorNaming::Prefix
                  : m_replace->isChecked() ? MirrorNaming::Replace
                                           : MirrorNaming::Suffix;
    spec.affix = m_affix->text();
    spec.findText = m_find->text();
    spec.replaceText = m_replaceWith->text();
    spec.caseSensitive = m_caseSensitive->isChecked();
    spec.updateExisting = m_updateExisting->isChecked();
    spec.skipMirrored = m_skipMirrored->isChecked();
    return spec;
}

std::vector<int> MirrorDialog::rows() const
{
    if (m_selectedRow->isChecked() && m_selection >= 0) return { m_selection };
    return {};
}

void MirrorDialog::refreshPreview()
{
    const bool replacing = m_replace->isChecked();
    m_affix->setEnabled(!replacing);
    m_find->setEnabled(replacing);
    m_replaceWith->setEnabled(replacing);
    m_caseSensitive->setEnabled(replacing);

    const MirrorSpec current = spec();
    const MirrorOutcome outcome = mirrorHardpoints(m_table, rows(), current);

    // Re-derive the pairs for display rather than diffing the outcome: what the
    // user wants to see is source -> target, which the outcome no longer says.
    const std::vector<int> selected = rows();
    std::vector<int> sources;
    if (selected.empty()) {
        for (int i = 0; i < static_cast<int>(m_table.size()); ++i) sources.push_back(i);
    } else {
        sources = selected;
    }

    QString html = QStringLiteral("<table cellspacing='0' cellpadding='2'>");
    int shown = 0;
    int applicable = 0;
    for (const int row : sources) {
        if (row < 0 || row >= static_cast<int>(m_table.size())) continue;
        const Hardpoint& point = m_table.points[static_cast<std::size_t>(row)];
        if (current.skipMirrored && point.isMirrored()) continue;
        const QString name = mirroredName(point.name, current);
        if (name.isEmpty()) continue;
        ++applicable;
        if (shown >= kPreviewLimit) continue;
        ++shown;

        const bool exists = m_table.indexOf(name) >= 0;
        html += QStringLiteral("<tr><td>%1</td><td>&nbsp;&rarr;&nbsp;</td><td><b>%2</b></td>"
                               "<td>&nbsp;%3</td></tr>")
                    .arg(escaped(point.name), escaped(name),
                         exists ? (current.updateExisting ? tr("(replaces the existing point)")
                                                          : tr("(exists already - kept)"))
                                : QString());
    }
    html += QStringLiteral("</table>");
    if (applicable > shown)
        html += tr("<p>and %1 more.</p>").arg(applicable - shown);
    if (applicable == 0)
        html = tr("<p>This rule does not produce a new name for any of these points.</p>");

    m_preview->setHtml(html);

    QStringList parts;
    if (outcome.added > 0) parts << tr("%1 added").arg(outcome.added);
    if (outcome.updated > 0) parts << tr("%1 replaced").arg(outcome.updated);
    if (outcome.skipped > 0) parts << tr("%1 skipped").arg(outcome.skipped);
    m_summary->setText(parts.isEmpty() ? tr("Nothing would change.")
                                       : parts.join(QStringLiteral(", ")));
}

} // namespace suspkin
