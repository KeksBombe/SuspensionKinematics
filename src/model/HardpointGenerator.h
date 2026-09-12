#pragma once

#include "geom/Vec3.h"
#include "model/DesignParameters.h"
#include "model/Hardpoint.h"
#include "model/HardpointMirror.h"
#include "model/Linkage.h"
#include "model/Mechanism.h"

#include <QString>
#include <QStringList>

#include <array>
#include <vector>

namespace suspkin {

class MeshQuery;

/// What the generator produces: roles, never names. The project's own linkage
/// template says what each role is called, so a team whose template names its
/// lower front pivot "FL_LOA_front" gets exactly that.
///
/// The rocker group -- pushrod inner, rocker, damper, anti-roll bar -- is not
/// here on purpose. It is packaging, not a consequence of vehicle targets, and
/// is placed by hand.
enum class DesignRole {
    LowerFront,
    LowerRear,
    LowerOuter,
    UpperFront,
    UpperRear,
    UpperOuter,
    TieRodInboard,
    TieRodOutboard,
    WheelCenter,
    WheelAxis,
    ContactPatch,
};

inline constexpr DesignRole kDesignRoles[] = {
    DesignRole::LowerFront,    DesignRole::LowerRear,      DesignRole::LowerOuter,
    DesignRole::UpperFront,    DesignRole::UpperRear,      DesignRole::UpperOuter,
    DesignRole::TieRodInboard, DesignRole::TieRodOutboard, DesignRole::WheelCenter,
    DesignRole::WheelAxis,     DesignRole::ContactPatch,
};
inline constexpr std::size_t kDesignRoleCount = std::size(kDesignRoles);

/// What a role is, for a person: "lower front pivot".
QString designRoleLabel(DesignRole role);
/// The name @p mechanism gives @p role, or empty when it names none.
QString mechanismName(const MechanismTemplate& mechanism, DesignRole role);

/// Step 12 of the construction: the one chassis pivot that was left off the
/// tie rod's plane, and where it would have to be to lie on it.
///
/// Advice, never applied. Zero bump steer wants the tie rod's instant axis to
/// pass through the wishbones' instant centre; three pivots and the tie rod
/// inboard make a plane, and moving the fourth onto it -- along its own leg, so
/// the arm keeps its planform -- is what the geometry asks for.
struct CoplanarityAdvice {
    bool valid = false;
    DesignRole pivot = DesignRole::LowerFront;
    Vec3 placed;   ///< where the generator put it
    Vec3 advised;  ///< where the plane of the other three meets its leg
    double distance = 0.0;
};

/// One generated corner, on one side: every role's position and the
/// construction that produced it, which is what the tests and the dialog read.
struct GeneratedCorner {
    bool ok = false;
    QString error;         ///< why nothing came out, when nothing did
    QStringList warnings;  ///< targets that were met, but at a price worth knowing

    AxlePosition axle = AxlePosition::Front;
    double side = 1.0; ///< +1 on the left, -1 on the right

    std::array<Vec3, kDesignRoleCount> points{};

    /// The construction.
    Vec3 spinAxis;          ///< outboard, unit: camber and toe in one direction
    double tyreRadius = 0.0; ///< down the wheel's own plane to the ground
    Vec3 steeringPierce;    ///< where the steering axis meets the ground
    Vec3 steeringDirection; ///< unit, pointing up
    Vec3 rollCentre;        ///< front-view instant centre
    Vec3 pitchCentre;       ///< side-view instant centre
    Vec3 upperNormal;       ///< the upper wishbone's plane
    Vec3 lowerNormal;
    /// Which pivots were put against the chassis rather than on their line.
    int pivotsOnChassis = 0;
    CoplanarityAdvice advice;

    const Vec3& at(DesignRole role) const { return points[static_cast<std::size_t>(role)]; }
    Vec3& at(DesignRole role) { return points[static_cast<std::size_t>(role)]; }
};

/// Build one corner out of @p parameters: section 3 of
/// docs/HARDPOINT_GENERATOR_PLAN.md, step for step.
///
/// Pure. @p chassis, when given and asked for, is what the inboard pivots are
/// put against; without it they go on their pivot lines. The contact patch is
/// computed with the solver's own contactPatchFor() and tireRadiusToGround(),
/// so the generator and the solver cannot drift apart about where a tyre
/// touches.
GeneratedCorner generateCorner(const DesignParameters& parameters, AxlePosition axle,
                               const MeshQuery* chassis = nullptr);

/// @p corner in the project's own vocabulary: one point per role that
/// @p mechanism -- already instantiated for this corner and side -- has a name
/// for. A role it does not name is not written.
HardpointTable bindGeneratedCorner(const GeneratedCorner& corner, const MechanismTemplate& mechanism);

/// What generating would do to one point.
struct DesignChange {
    enum class Kind { Added, Moved, Unchanged };

    QString name;
    Kind kind = Kind::Added;
    double distance = 0.0;   ///< how far a moved point moves
    /// It differs from what the workbook holds -- or was never in it -- and
    /// that difference is about to be written over. The thing the preview is
    /// for.
    bool handEdited = false;
    bool mirrored = false;   ///< the far side, from the mirror rule
};

/// The whole of a generation, worked out and not yet applied: what the dialog
/// previews, and what the window applies if the user says yes.
struct DesignPlan {
    QString error; ///< nothing can be generated, and why

    /// @p current with every generated point, and its mirror, folded in.
    HardpointTable table;
    std::vector<DesignChange> changes;
    std::vector<GeneratedCorner> corners;
    QStringList warnings;
    /// Step 12, as sentences a person can act on.
    QStringList advice;

    /// The template's corners with the steering the generation implies:
    /// a rack on each axle generated as steered, an explicit "none" on each
    /// generated as not. @ref steeringChanged says whether that differs from
    /// what the template already says.
    std::vector<CornerSpec> steering;
    bool steeringChanged = false;

    bool ok() const { return error.isEmpty() && !corners.empty(); }
    int count(DesignChange::Kind kind) const;
    int handEditedCount() const;
};

/// Generate every axle @p parameters asks for into @p current, name the points
/// through @p templ's mechanism, and put the far side through @p mirror.
/// @p baseline is the table as the workbook holds it, which is how a point the
/// user has changed by hand is told apart from one they have not.
DesignPlan planDesign(const DesignParameters& parameters, const LinkageTemplate& templ,
                      const MirrorSpec& mirror, const HardpointTable& current,
                      const HardpointTable& baseline, const MeshQuery* chassis = nullptr);

} // namespace suspkin
