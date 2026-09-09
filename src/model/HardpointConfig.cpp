#include "model/HardpointConfig.h"

#include <QCoreApplication>

#include <cstddef>

namespace suspkin {
namespace {

QString tr(const char* text)
{
    return QCoreApplication::translate("HardpointConfig", text);
}

struct TypeName {
    PointType type;
    const char* text;
};

/// The strings the project file uses. Stable: changing one silently retypes
/// every point in every project that was saved with it.
constexpr TypeName kTypeNames[] = {
    { PointType::Unassigned, "unassigned" },
    { PointType::ToBody, "toBody" },
    { PointType::Solved, "solved" },
    { PointType::Dependent, "dependent" },
};

const QString kCornerToken = QStringLiteral("{corner}");
const QString kSideToken = QStringLiteral("{side}");

/// A part's label with the tokens taken out: what the body is called when it is
/// not any particular corner's or side's copy of it.
QString genericLabel(const PartTemplate& part)
{
    QString label = part.label;
    label.remove(kCornerToken);
    label.remove(kSideToken);
    label = label.simplified();
    if (label.isEmpty()) label = part.id;
    if (!label.isEmpty()) label[0] = label[0].toUpper();
    return label;
}

/// The part template an instantiated part came from. Instance ids are
/// "lowerWishbone@F:mirror", so the source id is everything up to the '@'.
QString sourceIdOf(const LinkagePart& part)
{
    return part.id.section(QLatin1Char('@'), 0, 0);
}

} // namespace

QString pointTypeToString(PointType type)
{
    for (const TypeName& entry : kTypeNames)
        if (entry.type == type) return QLatin1String(entry.text);
    return QStringLiteral("unassigned");
}

PointType pointTypeFromString(const QString& text, PointType fallback)
{
    for (const TypeName& entry : kTypeNames)
        if (text.compare(QLatin1String(entry.text), Qt::CaseInsensitive) == 0) return entry.type;
    return fallback;
}

QString pointTypeLabel(PointType type)
{
    switch (type) {
    case PointType::Unassigned: return tr("Unassigned");
    case PointType::ToBody: return tr("To Body/Ground");
    case PointType::Solved: return tr("Solved");
    case PointType::Dependent: return tr("Dependent");
    }
    return tr("Unassigned");
}

QString pointTypeDescription(PointType type)
{
    switch (type) {
    case PointType::Unassigned:
        return tr("No constraint yet. The point is drawn and written back, but the solver is "
                  "not told what it is.");
    case PointType::ToBody:
        return tr("Anchored to the chassis. The point stays where it is through the whole of "
                  "the travel, and the linkage swings about it.");
    case PointType::Solved:
        return tr("A moving joint. Its position comes out of the solve, from the lengths of "
                  "the members that meet here.");
    case PointType::Dependent:
        return tr("Carried rigidly by a body that moves -- a wheel centre on the upright, a "
                  "sensor bracket -- so it follows without being solved for.");
    }
    return QString();
}

const QString& BodyCatalog::ground()
{
    // Not translated: it is written into project files.
    static const QString kGround = QStringLiteral("Body / Ground");
    return kGround;
}

BodyCatalog bodyCatalog(const LinkageTemplate& templ)
{
    BodyCatalog catalog;
    if (templ.parts.empty()) return catalog;

    // The chassis first, because it is the one body every suspension has and
    // the one most rows name.
    catalog.bodies.append(BodyCatalog::ground());
    for (const PartTemplate& part : templ.parts) {
        const QString label = genericLabel(part);
        // Two parts may draw one body -- an upright given a second chain, say --
        // and it should be offered once.
        if (!label.isEmpty() && !catalog.bodies.contains(label)) catalog.bodies.append(label);
    }
    return catalog;
}

std::vector<ConfigIssue> validateHardpointConfig(const HardpointConfig& config,
                                                 const BodyCatalog& catalog)
{
    std::vector<ConfigIssue> issues;
    const auto add = [&issues](ConfigIssueLevel level, const QString& message) {
        issues.push_back(ConfigIssue{ level, message });
    };

    if (config.bushing < kNoBushing || config.bushing > kMaxBushingIndex) {
        add(ConfigIssueLevel::Error, tr("A bushing index runs from %1, which is a rigid joint, "
                                        "to %2.")
                                         .arg(kNoBushing)
                                         .arg(kMaxBushingIndex));
    }

    if (!config.part1.isEmpty() && config.part1 == config.part2) {
        add(ConfigIssueLevel::Error,
            tr("A joint is between two different bodies, and this one names %1 twice.")
                .arg(config.part1));
    }

    // A project whose template did not load has no catalog to check against.
    // Refusing every edit until it does would be blaming the user for it.
    if (!catalog.isEmpty()) {
        for (const QString& body : { config.part1, config.part2 }) {
            if (body.isEmpty() || catalog.contains(body)) continue;
            add(ConfigIssueLevel::Error,
                tr("This project's linkage has no body called \"%1\".").arg(body));
        }
    }

    const bool named = !config.part1.isEmpty() || !config.part2.isEmpty();
    const bool complete = !config.part1.isEmpty() && !config.part2.isEmpty();
    const QString& ground = BodyCatalog::ground();

    switch (config.type) {
    case PointType::ToBody:
        if (!named)
            add(ConfigIssueLevel::Warning,
                tr("A point fixed to the chassis still joins some body to %1; neither is named.")
                    .arg(ground));
        else if (!config.namesBody(ground))
            add(ConfigIssueLevel::Warning,
                tr("A point fixed to the chassis should name %1 as one of its two bodies.")
                    .arg(ground));
        break;

    case PointType::Solved:
        if (config.namesBody(ground))
            add(ConfigIssueLevel::Warning,
                tr("A solved point moves with the linkage, so naming %1 here would hold it "
                   "still.")
                    .arg(ground));
        else if (!complete)
            add(ConfigIssueLevel::Warning,
                tr("A solved point is where two moving bodies meet, and only one is named."));
        break;

    case PointType::Dependent:
        if (config.part1.isEmpty())
            add(ConfigIssueLevel::Warning,
                tr("A dependent point needs the body that carries it."));
        break;

    case PointType::Unassigned:
        if (named || config.bushing != kNoBushing)
            add(ConfigIssueLevel::Warning,
                tr("This point is described but has no type, so the solver has nothing to do "
                   "with it."));
        break;
    }

    if (config.bushing != kNoBushing && !complete) {
        add(ConfigIssueLevel::Warning,
            tr("A bushing acts between two bodies, and this point names %1.")
                .arg(named ? tr("only one") : tr("neither")));
    }

    return issues;
}

bool hasError(const std::vector<ConfigIssue>& issues)
{
    for (const ConfigIssue& issue : issues)
        if (issue.level == ConfigIssueLevel::Error) return true;
    return false;
}

QStringList issueMessages(const std::vector<ConfigIssue>& issues)
{
    QStringList messages;
    messages.reserve(static_cast<int>(issues.size()));
    for (const ConfigIssue& issue : issues) messages.append(issue.message);
    return messages;
}

HardpointConfigMap inferHardpointConfig(const HardpointTable& table, const LinkageTemplate& templ,
                                        const MirrorSpec& mirror)
{
    HardpointConfigMap config;
    if (table.isEmpty() || templ.parts.empty()) return config;

    QHash<QString, QString> labelById;
    for (const PartTemplate& part : templ.parts) labelById.insert(part.id, genericLabel(part));

    // Which bodies are drawn through each point. Resolving the linkage is how
    // this stays honest: the bodies offered here are the ones actually joined
    // at that node, in the order the template lists its parts.
    std::vector<QStringList> bodies(table.size());
    const Linkage linkage = buildLinkage(templ, table, mirror);
    for (const LinkagePart& part : linkage.parts) {
        const QString body = labelById.value(sourceIdOf(part));
        if (body.isEmpty()) continue;
        for (const ResolvedChain& chain : part.chains) {
            for (const int index : chain.points) {
                if (index < 0 || index >= static_cast<int>(bodies.size())) continue;
                QStringList& at = bodies[static_cast<std::size_t>(index)];
                if (!at.contains(body)) at.append(body);
            }
        }
    }

    const auto bodiesAt = [&](const QString& name) -> QStringList {
        const int index = name.isEmpty() ? -1 : table.indexOf(name);
        if (index < 0) return {};
        return bodies[static_cast<std::size_t>(index)];
    };

    const auto assign = [&](const QString& name, PointType type, const QString& part1,
                            const QString& part2) {
        if (name.isEmpty() || table.indexOf(name) < 0) return;
        if (config.contains(name)) return; // whichever corner named it first
        HardpointConfig entry;
        entry.type = type;
        entry.part1 = part1;
        entry.part2 = part2;
        config.insert(name, entry);
    };

    // A chassis pivot joins whatever member it belongs to, to the car.
    const auto grounded = [&](const QString& name) {
        assign(name, PointType::ToBody, bodiesAt(name).value(0), BodyCatalog::ground());
    };
    // A solved joint is where two members meet, and both are drawn through it.
    const auto solved = [&](const QString& name) {
        const QStringList at = bodiesAt(name);
        assign(name, PointType::Solved, at.value(0), at.value(1));
    };
    const auto dependent = [&](const QString& name, const QString& carrier) {
        const QStringList at = bodiesAt(name);
        assign(name, PointType::Dependent, at.value(0, carrier), at.value(1));
    };

    std::vector<CornerSpec> corners = templ.corners;
    if (corners.empty()) corners.push_back(CornerSpec{});

    for (const CornerSpec& corner : corners) {
        for (int side = 0; side < 2; ++side) {
            const MechanismTemplate mechanism =
                instantiateMechanism(templ.mechanism, corner.token, side == 1, mirror);

            grounded(mechanism.lowerFront);
            grounded(mechanism.lowerRear);
            grounded(mechanism.upperFront);
            grounded(mechanism.upperRear);
            grounded(mechanism.tieRodInboard);
            grounded(mechanism.rockerPivot);
            grounded(mechanism.rockerAxis);
            grounded(mechanism.damperInboard);
            grounded(mechanism.antiRollArmPivot);

            solved(mechanism.lowerOuter);
            solved(mechanism.upperOuter);
            solved(mechanism.tieRodOutboard);
            solved(mechanism.pushrodInner);
            solved(mechanism.damperOutboard);
            solved(mechanism.antiRollRocker);
            solved(mechanism.antiRollArmOuter);

            // The pushrod's outer end is the one place where what the template
            // draws and what the mechanism means come apart: a "pickup" member
            // is a way of showing which arm carries the rod, not a body. The
            // mount says which body it really is.
            {
                const QStringList outer = bodiesAt(mechanism.pushrodOuter);
                const QStringList inner = bodiesAt(mechanism.pushrodInner);
                QString rod;
                for (const QString& body : outer) {
                    if (!inner.contains(body)) continue;
                    rod = body; // the body both ends of the rod belong to
                    break;
                }
                if (rod.isEmpty()) rod = outer.value(0);

                QString mount;
                switch (mechanism.pushrodMount) {
                case PushrodMount::UpperArm: mount = bodiesAt(mechanism.upperOuter).value(0); break;
                case PushrodMount::LowerArm: mount = bodiesAt(mechanism.lowerOuter).value(0); break;
                case PushrodMount::Upright: mount = bodiesAt(mechanism.wheelCenter).value(0); break;
                }
                if (mount == rod) mount.clear();
                assign(mechanism.pushrodOuter, PointType::Solved, rod, mount);
            }

            // The upright is what carries everything outboard, and it is the
            // first body drawn through the wheel centre.
            const QString upright = bodiesAt(mechanism.wheelCenter).value(0);
            dependent(mechanism.wheelCenter, upright);
            dependent(mechanism.contactPatch, upright);
            for (const QString& carried : mechanism.carried) dependent(carried, upright);
        }
    }

    return config;
}

int fillMissingConfig(HardpointConfigMap& config, const HardpointConfigMap& inferred)
{
    int added = 0;
    for (auto it = inferred.constBegin(); it != inferred.constEnd(); ++it) {
        // Presence is the test, not emptiness: a row the user cleared on purpose
        // is an answer, and inference should not talk over it.
        if (config.contains(it.key())) continue;
        config.insert(it.key(), it.value());
        ++added;
    }
    return added;
}

} // namespace suspkin
