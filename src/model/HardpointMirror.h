#pragma once

#include "model/Hardpoint.h"

#include <QString>
#include <QStringList>

#include <vector>

namespace suspkin {

/// Which coordinate a mirror negates. Y is the default and the one that means
/// "the other side of the car": the frame is ISO 8855, so the vehicle's
/// longitudinal plane is y = 0.
enum class MirrorAxis { X, Y, Z };

/// How a mirrored point is named. There is no single convention -- some
/// workbooks tag the side with a suffix, some with a prefix, some carry an `L`
/// or `R` in the middle of the name -- so the rule is the user's to pick.
enum class MirrorNaming {
    Suffix,  ///< "F_LCA_O" -> "F_LCA_O_R"
    Prefix,  ///< "F_LCA_O" -> "R_F_LCA_O"
    Replace, ///< "L_LCA_O" -> "R_LCA_O", by substring
};

struct MirrorSpec {
    MirrorAxis axis = MirrorAxis::Y;
    MirrorNaming naming = MirrorNaming::Suffix;
    QString affix = QStringLiteral("_M"); ///< the suffix or prefix to attach
    QString findText;                     ///< Replace: what to look for
    QString replaceText;                  ///< Replace: what to put there
    bool caseSensitive = false;           ///< Replace only
    /// Recompute points that already exist under the mirrored name. Off means
    /// an existing point is left exactly as it is, so re-running a mirror
    /// cannot silently undo hand edits.
    bool updateExisting = true;
    /// Skip points that are themselves the result of a mirror, so mirroring
    /// everything twice does not produce mirrors of mirrors.
    bool skipMirrored = true;

    bool operator==(const MirrorSpec& other) const;
    bool operator!=(const MirrorSpec& other) const { return !(*this == other); }
};

/// The name @p spec would give the mirror of @p name, or an empty string when
/// the rule does not apply to it -- a Replace whose text is not in the name, or
/// any rule that would map the name onto itself.
QString mirroredName(const QString& name, const MirrorSpec& spec);

struct MirrorOutcome {
    HardpointTable table; ///< the input with the mirrored points folded in
    int added = 0;
    int updated = 0;
    int skipped = 0;      ///< the rule did not apply, or the target already existed
    QStringList notes;    ///< why points were skipped, for the user
};

/// Mirror @p rows of @p table (all of it when @p rows is empty) according to
/// @p spec.
///
/// Mirrored points are appended in the order they were derived, so the original
/// rows keep the position the workbook gave them and the new ones arrive as a
/// block underneath -- which is also how they are written back into a sheet.
MirrorOutcome mirrorHardpoints(const HardpointTable& table, const std::vector<int>& rows,
                               const MirrorSpec& spec);

/// Stable identifiers for the project file. Round-trip safe: a spec written by
/// one release reads back the same in the next.
QString mirrorAxisToString(MirrorAxis axis);
MirrorAxis mirrorAxisFromString(const QString& text, MirrorAxis fallback = MirrorAxis::Y);
QString mirrorNamingToString(MirrorNaming naming);
MirrorNaming mirrorNamingFromString(const QString& text, MirrorNaming fallback = MirrorNaming::Suffix);

} // namespace suspkin
