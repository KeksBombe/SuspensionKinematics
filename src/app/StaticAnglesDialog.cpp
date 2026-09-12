#include "app/StaticAnglesDialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QLabel>
#include <QVBoxLayout>

namespace suspkin {
namespace {

QString millimetres(double value) { return QString::number(value, 'f', 1); }

} // namespace

StaticAnglesDialog::StaticAnglesDialog(const std::vector<StaticAnglesAxle>& axles, QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Static Camber and Toe"));
    // Wide enough that the notes wrap onto two lines rather than five; the
    // height follows from the width when the dialog is first shown.
    setMinimumWidth(480);

    auto* layout = new QVBoxLayout(this);

    auto* intro = new QLabel(
        tr("Static camber and toe for each axle, the way Lotus sets them with Set Static Angles. "
           "The wheel axis and the contact patch are computed from these, so neither has to be "
           "placed as a hardpoint. The far side takes the same numbers, mirrored.\n\n"
           "An axle that is not ticked reads its angles off its hardpoints, as shown."),
        this);
    intro->setWordWrap(true);
    layout->addWidget(intro);

    m_rows.reserve(axles.size());
    for (const StaticAnglesAxle& axle : axles) {
        Row row;
        row.axle = axle;

        const QString heading = axle.label.isEmpty() ? axle.token : axle.label;
        layout->addWidget(new QLabel(QStringLiteral("<b>%1</b>").arg(heading.toHtmlEscaped()), this));
        auto* grid = new QGridLayout;
        grid->setContentsMargins(12, 0, 0, 0);

        row.stated = new QCheckBox(tr("Set the angles here"), this);
        row.stated->setChecked(axle.stated.has_value());
        grid->addWidget(row.stated, 0, 0, 1, 4);

        row.camber = new QDoubleSpinBox(this);
        row.camber->setRange(-15.0, 15.0);
        row.camber->setDecimals(3);
        row.camber->setSingleStep(0.1);
        row.camber->setSuffix(tr(" deg"));
        row.camber->setToolTip(tr("Negative leans the top of the wheel in."));
        row.toe = new QDoubleSpinBox(this);
        row.toe->setRange(-10.0, 10.0);
        row.toe->setDecimals(3);
        row.toe->setSingleStep(0.05);
        row.toe->setSuffix(tr(" deg"));
        row.toe->setToolTip(tr("Positive is toe-in: the front of the wheel points at the "
                               "centreline."));
        const StaticAlignment shown = axle.stated.value_or(axle.fromHardpoints);
        row.camber->setValue(shown.camber);
        row.toe->setValue(shown.toe);
        grid->addWidget(new QLabel(tr("Camber"), this), 1, 0);
        grid->addWidget(row.camber, 1, 1);
        grid->addWidget(new QLabel(tr("Toe"), this), 1, 2);
        grid->addWidget(row.toe, 1, 3);
        grid->setColumnStretch(1, 1);
        grid->setColumnStretch(3, 1);

        layout->addLayout(grid);

        // In the dialog's own column rather than across the grid's: a wrapped
        // label spanning grid columns is given the height of one line and has
        // the rest cut off, where one in a plain column gets what it needs.
        row.note = new QLabel(this);
        row.note->setWordWrap(true);
        row.note->setTextInteractionFlags(Qt::TextSelectableByMouse);
        row.note->setContentsMargins(12, 0, 0, 6);
        layout->addWidget(row.note);
        m_rows.push_back(row);
    }

    // Wired once every row has its final address: the vector does not move
    // again after this.
    for (std::size_t i = 0; i < m_rows.size(); ++i) {
        Row* row = &m_rows[i];
        connect(row->stated, &QCheckBox::toggled, this, [this, row](bool on) {
            // Off goes back to what the hardpoints say, which is what the axle
            // will do; on starts from there, rather than from zero.
            if (!on) {
                row->camber->setValue(row->axle.fromHardpoints.camber);
                row->toe->setValue(row->axle.fromHardpoints.toe);
            }
            refresh(*row);
        });
        connect(row->camber, &QDoubleSpinBox::valueChanged, this, [this, row] { refresh(*row); });
        connect(row->toe, &QDoubleSpinBox::valueChanged, this, [this, row] { refresh(*row); });
        refresh(*row);
    }

    if (m_rows.empty()) {
        auto* none = new QLabel(tr("No axle in this table solves, so there is no wheel to set."),
                                this);
        none->setWordWrap(true);
        layout->addWidget(none);
    }

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

void StaticAnglesDialog::refresh(Row& row)
{
    const bool stated = row.stated->isChecked();
    row.camber->setEnabled(stated);
    row.toe->setEnabled(stated);

    const StaticAnglesAxle& axle = row.axle;
    if (!stated) {
        QString source;
        switch (axle.source) {
        case WheelAttitude::WheelAxis:
            source = tr("from the wheel axis point, %1.").arg(axle.sourcePoint);
            break;
        case WheelAttitude::ContactPatch:
            source = tr("camber from the contact patch %1 under the wheel centre, and zero "
                        "toe, which a patch cannot state.")
                         .arg(axle.sourcePoint);
            break;
        case WheelAttitude::Upright:
        case WheelAttitude::Stated:
            source = tr("nothing in the table says, so the wheel is upright and points straight "
                        "ahead.");
            break;
        }
        row.note->setText(tr("Read off the hardpoints: %1").arg(source));
        return;
    }

    // Where the tyre will touch, near side: the same construction the solver
    // makes, so what is shown here is what the curves will be read against.
    const StaticAlignment alignment{ row.camber->value(), row.toe->value() };
    const Vec3 axis = spinAxisFor(alignment, axle.side);
    bool grounded = false;
    const double radius = tireRadiusToGround(axle.wheelCenter, axis, axle.groundZ, &grounded);
    if (!grounded) {
        row.note->setText(tr("The wheel centre is not above the ground, so there is no contact "
                             "patch to compute."));
        return;
    }
    const Vec3 patch = contactPatchFor(axle.wheelCenter, axis, radius);
    row.note->setText(tr("Contact patch, computed: x %1, y %2, z %3 mm, %4 mm down the wheel's "
                         "own plane from its centre.")
                          .arg(millimetres(patch.x), millimetres(patch.y), millimetres(patch.z),
                               millimetres(radius)));
}

QHash<QString, StaticAlignment> StaticAnglesDialog::alignment() const
{
    QHash<QString, StaticAlignment> alignment;
    for (const Row& row : m_rows)
        if (row.stated->isChecked())
            alignment.insert(row.axle.token,
                             StaticAlignment{ row.camber->value(), row.toe->value() });
    return alignment;
}

} // namespace suspkin
