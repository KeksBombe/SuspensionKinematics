#pragma once

#include "model/SuspensionSolver.h"

#include <QDialog>
#include <QHash>
#include <QString>

#include <optional>
#include <vector>

class QCheckBox;
class QDoubleSpinBox;
class QLabel;

namespace suspkin {

/// One axle as the static angles dialog shows it.
struct StaticAnglesAxle {
    QString token;
    QString label;

    /// What the hardpoints say on their own, and where that came from: the
    /// numbers an axle goes back to when the user stops stating them.
    StaticAlignment fromHardpoints;
    WheelAttitude source = WheelAttitude::Upright;
    /// The point the hardpoint angles were read off, as the table names it --
    /// the wheel axis point, or the contact patch -- so the dialog can say
    /// which point to go and look at.
    QString sourcePoint;

    /// What the project states for this axle, if anything.
    std::optional<StaticAlignment> stated;

    /// Enough of the near-side corner to show where the computed contact patch
    /// lands as the numbers are typed: its wheel centre, which side it is on
    /// (+1 left, -1 right) and the height of the ground under it.
    Vec3 wheelCenter;
    double side = 1.0;
    double groundZ = 0.0;
};

/// Static camber and toe, per axle: Lotus's Set Static Angles.
///
/// A number here is what the wheel's axis is built from, and the contact patch
/// with it -- neither has to be placed as a hardpoint. An axle left unticked
/// reads its angles off the hardpoints the way it always did, and the dialog
/// shows what those come to, so ticking one starts from where the car already
/// is rather than from zero.
class StaticAnglesDialog : public QDialog {
    Q_OBJECT

public:
    StaticAnglesDialog(const std::vector<StaticAnglesAxle>& axles, QWidget* parent = nullptr);

    /// The angles for every ticked axle, by corner token. An axle that is not
    /// in here goes back to its hardpoints.
    QHash<QString, StaticAlignment> alignment() const;

private:
    struct Row {
        StaticAnglesAxle axle;
        QCheckBox* stated = nullptr;
        QDoubleSpinBox* camber = nullptr;
        QDoubleSpinBox* toe = nullptr;
        QLabel* note = nullptr;
    };

    void refresh(Row& row);

    std::vector<Row> m_rows;
};

} // namespace suspkin
