#pragma once

#include "model/Hardpoint.h"
#include "model/HardpointMirror.h"
#include "model/Linkage.h"

#include <QHash>
#include <QString>
#include <QStringList>

#include <vector>

namespace suspkin {

/// What the solver is being told to do with a point.
///
/// These are the three things a hardpoint can be in a kinematic model, and they
/// are the same three the mechanism template already distinguishes: a chassis
/// pivot that never moves, a joint whose position falls out of the solve, and a
/// point that is simply carried along by a body that moves.
enum class PointType {
    Unassigned, ///< nobody has said yet; the table is still being filled in
    ToBody,     ///< fixed to the chassis: the solve treats it as ground
    Solved,     ///< a moving joint, positioned by the linkage it belongs to
    Dependent,  ///< rides a body rigidly -- a wheel centre, a caliper mount
};

/// Every type, in the order the dropdown offers them.
inline constexpr PointType kPointTypes[] = { PointType::Unassigned, PointType::ToBody,
                                             PointType::Solved, PointType::Dependent };

/// Stable identifiers for the project file. Round-trip safe: a type written by
/// one release reads back the same in the next.
QString pointTypeToString(PointType type);
PointType pointTypeFromString(const QString& text, PointType fallback = PointType::Unassigned);

/// What the chip in the table says, and what its tooltip explains.
QString pointTypeLabel(PointType type);
QString pointTypeDescription(PointType type);

/// A bushing index of zero means the joint is rigid, which is why it is the
/// default rather than 1. The ceiling is only there to keep a typo out of the
/// project file; no stiffness map is going to run to four digits.
inline constexpr int kNoBushing = 0;
inline constexpr int kMaxBushingIndex = 999;

/// What the user says about one hardpoint, beyond where it is.
///
/// Deliberately separate from @ref Hardpoint. A Hardpoint is what the workbook
/// holds and what gets written back to it; this is the project's own
/// description of the joint, and mixing the two would push it into the edits
/// file as coordinate changes the user never made.
struct HardpointConfig {
    PointType type = PointType::Unassigned;
    /// The two bodies that meet here, by name, from the project's @ref
    /// BodyCatalog. Either may be empty: a point may be described from one side
    /// before the other is known.
    QString part1;
    QString part2;
    /// Which compliance bushing acts at this joint, for a stiffness and damping
    /// map kept outside the kinematics. @ref kNoBushing means a rigid joint.
    int bushing = kNoBushing;

    bool isEmpty() const
    {
        return type == PointType::Unassigned && part1.isEmpty() && part2.isEmpty()
               && bushing == kNoBushing;
    }

    bool namesBody(const QString& body) const { return part1 == body || part2 == body; }

    bool operator==(const HardpointConfig& other) const
    {
        return type == other.type && part1 == other.part1 && part2 == other.part2
               && bushing == other.bushing;
    }
    bool operator!=(const HardpointConfig& other) const { return !(*this == other); }
};

/// One entry per point, keyed by name.
///
/// By name rather than by row because rows move: mirroring appends, a reimported
/// workbook may be in a different order, and a configuration that followed row
/// numbers would silently end up describing the wrong joints.
using HardpointConfigMap = QHash<QString, HardpointConfig>;

/// The bodies a project's linkage describes, which is what the Part columns are
/// allowed to name.
///
/// It comes out of the linkage template rather than being a fixed list, because
/// the template is already the project's statement of what its car is made of.
/// A team that writes a five-link template gets that template's bodies here.
struct BodyCatalog {
    QStringList bodies; ///< ground first, then the template's parts in its order

    /// The chassis, which is in every catalog and is never a template part.
    ///
    /// Not translated on purpose: it is written into project files, so it has to
    /// read back the same whatever language the application is running in.
    static const QString& ground();

    bool isEmpty() const { return bodies.isEmpty(); }
    bool contains(const QString& body) const { return bodies.contains(body); }
};

/// The catalog @p templ implies: the ground body, then one entry per part it
/// describes, named generically -- the part's label with {corner} and {side}
/// taken out, so "{corner} {side} lower wishbone" becomes "Lower wishbone".
BodyCatalog bodyCatalog(const LinkageTemplate& templ);

/// How badly wrong an entry is.
///
/// An error is refused before it reaches the store; a warning is stored and
/// shown, because plenty of half-finished configurations are on their way
/// somewhere and saying "no" to them would only be in the way.
enum class ConfigIssueLevel { Warning, Error };

struct ConfigIssue {
    ConfigIssueLevel level = ConfigIssueLevel::Warning;
    QString message;
};

/// Check @p config against what the model can actually mean.
///
/// This is the whole validation layer, and it is pure: no widget calls it, and
/// the table's edit path is the only thing that has to route through it.
/// An empty @p catalog switches off the body-name check -- a project whose
/// template failed to load has no list to check against, and refusing every
/// edit until it does would be punishing the wrong person.
std::vector<ConfigIssue> validateHardpointConfig(const HardpointConfig& config,
                                                 const BodyCatalog& catalog);

bool hasError(const std::vector<ConfigIssue>& issues);
QStringList issueMessages(const std::vector<ConfigIssue>& issues);

/// What @p templ implies about the points in @p table.
///
/// The types come from the mechanism block -- it already says which point is a
/// chassis pivot and which is an outer ball joint -- and the bodies come from
/// the parts the template draws through each point, so the configuration table
/// and the linkage cannot disagree about what a corner is made of.
///
/// Points the template says nothing about get no entry at all, rather than a
/// guess: an empty row is honest, and a wrong one is not.
HardpointConfigMap inferHardpointConfig(const HardpointTable& table, const LinkageTemplate& templ,
                                        const MirrorSpec& mirror);

/// Fill in @p config from @p inferred wherever it has nothing to say, and
/// report how many entries that added. What the user has set is never touched:
/// inference is a starting point, not an opinion.
int fillMissingConfig(HardpointConfigMap& config, const HardpointConfigMap& inferred);

} // namespace suspkin
