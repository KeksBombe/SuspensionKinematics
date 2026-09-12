#include "model/Linkage.h"

#include <QCoreApplication>

#include <utility>

namespace suspkin {
namespace {

QString tr(const char* text) { return QCoreApplication::translate("Linkage", text); }

struct KindName {
    PartKind kind;
    const char* text;
};

// The strings the template file uses. Adding a kind means adding a colour in
// ViewportWidget too, which is the only other place a kind means anything.
constexpr KindName kKindNames[] = {
    { PartKind::Wishbone, "wishbone" }, { PartKind::Link, "link" },
    { PartKind::Upright, "upright" },   { PartKind::Rocker, "rocker" },
    { PartKind::Damper, "damper" },     { PartKind::AntiRoll, "antiroll" },
    { PartKind::Wheel, "wheel" },       { PartKind::Other, "other" },
};

const QString kCornerToken = QStringLiteral("{corner}");
const QString kSideToken = QStringLiteral("{side}");

/// A point name with its corner filled in, and then, for the far side, put
/// through the mirror rule. An empty return means the rule does not apply to
/// this name, which is the caller's signal that there is no far side for it.
QString instantiate(const QString& pattern, const QString& corner, bool mirrored,
                    const MirrorSpec& mirror)
{
    QString name = pattern;
    name.replace(kCornerToken, corner);
    if (!mirrored) return name;
    return mirroredName(name, mirror);
}

} // namespace

QString partKindToString(PartKind kind)
{
    for (const KindName& entry : kKindNames)
        if (entry.kind == kind) return QLatin1String(entry.text);
    return QStringLiteral("other");
}

PartKind partKindFromString(const QString& text, PartKind fallback)
{
    for (const KindName& entry : kKindNames)
        if (text.compare(QLatin1String(entry.text), Qt::CaseInsensitive) == 0) return entry.kind;
    return fallback;
}

int ResolvedChain::segmentCount() const
{
    const int count = static_cast<int>(points.size());
    if (count < 2) return 0;
    return closed && count > 2 ? count : count - 1;
}

int LinkagePart::segmentCount() const
{
    int total = 0;
    for (const ResolvedChain& chain : chains) total += chain.segmentCount();
    return total;
}

int Linkage::segmentCount() const
{
    int total = 0;
    for (const LinkagePart& part : parts) total += part.segmentCount();
    return total;
}

namespace {

/// One part for one corner and one side. Nothing is reported from here: whether
/// a missing point is worth mentioning depends on which side is being built,
/// and only the caller knows that.
struct Instance {
    LinkagePart part;
    QStringList missing; ///< names that are not in the table
    int found = 0;       ///< names that are, across every chain
    int wanted = 0;
};

Instance instantiatePart(const PartTemplate& source, const HardpointTable& table,
                         const CornerSpec& corner, bool mirrored, const MirrorSpec& mirror,
                         const QString& sideLabel)
{
    // A part that spells its points out is drawn through exactly those names:
    // no corner to substitute, no far side to mirror onto.
    const bool literal = !source.perCorner;

    Instance instance;
    instance.part.id = literal ? QStringLiteral("%1@literal").arg(source.id)
                               : QStringLiteral("%1@%2:%3")
                                     .arg(source.id, corner.token,
                                          mirrored ? QStringLiteral("mirror")
                                                   : QStringLiteral("base"));
    instance.part.label = source.label;
    instance.part.label.replace(kCornerToken, corner.label);
    instance.part.label.replace(kSideToken, sideLabel);
    instance.part.label = instance.part.label.simplified();
    if (instance.part.label.isEmpty()) instance.part.label = source.id;
    instance.part.kind = source.kind;
    instance.part.corner = corner.token;
    instance.part.mirrored = mirrored;

    for (const ChainTemplate& chain : source.chains) {
        ResolvedChain resolved;
        resolved.closed = chain.closed;
        bool complete = true;

        for (const QString& pattern : chain.points) {
            ++instance.wanted;
            const QString name =
                literal ? pattern : instantiate(pattern, corner.token, mirrored, mirror);
            const int index = name.isEmpty() ? -1 : table.indexOf(name);
            if (index < 0) {
                complete = false;
                // An empty name means the mirror rule does not apply, which is
                // worth saying with the pattern it came from rather than blank.
                if (!chain.optional && !source.optional)
                    instance.missing.append(name.isEmpty() ? pattern : name);
                continue;
            }
            ++instance.found;
            resolved.points.push_back(index);
        }

        // A chain that lost a point is still drawn through the ones it kept: a
        // wishbone missing an inner pickup is more use as one leg than as
        // nothing. Closing it, though, would invent an edge that is not there.
        if (!complete) resolved.closed = false;
        if (resolved.segmentCount() > 0) instance.part.chains.push_back(std::move(resolved));
    }

    return instance;
}

} // namespace

Linkage buildLinkage(const LinkageTemplate& templ, const HardpointTable& table,
                     const MirrorSpec& mirror)
{
    Linkage linkage;
    if (templ.parts.empty() || table.isEmpty()) return linkage;

    // A template without corners is one that spells its point names out in full.
    std::vector<CornerSpec> corners = templ.corners;
    if (corners.empty()) corners.push_back(CornerSpec{});

    const auto report = [&linkage](const Instance& instance) {
        if (!instance.missing.isEmpty()) {
            linkage.warnings.append(tr("%1: no hardpoint named %2")
                                        .arg(instance.part.label,
                                             instance.missing.join(QStringLiteral(", "))));
        }
        if (!instance.part.chains.empty()) linkage.parts.push_back(instance.part);
    };

    for (const CornerSpec& corner : corners) {
        for (int side = 0; side < 2; ++side) {
            const bool mirrored = (side == 1);

            std::vector<Instance> instances;
            instances.reserve(templ.parts.size());
            int found = 0;
            for (const PartTemplate& source : templ.parts) {
                if (!source.perCorner) continue; // drawn once, below
                instances.push_back(
                    instantiatePart(source, table, corner, mirrored, mirror,
                                    mirrored ? templ.mirroredSideLabel : templ.baseSideLabel));
                found += instances.back().found;
            }

            // A corner or a side with not one point in the table is not missing
            // anything -- it is not on this car. A template describes a kind of
            // suspension, and a particular table may be one axle, or one side
            // that has not been mirrored yet. Warning per part about either
            // would bury the warnings that do matter.
            if (found == 0) continue;

            for (const Instance& instance : instances) report(instance);
        }
    }

    // Parts that name their points outright belong to no corner and no side, so
    // there is nothing for "not on this car" to mean: one that has lost a point
    // has lost it, and says so.
    for (const PartTemplate& source : templ.parts) {
        if (source.perCorner) continue;
        report(instantiatePart(source, table, CornerSpec{}, false, mirror, QString()));
    }

    return linkage;
}

} // namespace suspkin
