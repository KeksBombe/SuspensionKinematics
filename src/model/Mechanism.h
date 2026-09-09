#pragma once

#include "model/Hardpoint.h"
#include "model/HardpointMirror.h"

#include <QString>
#include <QStringList>

namespace suspkin {

/// Which body the pushrod picks up on.
///
/// The car this was written for mounts it on the **upper wishbone**; an upright
/// pickup and a lower-arm pickup are the other two ways it is done. This is not
/// a cosmetic choice: it decides what the pushrod's outer end does as the wheel
/// moves, and so it decides the motion ratio.
enum class PushrodMount { UpperArm, LowerArm, Upright };

QString pushrodMountToString(PushrodMount mount);
PushrodMount pushrodMountFromString(const QString& text,
                                    PushrodMount fallback = PushrodMount::UpperArm);

/// Which hardpoint plays which role in one corner, by name.
///
/// This is to the solver what a @ref PartTemplate is to the renderer: written
/// once with {corner} still in it, and instantiated per axle and per side the
/// same way, through the project's own mirror rule. A template file therefore
/// still never spells out a naming convention for left and right.
///
/// The names are all that is here. Nothing in this struct knows a coordinate --
/// binding it to a table is @ref CornerSolver's job.
struct MechanismTemplate {
    /// The lower wishbone: two chassis pivots and the ball joint between them.
    QString lowerFront;
    QString lowerRear;
    QString lowerOuter;

    /// The upper wishbone, the same way round.
    QString upperFront;
    QString upperRear;
    QString upperOuter;

    /// The toe link. Inboard is on the rack, and moves when the wheel is steered.
    QString tieRodInboard;
    QString tieRodOutboard;

    /// What the upright carries besides its three joints.
    QString wheelCenter;
    QString contactPatch;
    /// Anything else rigid with the upright -- a caliper mount, a sensor -- so
    /// that it moves with the wheel instead of standing still in the viewport.
    QStringList carried;

    /// The pushrod, and the body its outer end belongs to.
    PushrodMount pushrodMount = PushrodMount::UpperArm;
    QString pushrodOuter;
    QString pushrodInner;

    /// The rocker: a pivot point and a second point giving its axis.
    QString rockerPivot;
    QString rockerAxis;

    /// The damper. Its outer end rides the rocker; its inboard end is chassis.
    QString damperInboard;
    QString damperOutboard;

    /// The anti-roll bar, all three optional. @ref antiRollRocker is the point
    /// on the rocker the drop link hangs off, @ref antiRollArmOuter is the drop
    /// link's other end on the bar's arm, and @ref antiRollArmPivot is where
    /// that arm meets the bar itself.
    QString antiRollRocker;
    QString antiRollArmOuter;
    QString antiRollArmPivot;

    /// Nothing to solve without a lower wishbone and an upright to hang off it.
    bool isEmpty() const;
    /// A corner may be a plain double wishbone with an outboard damper.
    bool hasRocker() const;
    bool hasAntiRoll() const;
    bool hasTieRod() const { return !tieRodInboard.isEmpty() && !tieRodOutboard.isEmpty(); }

    /// Every name this mechanism mentions, in a fixed order, for reporting which
    /// of them a table did not have.
    QStringList allNames() const;
};

/// @p templ with {corner} replaced by @p cornerToken and, for the far side of
/// the car, every name put through @p mirror.
///
/// A name the mirror rule does not apply to comes back empty, which reads
/// downstream as "this corner does not have one" -- the same way a missing point
/// does. That is deliberate: a half-mirrored table should solve the half it has.
MechanismTemplate instantiateMechanism(const MechanismTemplate& templ, const QString& cornerToken,
                                       bool mirrored, const MirrorSpec& mirror);

/// How much of @p mechanism @p table actually holds: the names it is missing,
/// and whether the ones it needs are all there.
struct MechanismCoverage {
    QStringList missingRequired; ///< without these there is nothing to solve
    QStringList missingOptional; ///< the rocker, the anti-roll bar, the carried points
    /// True when no name at all resolved, which means this corner or this side
    /// is simply not in the table -- not an error, and not worth a warning.
    bool absent = false;

    bool solvable() const { return !absent && missingRequired.isEmpty(); }
};

MechanismCoverage coverMechanism(const MechanismTemplate& mechanism, const HardpointTable& table);

} // namespace suspkin
