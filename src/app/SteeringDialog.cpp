#include "app/SteeringDialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QVBoxLayout>

namespace suspkin {
namespace {

const QString kCornerToken = QStringLiteral("{corner}");

/// A role name with its corner filled in, as the table would hold it.
QString named(const QString& pattern, const QString& token)
{
    QString name = pattern;
    name.replace(kCornerToken, token);
    return name;
}

QString escaped(const QString& text) { return text.toHtmlEscaped(); }

} // namespace

SteeringDialog::SteeringDialog(const LinkageTemplate& templ, const MirrorSpec& mirror,
                               const HardpointTable& table, QWidget* parent)
    : QDialog(parent), m_mirror(mirror), m_table(table), m_corners(templ.corners)
{
    setWindowTitle(tr("Steering"));

    auto* layout = new QVBoxLayout(this);

    auto* intro = new QLabel(
        tr("Which axle does the steering rack drive? An axle that drives none keeps its toe link "
           "where the workbook put it, and is not offered a steer sweep -- which is what stops a "
           "rear axle being steered by a rack the car has not got."),
        this);
    intro->setWordWrap(true);
    layout->addWidget(intro);

    const QString rackRole = templ.mechanism.tieRodInboard;

    auto* form = new QFormLayout();
    for (const CornerSpec& corner : m_corners) {
        auto* box = new QComboBox(this);
        // Empty data is the answer "no rack here", which is a real answer and
        // not the absence of one.
        box->addItem(tr("Not steered"), QString());
        if (!rackRole.isEmpty()) {
            // Shown as the hardpoint the user would find in their own table,
            // stored as the role, so the file keeps saying {corner} and the far
            // side keeps coming from the project's own mirror rule.
            box->addItem(named(rackRole, corner.token), rackRole);
        }
        const int index = box->findData(corner.steeringRack);
        box->setCurrentIndex(index >= 0 ? index : 0);
        box->setEnabled(!rackRole.isEmpty());
        connect(box, &QComboBox::currentIndexChanged, this, [this] { refreshPreview(); });

        form->addRow(corner.label.isEmpty() ? corner.token : corner.label, box);
        m_boxes.push_back(box);
    }
    layout->addLayout(form);

    if (rackRole.isEmpty()) {
        auto* none = new QLabel(tr("This project's template names no inboard tie rod end, so "
                                   "there is nothing for a rack to drive."),
                                this);
        none->setWordWrap(true);
        layout->addWidget(none);
    }

    m_preview = new QLabel(this);
    m_preview->setWordWrap(true);
    m_preview->setTextFormat(Qt::RichText);
    layout->addWidget(m_preview);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    refreshPreview();
}

std::vector<CornerSpec> SteeringDialog::corners() const
{
    std::vector<CornerSpec> out = m_corners;
    for (std::size_t i = 0; i < out.size() && i < m_boxes.size(); ++i) {
        out[i].steeringRack = m_boxes[i]->currentData().toString();
        // Answered, whichever way. A template where every axle came back "not
        // steered" has still been asked, and must not read next time as one that
        // never was -- that would put the steering back on every axle.
        out[i].steeringStated = true;
    }
    return out;
}

void SteeringDialog::refreshPreview()
{
    QStringList lines;
    const std::vector<CornerSpec> chosen = corners();
    for (const CornerSpec& corner : chosen) {
        const QString label = escaped(corner.label.isEmpty() ? corner.token : corner.label);
        if (corner.steeringRack.isEmpty()) {
            lines << tr("<b>%1</b>: not steered.").arg(label);
            continue;
        }

        // Both sides, the way the parts and the mechanism get theirs: the far
        // side comes from the project's own mirror rule, never from a convention
        // written into a template.
        const QString base = named(corner.steeringRack, corner.token);
        const QString far = mirroredName(base, m_mirror);
        QStringList missing;
        for (const QString& name : { base, far }) {
            if (name.isEmpty() || m_table.indexOf(name) < 0) missing << name;
        }

        QString line = tr("<b>%1</b>: the rack moves %2 and %3.")
                           .arg(label, escaped(base), escaped(far.isEmpty() ? tr("no far side")
                                                                            : far));
        if (!missing.isEmpty()) {
            // Not an error: a workbook may hold one axle, or may not have been
            // mirrored yet. Worth saying, because a rack driving a point that is
            // not in the table drives nothing.
            line += QStringLiteral(" ")
                    + tr("Not in the hardpoint table yet: %1.").arg(escaped(missing.join(
                          QStringLiteral(", "))));
        }
        lines << line;
    }

    m_preview->setText(lines.join(QStringLiteral("<br>")));
}

} // namespace suspkin
