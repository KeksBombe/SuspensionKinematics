#pragma once

#include "model/Sweep.h"

#include <QDialog>

class QCheckBox;
class QDoubleSpinBox;
class QFormLayout;
class QGroupBox;
class QLabel;

namespace suspkin {

/// The numbers a sweep is run with, in a window of their own.
///
/// It is a window rather than a row of boxes in the analysis dock for the
/// reason Lotus gives it one: bump, roll and steer each have their own travel
/// and their own increment, and there is no room beside a plot to show all
/// three at once. Holding all three means changing which one is swept never
/// reinterprets twenty-five millimetres of wheel travel as twenty-five degrees
/// of body roll.
///
/// Nothing waits for a button. Every edit leaves as a signal and the model
/// moves with it, which is what makes the window worth leaving open beside the
/// viewport -- and it is what the rest of the application does, where nothing
/// is ever unsaved. Closing it is therefore the only thing the button box has
/// to offer.
class SweepParametersDialog : public QDialog {
    Q_OBJECT

public:
    explicit SweepParametersDialog(QWidget* parent = nullptr);

    SweepSettings settings() const;
    void setSettings(const SweepSettings& settings);

    /// How long one there-and-back run of the travel takes.
    double animationSeconds() const;
    void setAnimationSeconds(double seconds);

    /// Whether every axle moves, or only the one the curve belongs to.
    bool movesAllAxles() const;
    void setMovesAllAxles(bool all);

    /// Which of the three blocks is the live one. Shown rather than acted on:
    /// the other two stay editable, because setting up a roll sweep before
    /// switching to it is the normal way round.
    void setKind(SweepKind kind);

    /// Whether the selected axle has a steering rack. The steer block and the
    /// held rack travel are switched off for an axle that has none: they are
    /// numbers that would be quietly ignored, and a control that does nothing is
    /// worse than one that is visibly not for you.
    void setSteeringAvailable(bool available);

signals:
    void settingsChanged();
    void animationSecondsChanged();
    void movesAllAxlesChanged();
    /// The window was closed by the user. Its being open is project state like
    /// anything else, so somebody has to hear about it.
    void closedByUser();

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    QDoubleSpinBox* addNumber(QFormLayout* form, const QString& label, double minimum,
                              double maximum, int decimals, double step, const QString& tip);
    /// Retitle the group boxes and rewrite what each block's numbers come to.
    void refreshSummaries();

    QGroupBox* m_bumpGroup = nullptr;
    QGroupBox* m_rollGroup = nullptr;
    QGroupBox* m_steerGroup = nullptr;

    QDoubleSpinBox* m_bumpTravel = nullptr;
    QDoubleSpinBox* m_reboundTravel = nullptr;
    QDoubleSpinBox* m_bumpIncrement = nullptr;
    QDoubleSpinBox* m_rollAngle = nullptr;
    QDoubleSpinBox* m_rollIncrement = nullptr;
    QDoubleSpinBox* m_steerTravel = nullptr;
    QDoubleSpinBox* m_steerIncrement = nullptr;
    QDoubleSpinBox* m_rack = nullptr;
    QDoubleSpinBox* m_seconds = nullptr;
    QCheckBox* m_allAxles = nullptr;

    QLabel* m_bumpSummary = nullptr;
    QLabel* m_rollSummary = nullptr;
    QLabel* m_steerSummary = nullptr;

    SweepKind m_kind = SweepKind::Bump;
    /// Set while the window is being told what to show, so echoing it straight
    /// back out does not look like the user having done something.
    bool m_updating = false;
};

} // namespace suspkin
