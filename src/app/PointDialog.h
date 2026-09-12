#pragma once

#include "model/Hardpoint.h"

#include <QDialog>

class QDialogButtonBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;

namespace suspkin {

/// A new point: its name and where it is.
///
/// The name is checked as it is typed, by the same rule the table's own name
/// column is -- hardpointNameProblem() -- so the answer to "why is OK greyed
/// out" is on screen before anybody presses it.
class PointDialog : public QDialog {
    Q_OBJECT

public:
    /// @p table is what the name must not collide with. @p seed is where the
    /// new point starts: the selected point, so it begins next to the one being
    /// worked on, with a name that is free. @p note, when given, is a line of
    /// explanation shown above the fields.
    PointDialog(const HardpointTable& table, const Hardpoint& seed, const QString& note,
                QWidget* parent = nullptr);

    Hardpoint point() const;

private:
    void refresh();

    const HardpointTable& m_table;
    QLineEdit* m_name = nullptr;
    QDoubleSpinBox* m_coord[3] = { nullptr, nullptr, nullptr };
    QLabel* m_problem = nullptr;
    QDialogButtonBox* m_buttons = nullptr;
};

/// The first name after @p base that @p table does not have yet: "F_LCA_O_2",
/// "F_LCA_O_3", ... -- or @p base itself when that is free. What a new point
/// seeded from a selected one is offered, so the dialog opens on something
/// that can be accepted.
QString freePointName(const HardpointTable& table, const QString& base);

} // namespace suspkin
