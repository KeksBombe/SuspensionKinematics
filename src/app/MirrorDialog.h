#pragma once

#include "model/HardpointMirror.h"

#include <QDialog>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QRadioButton;
class QTextBrowser;

namespace suspkin {

/// Options for mirroring hardpoints to the other side of the car, with a live
/// preview of the names that would come out.
///
/// The preview is the point of the dialog. There is no universal naming
/// convention for the two sides of a suspension, so the only way to be sure a
/// rule does what the user meant is to show them the result before it is
/// applied.
class MirrorDialog : public QDialog {
    Q_OBJECT

public:
    /// @p table is previewed against, @p selection is the row the user has
    /// picked in the panel (or -1), and @p spec seeds the controls with whatever
    /// the project last used.
    MirrorDialog(const HardpointTable& table, int selection, const MirrorSpec& spec,
                 QWidget* parent = nullptr);

    MirrorSpec spec() const;
    /// The rows to mirror: empty means all of them.
    std::vector<int> rows() const;

private:
    void refreshPreview();

    const HardpointTable& m_table;
    int m_selection = -1;

    QComboBox* m_axis = nullptr;
    QRadioButton* m_allRows = nullptr;
    QRadioButton* m_selectedRow = nullptr;
    QRadioButton* m_suffix = nullptr;
    QRadioButton* m_prefix = nullptr;
    QRadioButton* m_replace = nullptr;
    QLineEdit* m_affix = nullptr;
    QLineEdit* m_find = nullptr;
    QLineEdit* m_replaceWith = nullptr;
    QCheckBox* m_caseSensitive = nullptr;
    QCheckBox* m_updateExisting = nullptr;
    QCheckBox* m_skipMirrored = nullptr;
    QTextBrowser* m_preview = nullptr;
    QLabel* m_summary = nullptr;
};

} // namespace suspkin
