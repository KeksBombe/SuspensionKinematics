#include "app/WheelDialog.h"

#include "io/MeshImport.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace suspkin {
namespace {

/// What a corner combo holds when the user has not picked a point for it.
const char kNoPoint[] = "";

} // namespace

WheelDialog::WheelDialog(const HardpointTable& table, const WheelSpec& spec,
                         const QString& wheelPath, const QString& rimPath,
                         const QString& browseDirectory, QWidget* parent)
    : QDialog(parent), m_table(table), m_browseDirectory(browseDirectory)
{
    setWindowTitle(tr("Add wheels"));

    // --- the four centres --------------------------------------------------
    auto* cornerBox = new QGroupBox(tr("Wheel centres"), this);
    auto* cornerForm = new QFormLayout(cornerBox);
    for (const WheelCorner corner : kWheelCorners) {
        auto* combo = new QComboBox(cornerBox);
        combo->addItem(tr("- not used -"), QString::fromLatin1(kNoPoint));
        for (const Hardpoint& point : m_table.points) combo->addItem(point.name, point.name);
        m_corners[static_cast<std::size_t>(corner)] = combo;
        cornerForm->addRow(wheelCornerLabel(corner), combo);
        connect(combo, &QComboBox::currentIndexChanged, this, [this] { refreshSummary(); });
    }

    auto* guess = new QPushButton(tr("Guess from the hardpoint names"), cornerBox);
    guess->setToolTip(tr("Look for points named like a wheel centre and sort them by where they "
                         "are: forward is +X and left is +Y."));
    connect(guess, &QPushButton::clicked, this, [this] {
        WheelSpec guessed = guessWheelSpec(m_table);
        // Only the four points are a guess; how the models are placed is the
        // user's setting and is left alone.
        for (const WheelCorner corner : kWheelCorners) fillCorner(corner, guessed.point(corner));
        refreshSummary();
    });
    cornerForm->addRow(QString(), guess);

    // --- the two models ----------------------------------------------------
    auto* modelBox = new QGroupBox(tr("Models"), this);
    auto* modelForm = new QFormLayout(modelBox);
    m_wheelPath = buildModelRow(modelForm, tr("Wheel:"), wheelPath, tr("Choose the wheel model"));
    m_rimPath = buildModelRow(modelForm, tr("Rim:"), rimPath, tr("Choose the rim model"));

    auto* modelHint = new QLabel(
        tr("One model each, drawn at every wheel centre. Leave a field empty to draw only the "
           "other one. The files are copied into the project."),
        modelBox);
    modelHint->setWordWrap(true);
    modelForm->addRow(modelHint);

    // --- how they are placed ----------------------------------------------
    auto* placementBox = new QGroupBox(tr("Placement"), this);
    auto* placementForm = new QFormLayout(placementBox);

    m_modelSide = new QComboBox(placementBox);
    m_modelSide->addItem(tr("The left side - mirror them for the right"), int(WheelModelSide::Left));
    m_modelSide->addItem(tr("The right side - mirror them for the left"), int(WheelModelSide::Right));
    m_modelSide->addItem(tr("Neither - never mirror them"), int(WheelModelSide::Symmetric));
    m_modelSide->setToolTip(tr("A rim is dished, so the same model cannot simply be dropped onto "
                               "all four corners. The far side is drawn as its mirror image."));
    placementForm->addRow(tr("The models are drawn for:"), m_modelSide);

    m_alignToCenter = new QCheckBox(tr("Put the centre of each model on its hardpoint"),
                                    placementBox);
    m_alignToCenter->setToolTip(tr("On for a wheel modelled on its own, wherever its origin sits. "
                                   "Off for one already positioned in vehicle coordinates, which "
                                   "is then placed by its own origin."));
    placementForm->addRow(m_alignToCenter);

    m_summary = new QLabel(this);
    m_summary->setWordWrap(true);

    m_buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    m_buttons->button(QDialogButtonBox::Ok)->setText(tr("Add wheels"));
    connect(m_buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(m_buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(cornerBox);
    layout->addWidget(modelBox);
    layout->addWidget(placementBox);
    layout->addWidget(m_summary);
    layout->addStretch(1);
    layout->addWidget(m_buttons);

    connect(m_modelSide, &QComboBox::currentIndexChanged, this, [this] { refreshSummary(); });
    connect(m_alignToCenter, &QCheckBox::toggled, this, [this] { refreshSummary(); });

    // A project that has never had wheels gets the guess as its starting point,
    // which is right often enough to be worth the one glance it costs to check.
    applySpec(spec.isEmpty() ? [&] {
        WheelSpec guessed = guessWheelSpec(table);
        guessed.modelSide = spec.modelSide;
        guessed.alignToCenter = spec.alignToCenter;
        return guessed;
    }() : spec);

    refreshSummary();
    resize(560, 520);
}

QLineEdit* WheelDialog::buildModelRow(QFormLayout* form, const QString& label, const QString& path,
                                      const QString& title)
{
    auto* row = new QWidget(this);
    auto* rowLayout = new QHBoxLayout(row);
    rowLayout->setContentsMargins(0, 0, 0, 0);

    auto* edit = new QLineEdit(path, row);
    edit->setClearButtonEnabled(true);
    edit->setPlaceholderText(tr("no model"));
    rowLayout->addWidget(edit, 1);

    auto* browse = new QPushButton(tr("Browse..."), row);
    rowLayout->addWidget(browse);
    connect(browse, &QPushButton::clicked, this, [this, edit, title] {
        // Start where the field already points, so replacing a model lands in
        // the folder the last one came from.
        const QString current = edit->text().trimmed();
        const QString start =
            current.isEmpty() ? m_browseDirectory : QFileInfo(current).absolutePath();
        const QString chosen = QFileDialog::getOpenFileName(this, title, start, importFileFilter());
        if (!chosen.isEmpty()) edit->setText(QDir::toNativeSeparators(chosen));
    });
    connect(edit, &QLineEdit::textChanged, this, [this] { refreshSummary(); });

    form->addRow(label, row);
    return edit;
}

void WheelDialog::fillCorner(WheelCorner corner, const QString& name)
{
    QComboBox* combo = m_corners[static_cast<std::size_t>(corner)];
    int index = combo->findData(name);
    if (index < 0 && !name.isEmpty()) {
        // A point the project remembers that this workbook does not have. Kept
        // as a choice rather than dropped, so importing the right workbook again
        // restores the wheel instead of losing which point it was on.
        combo->addItem(tr("%1 (not in this table)").arg(name), name);
        index = combo->count() - 1;
    }
    combo->setCurrentIndex(index < 0 ? 0 : index);
}

void WheelDialog::applySpec(const WheelSpec& spec)
{
    for (const WheelCorner corner : kWheelCorners) fillCorner(corner, spec.point(corner));
    m_modelSide->setCurrentIndex(m_modelSide->findData(int(spec.modelSide)));
    m_alignToCenter->setChecked(spec.alignToCenter);
}

WheelSpec WheelDialog::spec() const
{
    WheelSpec spec;
    for (const WheelCorner corner : kWheelCorners) {
        spec.setPoint(corner,
                      m_corners[static_cast<std::size_t>(corner)]->currentData().toString());
    }
    spec.modelSide = static_cast<WheelModelSide>(m_modelSide->currentData().toInt());
    spec.alignToCenter = m_alignToCenter->isChecked();
    return spec;
}

QString WheelDialog::wheelPath() const { return m_wheelPath->text().trimmed(); }
QString WheelDialog::rimPath() const { return m_rimPath->text().trimmed(); }

void WheelDialog::refreshSummary()
{
    QStringList warnings;
    const std::vector<WheelPlacement> placements = resolveWheels(spec(), m_table, &warnings);
    const int models = int(!wheelPath().isEmpty()) + int(!rimPath().isEmpty());

    // Both halves are needed for anything to appear: a model with nowhere to go
    // and a centre with nothing to draw are equally invisible.
    const bool usable = !placements.empty() && models > 0;
    m_buttons->button(QDialogButtonBox::Ok)->setEnabled(usable);

    QStringList lines;
    if (placements.empty()) {
        lines << tr("Pick the hardpoint each wheel is centred on.");
    } else if (models == 0) {
        lines << tr("Pick a wheel model, a rim model, or both.");
    } else {
        int mirrored = 0;
        for (const WheelPlacement& placement : placements) mirrored += placement.mirrored ? 1 : 0;
        lines << tr("%1 wheel(s), %2 of them mirrored, from %3 model(s).")
                     .arg(placements.size())
                     .arg(mirrored)
                     .arg(models);
    }
    lines += warnings;
    m_summary->setText(lines.join(QStringLiteral("\n")));
}

} // namespace suspkin
