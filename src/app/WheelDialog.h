#pragma once

#include "model/Wheels.h"

#include <QDialog>
#include <QString>

#include <array>

class QCheckBox;
class QComboBox;
class QDialogButtonBox;
class QFormLayout;
class QLabel;
class QLineEdit;

namespace suspkin {

/// Which hardpoints the four wheels are centred on, and which two models are
/// drawn there.
///
/// The models are picked as files on disk; the project copies them in for
/// itself, as it does with everything else that is imported. A path handed to
/// the dialog and handed back unchanged means "keep the copy the project already
/// has", and an empty one means that model is not drawn at all.
class WheelDialog : public QDialog {
    Q_OBJECT

public:
    /// @p table is what the corner lists are filled from, @p spec seeds the
    /// controls with whatever the project last used, @p wheelPath and @p rimPath
    /// are the models it already holds, and @p browseDirectory is where the file
    /// dialog starts when there is nothing better to go on.
    WheelDialog(const HardpointTable& table, const WheelSpec& spec, const QString& wheelPath,
                const QString& rimPath, const QString& browseDirectory,
                QWidget* parent = nullptr);

    WheelSpec spec() const;
    QString wheelPath() const;
    QString rimPath() const;

private:
    /// One model row: the path field and the button that fills it in.
    QLineEdit* buildModelRow(QFormLayout* form, const QString& label, const QString& path,
                             const QString& title);
    void fillCorner(WheelCorner corner, const QString& name);
    void applySpec(const WheelSpec& spec);
    void refreshSummary();

    const HardpointTable& m_table;
    QString m_browseDirectory;

    std::array<QComboBox*, kWheelCornerCount> m_corners{};
    QLineEdit* m_wheelPath = nullptr;
    QLineEdit* m_rimPath = nullptr;
    QComboBox* m_modelSide = nullptr;
    QCheckBox* m_alignToCenter = nullptr;
    QLabel* m_summary = nullptr;
    QDialogButtonBox* m_buttons = nullptr;
};

} // namespace suspkin
