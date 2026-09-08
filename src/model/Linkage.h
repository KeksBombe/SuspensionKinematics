#pragma once

#include "model/Hardpoint.h"
#include "model/HardpointMirror.h"

#include <QString>
#include <QStringList>

#include <vector>

namespace suspkin {

/// What a part is, which is the only thing the renderer needs to know about it.
///
/// The list is deliberately about function rather than about shape: a tie rod, a
/// pushrod and an anti-roll drop link are all two-force members and read as one
/// family, while a wishbone and an upright are bodies.
enum class PartKind { Wishbone, Link, Upright, Rocker, Damper, AntiRoll, Wheel, Other };

/// Stable identifiers for the template file, which is the user's to edit.
QString partKindToString(PartKind kind);
PartKind partKindFromString(const QString& text, PartKind fallback = PartKind::Other);

/// One run of hardpoints, drawn as a polyline. Closing it is what turns three
/// points into an A-arm instead of two loose legs.
struct ChainTemplate {
    QStringList points; ///< hardpoint names, still carrying {corner}
    bool closed = false;
    /// Say nothing when these points are absent -- for what a car may not have.
    bool optional = false;
};

/// One part as the template describes it: names and tokens, no coordinates.
struct PartTemplate {
    QString id;    ///< "lowerWishbone"; unique within the template
    QString label; ///< "{corner} {side} lower wishbone"
    PartKind kind = PartKind::Other;
    std::vector<ChainTemplate> chains;
    bool optional = false;
};

/// An axle, or whatever else the point names are grouped by.
struct CornerSpec {
    QString token; ///< what {corner} is replaced with, e.g. "F"
    QString label; ///< what {corner} becomes in a label, e.g. "Front"
};

/// The rule for turning a hardpoint table into parts.
///
/// It names one corner's worth of parts and one side of the car. The other
/// corners come from @ref corners, and the other side from the project's own
/// mirror rule, so a template never has to repeat itself and never has to know
/// which naming convention a particular team uses for left and right.
struct LinkageTemplate {
    QString name;
    QString description;
    QStringList notes; ///< the file's own documentation; JSON has no comments
    std::vector<CornerSpec> corners;
    QString baseSideLabel;     ///< what {side} becomes for the points as named
    QString mirroredSideLabel; ///< and for their mirrors
    std::vector<PartTemplate> parts;

    bool isEmpty() const { return parts.empty(); }
};

/// A chain with its hardpoints found: indices into the table it was resolved
/// against, which is also the order the viewport holds its markers in.
struct ResolvedChain {
    std::vector<int> points;
    bool closed = false;

    int segmentCount() const;
};

/// One part of one corner of one side, ready to draw.
struct LinkagePart {
    QString id;    ///< "lowerWishbone@F:base", unique across the linkage
    QString label; ///< "Front left lower wishbone"
    PartKind kind = PartKind::Other;
    QString corner;        ///< the token it came from, empty when there are none
    bool mirrored = false; ///< the far side, built through the mirror rule
    std::vector<ResolvedChain> chains;

    int segmentCount() const;
};

/// Every part a template found in a table.
struct Linkage {
    std::vector<LinkagePart> parts;
    /// Points a part asked for and did not get. Not an error: a template
    /// describes a kind of car, and a particular car may be missing a corner.
    QStringList warnings;

    bool isEmpty() const { return parts.empty(); }
    int segmentCount() const;
};

/// Build the parts @p templ describes out of @p table.
///
/// Each part is instantiated once per corner, then a second time with every
/// point name put through @p mirror. A corner or a side with not one of its
/// points in the table is dropped without comment, so a table holding one axle,
/// or one side that has not been mirrored yet, yields exactly what it holds.
Linkage buildLinkage(const LinkageTemplate& templ, const HardpointTable& table,
                     const MirrorSpec& mirror);

} // namespace suspkin
