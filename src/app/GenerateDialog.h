#pragma once

#include "model/DesignParameters.h"
#include "model/HardpointGenerator.h"

#include <QDialog>

#include <functional>
#include <vector>

class QCheckBox;
class QComboBox;
class QDialogButtonBox;
class QDoubleSpinBox;
class QGridLayout;
class QLabel;
class QTimer;
class QTreeWidget;

namespace suspkin {

class MeshQuery;

/// Hardpoints > Generate from Design: the targets on the left, and on the
/// right what generating them would do to the table -- which points arrive,
/// which move and by how much, and which of the user's own edits would be
/// written over.
///
/// That preview is the point of the dialog. Generation is one-shot: changing a
/// target here moves nothing until Generate is pressed, and what is about to be
/// overwritten is shown before it is. The coplanarity advice is shown here too,
/// as advice; nothing in this dialog applies it.
class GenerateDialog : public QDialog {
    Q_OBJECT

public:
    /// Where a chassis to put the pivots against comes from. Asked for only
    /// when the user ticks the box, because building one over a big model takes
    /// a moment; nothing when the project has no geometry.
    using ChassisSource = std::function<const MeshQuery*()>;

    GenerateDialog(const DesignParameters& seed, const LinkageTemplate& templ,
                   const MirrorSpec& mirror, const HardpointTable& current,
                   const HardpointTable& baseline, bool chassisAvailable, ChassisSource chassis,
                   QWidget* parent = nullptr);

    /// The targets as they stand in the dialog, whether or not it was accepted:
    /// they are the project's to keep either way.
    DesignParameters parameters() const;
    /// What Generate would do, for the targets as they stand.
    const DesignPlan& plan() const { return m_plan; }

public slots:
    void accept() override;

private:
    struct AxleNumber {
        double AxleDesign::*member;
        QDoubleSpinBox* spin[2];
    };
    struct CarNumber {
        double DesignParameters::*member;
        QDoubleSpinBox* spin;
    };

    QWidget* buildTargets();
    QWidget* buildPreview();
    QDoubleSpinBox* number(double minimum, double maximum, int decimals, const QString& suffix,
                           const QString& tip);
    void addHeading(QGridLayout* grid, int* row, const QString& text);
    void load(const DesignParameters& parameters);
    void schedulePlan();
    void rebuildPlan();
    void showPlan();

    const LinkageTemplate& m_templ;
    MirrorSpec m_mirror;
    const HardpointTable& m_current;
    const HardpointTable& m_baseline;
    bool m_chassisAvailable = false;
    ChassisSource m_chassis;

    DesignParameters m_seed; ///< what the dialog opened with: fields it has no widget for
    std::vector<AxleNumber> m_axleNumbers;
    std::vector<CarNumber> m_carNumbers;
    QCheckBox* m_generate[2] = { nullptr, nullptr };
    QComboBox* m_corner[2] = { nullptr, nullptr };
    QCheckBox* m_steered[2] = { nullptr, nullptr };
    QComboBox* m_advised[2] = { nullptr, nullptr };
    QComboBox* m_side = nullptr;
    QCheckBox* m_useChassis = nullptr;

    QLabel* m_summary = nullptr;
    QTreeWidget* m_preview = nullptr;
    QLabel* m_notes = nullptr;
    QDialogButtonBox* m_buttons = nullptr;
    QTimer* m_timer = nullptr;

    DesignPlan m_plan;
    /// Set while values are being pushed into the widgets, so that is not
    /// taken for the user typing them.
    bool m_loading = false;
};

} // namespace suspkin
