#include "io/LinkageTemplate.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <utility>

namespace suspkin {
namespace {

QString tr(const char* text) { return QCoreApplication::translate("LinkageTemplate", text); }

constexpr int kFormatVersion = 1;
const char kFormatTag[] = "suspkin-linkage-template";
const char kBuiltinResource[] = ":/templates/double_wishbone_pushrod.json";

QStringList stringList(const QJsonValue& value)
{
    QStringList list;
    for (const QJsonValue& entry : value.toArray()) {
        const QString text = entry.toString();
        if (!text.isEmpty()) list.append(text);
    }
    return list;
}

/// A chain from either of the two forms the file allows: the long one, an object
/// with its own points and flags, and the shorthand, a part that is a single
/// chain and says so with "points" directly.
std::optional<ChainTemplate> chainFromJson(const QJsonObject& object)
{
    ChainTemplate chain;
    chain.points = stringList(object.value(QStringLiteral("points")));
    chain.closed = object.value(QStringLiteral("closed")).toBool(false);
    chain.optional = object.value(QStringLiteral("optional")).toBool(false);
    if (chain.points.size() < 2) return std::nullopt;
    return chain;
}

QJsonObject chainToJson(const ChainTemplate& chain)
{
    QJsonObject object;
    QJsonArray points;
    for (const QString& name : chain.points) points.append(name);
    object.insert(QStringLiteral("points"), points);
    if (chain.closed) object.insert(QStringLiteral("closed"), true);
    if (chain.optional) object.insert(QStringLiteral("optional"), true);
    return object;
}

/// One named string out of a sub-object, e.g. mechanism.lowerWishbone.outer.
QString roleName(const QJsonObject& parent, const char* group, const char* key)
{
    return parent.value(QLatin1String(group)).toObject().value(QLatin1String(key)).toString();
}

/// The mechanism block: which hardpoint plays which role, grouped by the part it
/// belongs to so the file reads like the suspension it describes.
MechanismTemplate mechanismFromJson(const QJsonObject& root)
{
    MechanismTemplate mechanism;
    if (!root.contains(QStringLiteral("mechanism"))) return mechanism;
    const QJsonObject object = root.value(QStringLiteral("mechanism")).toObject();

    mechanism.lowerFront = roleName(object, "lowerWishbone", "front");
    mechanism.lowerRear = roleName(object, "lowerWishbone", "rear");
    mechanism.lowerOuter = roleName(object, "lowerWishbone", "outer");
    mechanism.upperFront = roleName(object, "upperWishbone", "front");
    mechanism.upperRear = roleName(object, "upperWishbone", "rear");
    mechanism.upperOuter = roleName(object, "upperWishbone", "outer");
    mechanism.tieRodInboard = roleName(object, "tieRod", "inboard");
    mechanism.tieRodOutboard = roleName(object, "tieRod", "outboard");
    mechanism.wheelCenter = roleName(object, "upright", "wheelCenter");
    mechanism.contactPatch = roleName(object, "upright", "contactPatch");
    mechanism.carried = stringList(
        object.value(QStringLiteral("upright")).toObject().value(QStringLiteral("carries")));
    mechanism.pushrodMount = pushrodMountFromString(
        object.value(QStringLiteral("pushrod")).toObject().value(QStringLiteral("mount")).toString());
    mechanism.pushrodOuter = roleName(object, "pushrod", "outer");
    mechanism.pushrodInner = roleName(object, "pushrod", "inner");
    mechanism.rockerPivot = roleName(object, "rocker", "pivot");
    mechanism.rockerAxis = roleName(object, "rocker", "axis");
    mechanism.damperInboard = roleName(object, "damper", "inboard");
    mechanism.damperOutboard = roleName(object, "damper", "outboard");
    mechanism.antiRollRocker = roleName(object, "antiRollBar", "rocker");
    mechanism.antiRollArmOuter = roleName(object, "antiRollBar", "armOuter");
    mechanism.antiRollArmPivot = roleName(object, "antiRollBar", "armPivot");
    return mechanism;
}

/// A group of roles, left out entirely when none of them is named -- a corner
/// with no anti-roll bar should not carry three empty strings about.
void insertGroup(QJsonObject& parent, const char* group,
                 std::initializer_list<std::pair<const char*, const QString*>> roles)
{
    QJsonObject object;
    for (const auto& role : roles)
        if (!role.second->isEmpty()) object.insert(QLatin1String(role.first), *role.second);
    if (!object.isEmpty()) parent.insert(QLatin1String(group), object);
}

QJsonObject mechanismToJson(const MechanismTemplate& mechanism)
{
    QJsonObject object;
    insertGroup(object, "lowerWishbone",
                { { "front", &mechanism.lowerFront },
                  { "rear", &mechanism.lowerRear },
                  { "outer", &mechanism.lowerOuter } });
    insertGroup(object, "upperWishbone",
                { { "front", &mechanism.upperFront },
                  { "rear", &mechanism.upperRear },
                  { "outer", &mechanism.upperOuter } });
    insertGroup(object, "tieRod",
                { { "inboard", &mechanism.tieRodInboard },
                  { "outboard", &mechanism.tieRodOutboard } });

    QJsonObject upright;
    if (!mechanism.wheelCenter.isEmpty())
        upright.insert(QStringLiteral("wheelCenter"), mechanism.wheelCenter);
    if (!mechanism.contactPatch.isEmpty())
        upright.insert(QStringLiteral("contactPatch"), mechanism.contactPatch);
    if (!mechanism.carried.isEmpty()) {
        QJsonArray carries;
        for (const QString& name : mechanism.carried) carries.append(name);
        upright.insert(QStringLiteral("carries"), carries);
    }
    if (!upright.isEmpty()) object.insert(QStringLiteral("upright"), upright);

    if (!mechanism.pushrodOuter.isEmpty() || !mechanism.pushrodInner.isEmpty()) {
        QJsonObject pushrod;
        pushrod.insert(QStringLiteral("mount"), pushrodMountToString(mechanism.pushrodMount));
        if (!mechanism.pushrodOuter.isEmpty())
            pushrod.insert(QStringLiteral("outer"), mechanism.pushrodOuter);
        if (!mechanism.pushrodInner.isEmpty())
            pushrod.insert(QStringLiteral("inner"), mechanism.pushrodInner);
        object.insert(QStringLiteral("pushrod"), pushrod);
    }

    insertGroup(object, "rocker",
                { { "pivot", &mechanism.rockerPivot }, { "axis", &mechanism.rockerAxis } });
    insertGroup(object, "damper",
                { { "inboard", &mechanism.damperInboard },
                  { "outboard", &mechanism.damperOutboard } });
    insertGroup(object, "antiRollBar",
                { { "rocker", &mechanism.antiRollRocker },
                  { "armOuter", &mechanism.antiRollArmOuter },
                  { "armPivot", &mechanism.antiRollArmPivot } });
    return object;
}

} // namespace

LinkageTemplateLoadResult readLinkageTemplate(const QByteArray& bytes, const QString& label)
{
    LinkageTemplateLoadResult result;

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(bytes, &parseError);
    if (document.isNull() || !document.isObject()) {
        result.error = tr("%1 is not readable JSON: %2 (at offset %3)")
                           .arg(label, parseError.errorString())
                           .arg(parseError.offset);
        return result;
    }

    const QJsonObject root = document.object();
    if (root.value(QStringLiteral("format")).toString() != QLatin1String(kFormatTag)) {
        result.error = tr("%1 is not a linkage template.").arg(label);
        return result;
    }
    if (root.value(QStringLiteral("formatVersion")).toInt(1) > kFormatVersion) {
        result.error = tr("%1 was written by a newer version of SuspensionKinematics.").arg(label);
        return result;
    }

    LinkageTemplate templ;
    templ.name = root.value(QStringLiteral("name")).toString();
    templ.description = root.value(QStringLiteral("description")).toString();
    templ.notes = stringList(root.value(QStringLiteral("notes")));

    for (const QJsonValue& value : root.value(QStringLiteral("corners")).toArray()) {
        CornerSpec corner;
        // A bare string is a token that is its own label, which is what a
        // template with corners called "front" and "rear" wants.
        if (value.isString()) {
            corner.token = value.toString();
            corner.label = corner.token;
        } else {
            const QJsonObject object = value.toObject();
            corner.token = object.value(QStringLiteral("token")).toString();
            corner.label = object.value(QStringLiteral("label")).toString(corner.token);
        }
        if (!corner.token.isEmpty()) templ.corners.push_back(corner);
    }

    const QJsonObject sides = root.value(QStringLiteral("sides")).toObject();
    templ.baseSideLabel = sides.value(QStringLiteral("base")).toString();
    templ.mirroredSideLabel = sides.value(QStringLiteral("mirrored")).toString();

    templ.mechanism = mechanismFromJson(root);

    for (const QJsonValue& value : root.value(QStringLiteral("parts")).toArray()) {
        const QJsonObject object = value.toObject();

        PartTemplate part;
        part.id = object.value(QStringLiteral("id")).toString();
        part.label = object.value(QStringLiteral("label")).toString(part.id);
        part.kind = partKindFromString(object.value(QStringLiteral("kind")).toString());
        part.optional = object.value(QStringLiteral("optional")).toBool(false);

        if (object.contains(QStringLiteral("chains"))) {
            for (const QJsonValue& entry : object.value(QStringLiteral("chains")).toArray()) {
                if (std::optional<ChainTemplate> chain = chainFromJson(entry.toObject()))
                    part.chains.push_back(std::move(*chain));
                else
                    result.warnings.append(
                        tr("Part \"%1\" has a chain with fewer than two points; it was skipped.")
                            .arg(part.id));
            }
        } else if (std::optional<ChainTemplate> chain = chainFromJson(object)) {
            part.chains.push_back(std::move(*chain));
        }

        if (part.id.isEmpty()) {
            result.warnings.append(tr("A part without an \"id\" was skipped."));
            continue;
        }
        if (part.chains.empty()) {
            result.warnings.append(
                tr("Part \"%1\" names no points to connect; it was skipped.").arg(part.id));
            continue;
        }
        templ.parts.push_back(std::move(part));
    }

    if (templ.parts.empty()) {
        result.error = tr("%1 describes no parts.").arg(label);
        return result;
    }

    result.templ = std::move(templ);
    return result;
}

LinkageTemplateLoadResult readLinkageTemplateFile(const QString& path)
{
    const QString label = QDir::toNativeSeparators(path);

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        LinkageTemplateLoadResult result;
        result.error = tr("Cannot read %1: %2").arg(label, file.errorString());
        return result;
    }
    return readLinkageTemplate(file.readAll(), label);
}

QByteArray writeLinkageTemplate(const LinkageTemplate& templ)
{
    QJsonObject root;
    root.insert(QStringLiteral("format"), QLatin1String(kFormatTag));
    root.insert(QStringLiteral("formatVersion"), kFormatVersion);
    root.insert(QStringLiteral("name"), templ.name);
    if (!templ.description.isEmpty())
        root.insert(QStringLiteral("description"), templ.description);
    if (!templ.notes.isEmpty()) {
        QJsonArray notes;
        for (const QString& note : templ.notes) notes.append(note);
        root.insert(QStringLiteral("notes"), notes);
    }

    QJsonArray corners;
    for (const CornerSpec& corner : templ.corners) {
        QJsonObject object;
        object.insert(QStringLiteral("token"), corner.token);
        object.insert(QStringLiteral("label"), corner.label);
        corners.append(object);
    }
    if (!corners.isEmpty()) root.insert(QStringLiteral("corners"), corners);

    QJsonObject sides;
    sides.insert(QStringLiteral("base"), templ.baseSideLabel);
    sides.insert(QStringLiteral("mirrored"), templ.mirroredSideLabel);
    root.insert(QStringLiteral("sides"), sides);

    QJsonArray parts;
    for (const PartTemplate& part : templ.parts) {
        QJsonObject object;
        object.insert(QStringLiteral("id"), part.id);
        object.insert(QStringLiteral("label"), part.label);
        object.insert(QStringLiteral("kind"), partKindToString(part.kind));
        if (part.optional) object.insert(QStringLiteral("optional"), true);

        // The shorthand back out again: a one-chain part is much easier to read
        // and to edit as "points" than as a list of one.
        if (part.chains.size() == 1) {
            const QJsonObject chain = chainToJson(part.chains.front());
            for (auto it = chain.begin(); it != chain.end(); ++it)
                object.insert(it.key(), it.value());
        } else {
            QJsonArray chains;
            for (const ChainTemplate& chain : part.chains) chains.append(chainToJson(chain));
            object.insert(QStringLiteral("chains"), chains);
        }
        parts.append(object);
    }
    root.insert(QStringLiteral("parts"), parts);

    const QJsonObject mechanism = mechanismToJson(templ.mechanism);
    if (!mechanism.isEmpty()) root.insert(QStringLiteral("mechanism"), mechanism);

    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

QByteArray builtinLinkageTemplateBytes()
{
    QFile file{ QString::fromLatin1(kBuiltinResource) };
    if (!file.open(QIODevice::ReadOnly)) {
        // The resource is compiled into the binary, so this is a build problem
        // rather than anything the user did.
        qWarning("Built-in linkage template is missing from the binary (%s).", kBuiltinResource);
        return {};
    }
    return file.readAll();
}

LinkageTemplate builtinLinkageTemplate()
{
    const LinkageTemplateLoadResult result = readLinkageTemplate(
        builtinLinkageTemplateBytes(), QStringLiteral("the built-in linkage template"));
    return result.ok() ? *result.templ : LinkageTemplate{};
}

MechanismTemplate builtinMechanismTemplate() { return builtinLinkageTemplate().mechanism; }

QString linkageTemplateRelativePath() { return QStringLiteral("linkage/template.json"); }

QString linkageTemplateFileFilter()
{
    return tr("Linkage templates (*.json);;All files (*)");
}

} // namespace suspkin
