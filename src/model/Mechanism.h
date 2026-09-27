#pragma once

#include "model/Hardpoint.h"
#include "model/HardpointMirror.h"

#include <QMap>
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

/// Which body the anti-roll bar's drop link picks up on.
///
/// A rocker pickup is what a pushrod car usually does, and what the built-in
/// template assumes. The others are the ways a bar is hung straight off the
/// wheel -- a drop link to the upright, or to either wishbone -- and they need
/// no rocker at all.
enum class DropLinkMount { Rocker, UpperArm, LowerArm, Upright };

QString dropLinkMountToString(DropLinkMount mount);
DropLinkMount dropLinkMountFromString(const QString& text,
                                      DropLinkMount fallback = DropLinkMount::Rocker);

/// Point names a template used to use, keyed by the name it uses now -- both
/// still carrying {corner}. A table that holds the old name and not the new one
/// is read through the old one, which is how a workbook measured before a
/// rename keeps working without anybody editing it.
using FormerNames = QMap<QString, QString>;

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

    /// The hardpoint the steering rack drives, or empty for an axle with no
    /// rack -- whose toe link inboard end is then simply bolted to the chassis
    /// and stays there whatever a sweep asks for.
    ///
    /// Not read from the mechanism block, which is shared by every corner:
    /// @ref AxleSolver fills it in per corner from that corner's own
    /// @c CornerSpec before instantiating, so it goes through the same {corner}
    /// substitution and the same mirror rule as every other name here.
    QString steeringRack;

    /// What the upright carries besides its three joints.
    QString wheelCenter;
    /// A second point on the wheel's own axis of rotation, rigid with the
    /// upright. It is what says which way the wheel points: camber and toe are
    /// this direction read in two different views, and it is what turns the
    /// wheel model with the steering instead of leaving it standing.
    ///
    /// Either side of the wheel centre will do -- the direction is oriented
    /// outboard whichever end of the axle was measured.
    QString wheelAxis;
    /// Where the tyre touches the ground. Optional, and no longer the thing the
    /// wheel's orientation is read off: with @ref wheelAxis named it is computed
    /// -- straight down the wheel's own plane from the centre onto the ground --
    /// and a point named here only supplies the tyre radius to compute it at.
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

    /// The anti-roll bar, all of it optional.
    ///
    /// @ref antiRollDropLinkOuter is the drop link's upper end, on the body
    /// @ref antiRollMount names. @ref antiRollArmEnd is its lower end, on the
    /// bar's arm, and @ref antiRollArmRoot is where that arm meets the bar --
    /// a point on the bar's axis of rotation.
    ///
    /// @ref antiRollBearing is a second point on that axis, rigid with the
    /// chassis: the bar's bearing. With it named, the axis is the line through
    /// the arm root and the bearing, and each side has its own. Without it the
    /// bar is taken to be straight between the two arm roots, which needs both
    /// sides of the axle -- and one side on its own falls back to the y axis.
    DropLinkMount antiRollMount = DropLinkMount::Rocker;
    QString antiRollDropLinkOuter;
    QString antiRollArmEnd;
    QString antiRollArmRoot;
    QString antiRollBearing;

    /// Names the points above used to have, for a table that still uses them.
    /// Not a role: what it maps is names, and a template's parts are read
    /// through it too.
    FormerNames formerNames;

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

/// The same, with each name looked up in @p table: a name the table does not
/// hold, whose @ref MechanismTemplate::formerNames entry it does, comes back as
/// that former name. Everything else is exactly what the overload above gives.
MechanismTemplate instantiateMechanism(const MechanismTemplate& templ, const QString& cornerToken,
                                       bool mirrored, const MirrorSpec& mirror,
                                       const HardpointTable& table);

/// One point name instantiated the way instantiateMechanism() does it: {corner}
/// filled in, mirrored for the far side, and read through its former name when
/// @p table has only that. Empty for an empty pattern, or one the mirror rule
/// does not apply to.
QString resolvePointName(const QString& pattern, const QString& cornerToken, bool mirrored,
                         const MirrorSpec& mirror, const FormerNames& formerNames,
                         const HardpointTable& table);

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
