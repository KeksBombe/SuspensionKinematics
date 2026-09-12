#pragma once

#include "model/Hardpoint.h"
#include "model/Linkage.h"

#include <QDialog>
#include <QHash>
#include <QStringList>

class QCheckBox;
class QComboBox;
class QDialogButtonBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QTableWidget;

namespace suspkin {

/// A new part through the points the user picked.
///
/// The points are joined in the order they were picked -- which is why the
/// viewport keeps a Ctrl-click selection in order -- and can be put in another
/// order here before anything is written. The part names its points outright
/// (@ref PartTemplate::perCorner off): it is this bracket, on this side, drawn
/// once.
class NewPartDialog : public QDialog {
    Q_OBJECT

public:
    NewPartDialog(const LinkageTemplate& templ, const QStringList& points, QWidget* parent = nullptr);

    /// The part as it will be written into the template.
    PartTemplate part() const;

private:
    void refresh();
    void move(int step);

    const LinkageTemplate& m_templ;
    QLineEdit* m_label = nullptr;
    QComboBox* m_kind = nullptr;
    QListWidget* m_points = nullptr;
    QCheckBox* m_closed = nullptr;
    QLabel* m_id = nullptr;
    QDialogButtonBox* m_buttons = nullptr;
};

/// The template's parts, to be relabelled or taken out.
///
/// Nothing is written until OK, and then only what changed, each as a patch of
/// the user's own file. The last part cannot be deleted: a template with no
/// parts is not a template the reader will open again.
class EditPartsDialog : public QDialog {
    Q_OBJECT

public:
    explicit EditPartsDialog(const LinkageTemplate& templ, QWidget* parent = nullptr);

    /// New labels, by part id -- only for parts whose label changed.
    QHash<QString, QString> relabelled() const;
    /// Ids of the parts to take out.
    QStringList removed() const;

private:
    void refreshButtons();

    const LinkageTemplate& m_templ;
    QTableWidget* m_table = nullptr;
    QStringList m_removed;
    QDialogButtonBox* m_buttons = nullptr;
    class QPushButton* m_rename = nullptr;
    class QPushButton* m_delete = nullptr;
};

/// What a part kind is called in a dialog.
QString partKindLabel(PartKind kind);

} // namespace suspkin
