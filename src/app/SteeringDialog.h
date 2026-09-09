#pragma once

#include "model/HardpointMirror.h"
#include "model/Linkage.h"

#include <QDialog>

#include <vector>

class QComboBox;
class QLabel;

namespace suspkin {

/// Which axle the steering rack drives.
///
/// The question belongs to the axle, not to a hardpoint: both ends of a toe link
/// are bolted to something whichever axle they are on, and what tells a rack
/// apart from a bracket is whether the car has one there. The template already
/// describes the car, so that is where the answer is kept -- this dialog is only
/// a way of writing it without opening the file.
///
/// An axle nobody names cannot be steered: its toe link stays where the workbook
/// put it whatever a sweep asks for. That is the whole point of asking.
class SteeringDialog : public QDialog {
    Q_OBJECT

public:
    /// @p templ supplies the corners and the role the rack would drive,
    /// @p mirror turns one side's name into the other's for the preview, and
    /// @p table is what the preview is checked against.
    SteeringDialog(const LinkageTemplate& templ, const MirrorSpec& mirror,
                   const HardpointTable& table, QWidget* parent = nullptr);

    /// The corners as the user left them, ready to be written into the template.
    /// Every one of them is marked as having been answered, so "no axle on this
    /// car steers" survives a reload instead of reading as a template that was
    /// never asked.
    std::vector<CornerSpec> corners() const;

private:
    void refreshPreview();

    const MirrorSpec m_mirror;
    const HardpointTable& m_table;
    std::vector<CornerSpec> m_corners;
    std::vector<QComboBox*> m_boxes; ///< one per corner, in the same order
    QLabel* m_preview = nullptr;
};

} // namespace suspkin
