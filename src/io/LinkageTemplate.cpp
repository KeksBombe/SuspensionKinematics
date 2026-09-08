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

QString linkageTemplateRelativePath() { return QStringLiteral("linkage/template.json"); }

QString linkageTemplateFileFilter()
{
    return tr("Linkage templates (*.json);;All files (*)");
}

} // namespace suspkin
