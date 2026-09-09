#include "io/LinkageTemplate.h"

#include <QCoreApplication>
#include <QDir>
#include <QHash>
#include <cctype>
#include <utility>
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
    mechanism.wheelAxis = roleName(object, "upright", "wheelAxis");
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
    if (!mechanism.wheelAxis.isEmpty())
        upright.insert(QStringLiteral("wheelAxis"), mechanism.wheelAxis);
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

namespace {

LinkageTemplateLoadResult readTemplate(const QByteArray& bytes, const QString& label,
                                       bool allowMechanismFallback);

/// The roles the shipped template gives a corner, read without the fallback --
/// this is the thing the fallback falls back *to*, so it must never be able to
/// ask for itself.
MechanismTemplate builtinMechanism()
{
    const LinkageTemplateLoadResult result =
        readTemplate(builtinLinkageTemplateBytes(), QStringLiteral("the built-in linkage template"),
                     false);
    return result.ok() ? result.templ->mechanism : MechanismTemplate{};
}

LinkageTemplateLoadResult readTemplate(const QByteArray& bytes, const QString& label,
                                       bool allowMechanismFallback)
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
            // Which hardpoint this axle's rack drives, if it has one. Only the
            // long form can say it: a corner written as a bare string is a
            // token and nothing else, and an axle nobody has said anything
            // about is not steered -- unless no corner says anything at all,
            // which is LinkageTemplate::steeringDeclared()'s job to notice.
            corner.steeringStated = object.contains(QStringLiteral("steering"));
            corner.steeringRack = object.value(QStringLiteral("steering")).toString();
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

    // A template written before the solver existed has parts and no mechanism.
    // It still draws, but nothing else can do anything with it: the corner
    // cannot be solved, and no hardpoint can be told what it is for. The
    // built-in block is the right guess -- such a file is almost certainly a
    // copy of the built-in template -- and a guess that says so beats a table
    // of points that all read "unassigned". A template naming points that are
    // called something else entirely resolves none of it, which lands back
    // exactly where it started.
    if (allowMechanismFallback && !root.contains(QStringLiteral("mechanism"))) {
        templ.mechanism = builtinMechanism();
        templ.mechanismAssumed = !templ.mechanism.isEmpty();
    }

    result.templ = std::move(templ);
    return result;
}

} // namespace

LinkageTemplateLoadResult readLinkageTemplate(const QByteArray& bytes, const QString& label)
{
    return readTemplate(bytes, label, true);
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
        // An empty role is written out when the corner said so on purpose: "this
        // axle is not steered" is an answer, and losing it would read as a
        // template that never heard the question.
        if (!corner.steeringRack.isEmpty() || corner.steeringStated)
            object.insert(QStringLiteral("steering"), corner.steeringRack);
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
    const LinkageTemplateLoadResult result =
        readTemplate(builtinLinkageTemplateBytes(),
                     QStringLiteral("the built-in linkage template"), false);
    return result.ok() ? *result.templ : LinkageTemplate{};
}

MechanismTemplate builtinMechanismTemplate() { return builtinMechanism(); }

namespace {

/// The answer for one corner: what its steering role should say, and whether it
/// should say anything at all.
struct WantedSteering {
    QString rack;
    bool stated = false;

    bool wanted() const { return stated || !rack.isEmpty(); }
};

/// The index just past the value that starts at @p open, which is a '{', '[' or
/// '"'. Strings and their escapes are respected, which is the whole difficulty:
/// a brace inside a note would otherwise end the object early.
int endOfValue(const QByteArray& text, int open)
{
    const char first = text.at(open);
    if (first == '"') {
        for (int i = open + 1; i < text.size(); ++i) {
            if (text.at(i) == '\\') {
                ++i;
                continue;
            }
            if (text.at(i) == '"') return i + 1;
        }
        return -1;
    }
    if (first != '{' && first != '[') return -1;

    const char close = (first == '{') ? '}' : ']';
    int depth = 0;
    for (int i = open; i < text.size(); ++i) {
        const char c = text.at(i);
        if (c == '"') {
            const int after = endOfValue(text, i);
            if (after < 0) return -1;
            i = after - 1;
            continue;
        }
        if (c == first) ++depth;
        else if (c == close && --depth == 0) return i + 1;
    }
    return -1;
}

/// Where the "corners" array starts and ends in @p text, or {-1, -1}.
std::pair<int, int> findCornersArray(const QByteArray& text)
{
    const QByteArray key = QByteArrayLiteral("\"corners\"");
    for (int at = text.indexOf(key); at >= 0; at = text.indexOf(key, at + 1)) {
        int i = at + key.size();
        while (i < text.size() && std::isspace(static_cast<unsigned char>(text.at(i)))) ++i;
        if (i >= text.size() || text.at(i) != ':') continue; // a note mentioning the word
        ++i;
        while (i < text.size() && std::isspace(static_cast<unsigned char>(text.at(i)))) ++i;
        if (i >= text.size() || text.at(i) != '[') continue;
        const int end = endOfValue(text, i);
        if (end < 0) return { -1, -1 };
        return { i, end };
    }
    return { -1, -1 };
}

/// The spans of the array's elements, in the order they are written.
std::vector<std::pair<int, int>> elementSpans(const QByteArray& text, int arrayStart, int arrayEnd)
{
    std::vector<std::pair<int, int>> spans;
    for (int i = arrayStart + 1; i < arrayEnd - 1;) {
        const char c = text.at(i);
        if (std::isspace(static_cast<unsigned char>(c)) || c == ',') {
            ++i;
            continue;
        }
        const int end = endOfValue(text, i);
        if (end < 0) return {};
        spans.push_back({ i, end });
        i = end;
    }
    return spans;
}

/// The whitespace at the start of the line @p at sits on, for writing a new
/// entry that lines up with the ones around it.
QByteArray indentAt(const QByteArray& text, int at)
{
    int lineStart = at;
    while (lineStart > 0 && text.at(lineStart - 1) != '\n') --lineStart;
    int i = lineStart;
    while (i < text.size() && (text.at(i) == ' ' || text.at(i) == '\t')) ++i;
    return text.mid(lineStart, i - lineStart);
}

/// One corner object with its steering entry set, removed, or left alone --
/// edited as text, so everything else in it stays exactly as the user wrote it.
QByteArray withSteering(const QByteArray& object, const WantedSteering& answer)
{
    const QByteArray key = QByteArrayLiteral("\"steering\"");
    const int at = object.indexOf(key);

    if (at >= 0) {
        // Replace the value in place, or take the whole entry out.
        int valueStart = at + key.size();
        while (valueStart < object.size()
               && (std::isspace(static_cast<unsigned char>(object.at(valueStart)))
                   || object.at(valueStart) == ':'))
            ++valueStart;
        const int valueEnd = endOfValue(object, valueStart);
        if (valueEnd < 0) return {};

        QByteArray out = object;
        if (answer.wanted()) {
            out.replace(valueStart, valueEnd - valueStart,
                        QJsonValue(answer.rack).toString().toUtf8().prepend('"').append('"'));
            return out;
        }

        // Out it comes, with the comma that joined it to its neighbour and any
        // blank line it leaves behind.
        int from = at;
        while (from > 0 && std::isspace(static_cast<unsigned char>(out.at(from - 1)))) --from;
        int to = valueEnd;
        if (from > 0 && out.at(from - 1) == ',') {
            --from; // it was not the first entry: its own comma goes with it
        } else {
            while (to < out.size() && std::isspace(static_cast<unsigned char>(out.at(to)))) ++to;
            if (to < out.size() && out.at(to) == ',') ++to;
        }
        out.remove(from, to - from);
        return out;
    }

    if (!answer.wanted()) return object;

    // No entry yet: written in just before the closing brace, in the shape the
    // object is already written in.
    const int close = object.lastIndexOf('}');
    if (close < 0) return {};
    int insertAt = close;
    while (insertAt > 0 && std::isspace(static_cast<unsigned char>(object.at(insertAt - 1))))
        --insertAt;

    QByteArray entry = QByteArrayLiteral(", \"steering\": \"") + answer.rack.toUtf8() + '"';
    if (object.contains('\n')) {
        // A multi-line object gets a line of its own, lined up with the entry
        // above it.
        entry = QByteArrayLiteral(",\n") + indentAt(object, insertAt)
                + QByteArrayLiteral("\"steering\": \"") + answer.rack.toUtf8() + '"';
    }

    QByteArray out = object;
    out.insert(insertAt, entry);
    return out;
}

/// @p bytes with the steering entries edited as text. Empty when the file is
/// shaped in a way this cannot follow, which is the caller's signal to fall back
/// on rewriting it.
QByteArray spliceSteering(const QByteArray& bytes, const QJsonArray& parsed,
                          const QHash<QString, WantedSteering>& wanted)
{
    const auto [arrayStart, arrayEnd] = findCornersArray(bytes);
    if (arrayStart < 0) return {};

    const std::vector<std::pair<int, int>> spans = elementSpans(bytes, arrayStart, arrayEnd);
    if (spans.size() != static_cast<std::size_t>(parsed.size())) return {};

    QByteArray out = bytes;
    // Back to front, so an edit never moves the span of one not yet made.
    for (int i = static_cast<int>(spans.size()) - 1; i >= 0; --i) {
        const QJsonValue value = parsed.at(i);
        const QString token = value.isString() ? value.toString()
                                               : value.toObject()
                                                     .value(QStringLiteral("token"))
                                                     .toString();
        const auto it = wanted.constFind(token);
        if (it == wanted.constEnd()) continue; // a corner nobody said anything about

        const auto [from, to] = spans[static_cast<std::size_t>(i)];
        const QByteArray element = bytes.mid(from, to - from);
        QByteArray replacement;
        if (value.isString()) {
            if (!it->wanted()) continue; // the shorthand had nothing to lose
            // The shorthand cannot carry a role, so it grows into the long form.
            replacement = QByteArrayLiteral("{ \"token\": \"") + token.toUtf8()
                          + QByteArrayLiteral("\", \"label\": \"") + token.toUtf8()
                          + QByteArrayLiteral("\", \"steering\": \"") + it->rack.toUtf8()
                          + QByteArrayLiteral("\" }");
        } else {
            replacement = withSteering(element, *it);
            if (replacement.isEmpty()) return {};
        }
        out.replace(from, to - from, replacement);
    }
    return out;
}

} // namespace

QByteArray setTemplateSteering(const QByteArray& bytes, const std::vector<CornerSpec>& corners,
                               QString* error)
{
    QJsonParseError parseError;
    QJsonDocument document = QJsonDocument::fromJson(bytes, &parseError);
    if (document.isNull() || !document.isObject()) {
        if (error)
            *error = tr("The template is not readable JSON: %1 (at offset %2)")
                         .arg(parseError.errorString())
                         .arg(parseError.offset);
        return {};
    }

    // By token, because the order in the file is the user's and a corner they
    // added by hand should keep its place.
    QHash<QString, WantedSteering> wanted;
    for (const CornerSpec& corner : corners)
        wanted.insert(corner.token, WantedSteering{ corner.steeringRack, corner.steeringStated });

    QJsonObject root = document.object();
    const QJsonArray parsed = root.value(QStringLiteral("corners")).toArray();
    QJsonArray out;
    for (const QJsonValue& value : parsed) {
        QJsonObject object;
        if (value.isString()) {
            // The shorthand cannot carry a steering role, so it grows into the
            // long form -- and only when it has one to carry.
            const QString token = value.toString();
            const WantedSteering answer = wanted.value(token);
            if (!answer.wanted()) {
                out.append(value);
                continue;
            }
            object.insert(QStringLiteral("token"), token);
            object.insert(QStringLiteral("label"), token);
        } else {
            object = value.toObject();
        }

        const QString token = object.value(QStringLiteral("token")).toString();
        const auto it = wanted.constFind(token);
        // A corner nobody said anything about keeps whatever it had.
        if (it != wanted.constEnd()) {
            if (it->wanted())
                object.insert(QStringLiteral("steering"), it->rack);
            else
                object.remove(QStringLiteral("steering"));
        }
        out.append(object);
    }

    root.insert(QStringLiteral("corners"), out);
    document.setObject(root);
    // Rewriting the file is the fallback, not the plan: QJsonDocument sorts every
    // key alphabetically, which would shuffle a template somebody wrote by hand
    // and lose the order their notes read in.
    const QByteArray rewritten = document.toJson(QJsonDocument::Indented);

    // The plan: edit the steering entries as text and leave every other byte
    // where it was. What makes that safe is checking it afterwards -- the spliced
    // file has to parse, and has to say exactly what the rewritten one says.
    const QByteArray spliced = spliceSteering(bytes, parsed, wanted);
    if (!spliced.isEmpty()) {
        const QJsonDocument check = QJsonDocument::fromJson(spliced);
        if (!check.isNull() && check == document) return spliced;
    }
    return rewritten;
}

QString linkageTemplateRelativePath() { return QStringLiteral("linkage/template.json"); }

QString linkageTemplateFileFilter()
{
    return tr("Linkage templates (*.json);;All files (*)");
}

} // namespace suspkin
