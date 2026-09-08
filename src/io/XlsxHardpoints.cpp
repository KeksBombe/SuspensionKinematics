#include "io/XlsxHardpoints.h"

#include "io/Zip.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QHash>
#include <QSaveFile>
#include <QSet>
#include <QXmlStreamReader>

#include <algorithm>
#include <cmath>
#include <map>

namespace suspkin {
namespace {

/// The relationship namespace an OPC package uses for r:id attributes.
constexpr QLatin1StringView kRelNamespace{
    "http://schemas.openxmlformats.org/officeDocument/2006/relationships"
};

QString tr(const char* text) { return QCoreApplication::translate("XlsxHardpoints", text); }

// ---------------------------------------------------------------------------
// OPC package plumbing
//
// An .xlsx is a ZIP whose parts find each other through relationship files
// rather than through fixed paths. Following them rather than hard-coding
// "xl/worksheets/sheet1.xml" is what makes the reader work on workbooks that
// were not written by the version of Excel that happens to be on this desk.
// ---------------------------------------------------------------------------

struct Relationship {
    QString id;
    QString type;
    QString target;
};

QString normalizePath(const QString& path)
{
    QStringList parts;
    for (const QString& part : path.split(u'/', Qt::SkipEmptyParts)) {
        if (part == u".") continue;
        if (part == u"..") {
            if (!parts.isEmpty()) parts.removeLast();
            continue;
        }
        parts.append(part);
    }
    return parts.join(u'/');
}

QString directoryOf(const QString& part)
{
    const qsizetype slash = part.lastIndexOf(u'/');
    return slash < 0 ? QString() : part.left(slash);
}

QString relsPartFor(const QString& part)
{
    const QString directory = directoryOf(part);
    const QString name = part.mid(directory.isEmpty() ? 0 : directory.size() + 1);
    return (directory.isEmpty() ? QString() : directory + u'/') + u"_rels/" + name + u".rels";
}

QString resolveTarget(const QString& sourcePart, const QString& target)
{
    if (target.startsWith(u'/')) return normalizePath(target);
    const QString directory = directoryOf(sourcePart);
    return normalizePath(directory.isEmpty() ? target : directory + u'/' + target);
}

std::vector<Relationship> parseRelationships(const QByteArray& xml)
{
    std::vector<Relationship> relationships;
    QXmlStreamReader reader(xml);
    while (!reader.atEnd()) {
        if (reader.readNext() != QXmlStreamReader::StartElement) continue;
        if (reader.name() != u"Relationship") continue;
        const QXmlStreamAttributes attributes = reader.attributes();
        // An external target is a URL to somewhere else entirely; there is no
        // part in this package to resolve it to.
        if (attributes.value(u"TargetMode") == u"External") continue;
        relationships.push_back({ attributes.value(u"Id").toString(),
                                  attributes.value(u"Type").toString(),
                                  attributes.value(u"Target").toString() });
    }
    return relationships;
}

const Relationship* findById(const std::vector<Relationship>& relationships, const QString& id)
{
    for (const Relationship& relationship : relationships)
        if (relationship.id == id) return &relationship;
    return nullptr;
}

const Relationship* findByType(const std::vector<Relationship>& relationships, QLatin1StringView tail)
{
    for (const Relationship& relationship : relationships)
        if (relationship.type.endsWith(tail)) return &relationship;
    return nullptr;
}

// ---------------------------------------------------------------------------
// Cells
// ---------------------------------------------------------------------------

struct Cell {
    QString text;
    bool numeric = false; ///< the cell is typed as a number, not as text
};

/// Rows, then columns, both 1-based and both kept in order so the table is read
/// top to bottom the way a person reads it.
using SheetRows = std::map<int, std::map<int, Cell>>;

bool parseCellRef(QStringView ref, int* row, int* column)
{
    int col = 0;
    qsizetype i = 0;
    for (; i < ref.size() && ref[i].isLetter(); ++i)
        col = col * 26 + (ref[i].toUpper().unicode() - 'A' + 1);
    if (col == 0 || i == ref.size()) return false;

    bool ok = false;
    const int parsed = ref.mid(i).toInt(&ok);
    if (!ok || parsed <= 0) return false;

    *row = parsed;
    *column = col;
    return true;
}

QString cellRef(int row, int column)
{
    QString letters;
    for (int value = column; value > 0; value = (value - 1) / 26)
        letters.prepend(QChar(u'A' + (value - 1) % 26));
    return letters + QString::number(row);
}

QStringList parseSharedStrings(const QByteArray& xml)
{
    QStringList strings;
    QXmlStreamReader reader(xml);
    QString current;
    bool inItem = false;
    while (!reader.atEnd()) {
        const QXmlStreamReader::TokenType token = reader.readNext();
        if (token == QXmlStreamReader::StartElement) {
            if (reader.name() == u"si") {
                inItem = true;
                current.clear();
            } else if (inItem && reader.name() == u"t") {
                // A string split into formatting runs arrives as several <t>
                // elements; the value is all of them joined.
                current += reader.readElementText();
            }
        } else if (token == QXmlStreamReader::EndElement && reader.name() == u"si") {
            strings.append(current);
            inItem = false;
        }
    }
    return strings;
}

SheetRows parseSheet(const QByteArray& xml, const QStringList& sharedStrings)
{
    SheetRows rows;
    QXmlStreamReader reader(xml);
    int row = 0;
    int column = 0;

    while (!reader.atEnd()) {
        if (reader.readNext() != QXmlStreamReader::StartElement) continue;

        if (reader.name() == u"row") {
            row = reader.attributes().value(u"r").toInt();
            column = 0;
            continue;
        }
        if (reader.name() != u"c") continue;

        const QXmlStreamAttributes attributes = reader.attributes();
        const QStringView type = attributes.value(u"t");
        const QStringView ref = attributes.value(u"r");
        if (!ref.isEmpty()) {
            if (!parseCellRef(ref, &row, &column)) continue;
        } else {
            // A cell may omit its reference, in which case it simply follows the
            // previous one.
            ++column;
        }

        QString raw;
        const bool inlineString = (type == u"inlineStr");
        while (!reader.atEnd()) {
            const QXmlStreamReader::TokenType token = reader.readNext();
            if (token == QXmlStreamReader::EndElement && reader.name() == u"c") break;
            if (token != QXmlStreamReader::StartElement) continue;
            if (reader.name() == u"v" || (inlineString && reader.name() == u"t"))
                raw += reader.readElementText();
        }

        Cell cell;
        if (type == u"s") {
            bool ok = false;
            const int index = raw.toInt(&ok);
            if (ok && index >= 0 && index < sharedStrings.size()) cell.text = sharedStrings.at(index);
        } else if (type.isEmpty() || type == u"n") {
            cell.text = raw;
            cell.numeric = !raw.isEmpty();
        } else {
            cell.text = raw; // inlineStr, str, b, e -- all read as text
        }

        if (!cell.text.isEmpty() && row > 0) rows[row][column] = cell;
    }
    return rows;
}

// ---------------------------------------------------------------------------
// Turning a Name/Value table into hardpoints
// ---------------------------------------------------------------------------

/// Split "F_LCA_O_x" into "F_LCA_O" and axis 0. The separator may be any of
/// `_`, `.` or `-`, because those are the three that show up in practice.
bool splitAxisSuffix(const QString& name, QString* base, int* axis)
{
    if (name.size() < 3) return false;

    const QChar separator = name.at(name.size() - 2);
    if (separator != u'_' && separator != u'.' && separator != u'-') return false;

    switch (name.at(name.size() - 1).toLower().unicode()) {
    case u'x': *axis = 0; break;
    case u'y': *axis = 1; break;
    case u'z': *axis = 2; break;
    default: return false;
    }
    *base = name.left(name.size() - 2);
    return !base->isEmpty();
}

struct Group {
    QString base;
    double value[3] = { 0.0, 0.0, 0.0 };
    QString ref[3];
    QString text[3];
    bool present[3] = { false, false, false };
};

struct Extraction {
    HardpointTable table;
    std::vector<std::array<QString, 3>> cells;
    std::vector<std::array<QString, 3>> text;
    QStringList warnings;
};

/// Find the header row, if there is one, and the two columns it names.
/// Returns the row the data starts on, or 0 when no header was recognised.
int findHeaderRow(const SheetRows& rows, int* nameColumn, int* valueColumn)
{
    for (const auto& [row, cells] : rows) {
        int name = -1;
        int value = -1;
        for (const auto& [column, cell] : cells) {
            if (cell.numeric) continue;
            const QString heading = cell.text.trimmed().toLower();
            if (name < 0 && heading == u"name") name = column;
            // "Wert" so a German-labelled sheet imports without being renamed.
            else if (value < 0 && (heading == u"value" || heading == u"wert")) value = column;
        }
        if (name >= 0 && value >= 0) {
            *nameColumn = name;
            *valueColumn = value;
            return row + 1;
        }
    }
    return 0;
}

/// Read the table assuming names are in @p nameColumn and numbers in
/// @p valueColumn, starting at @p firstDataRow.
Extraction extractWith(const SheetRows& rows, int nameColumn, int valueColumn, int firstDataRow)
{
    std::vector<Group> groups;
    QHash<QString, int> indexByBase;
    QStringList unnamedExamples;
    int skippedRows = 0;

    for (const auto& [row, cells] : rows) {
        if (row < firstDataRow) continue;

        const auto nameCell = cells.find(nameColumn);
        const auto valueCell = cells.find(valueColumn);
        if (nameCell == cells.end() || valueCell == cells.end()) continue; // blank or spacer row

        const QString name = nameCell->second.text.trimmed();
        if (name.isEmpty()) continue;

        bool ok = false;
        const double value = valueCell->second.text.trimmed().toDouble(&ok);
        if (!ok) continue; // a heading, a unit, a note -- not a coordinate

        QString base;
        int axis = 0;
        if (!splitAxisSuffix(name, &base, &axis)) {
            ++skippedRows;
            if (unnamedExamples.size() < 3) unnamedExamples.append(name);
            continue;
        }

        auto existing = indexByBase.find(base);
        if (existing == indexByBase.end()) {
            existing = indexByBase.insert(base, static_cast<int>(groups.size()));
            groups.push_back(Group{ base, {}, {}, {}, {} });
        }
        Group& group = groups[*existing];
        group.present[axis] = true;
        group.value[axis] = value;
        group.ref[axis] = cellRef(row, valueColumn);
        group.text[axis] = valueCell->second.text.trimmed();
    }

    Extraction result;
    QStringList incomplete;
    for (const Group& group : groups) {
        if (!group.present[0] || !group.present[1] || !group.present[2]) {
            incomplete.append(group.base);
            continue;
        }
        Hardpoint point;
        point.name = group.base;
        for (int axis = 0; axis < 3; ++axis) point.coord[axis] = group.value[axis];
        result.table.points.push_back(std::move(point));
        result.cells.push_back({ group.ref[0], group.ref[1], group.ref[2] });
        result.text.push_back({ group.text[0], group.text[1], group.text[2] });
    }

    if (skippedRows > 0) {
        result.warnings.append(tr("%1 row(s) had no _x/_y/_z suffix and were ignored (%2).")
                                   .arg(skippedRows)
                                   .arg(unnamedExamples.join(QStringLiteral(", "))));
    }
    if (!incomplete.isEmpty()) {
        result.warnings.append(tr("%1 point(s) were missing a coordinate and were skipped: %2.")
                                   .arg(incomplete.size())
                                   .arg(incomplete.join(QStringLiteral(", "))));
    }
    return result;
}

/// Columns that hold anything at all, most-populated first. Only a handful are
/// ever tried, so a sheet with a wide block of unrelated data to the right does
/// not turn the search below into busywork.
std::vector<int> candidateColumns(const SheetRows& rows)
{
    std::map<int, int> counts;
    for (const auto& [row, cells] : rows) {
        Q_UNUSED(row);
        for (const auto& [column, cell] : cells) {
            Q_UNUSED(cell);
            ++counts[column];
        }
    }

    std::vector<int> columns;
    columns.reserve(counts.size());
    for (const auto& [column, count] : counts) {
        Q_UNUSED(count);
        columns.push_back(column);
    }
    std::stable_sort(columns.begin(), columns.end(),
                     [&counts](int a, int b) { return counts[a] > counts[b]; });

    constexpr std::size_t kMaxColumns = 8;
    if (columns.size() > kMaxColumns) columns.resize(kMaxColumns);
    return columns;
}

Extraction extractHardpoints(const SheetRows& rows)
{
    // A header says outright which columns to read, so it is tried first and
    // kept if it works at all.
    int nameColumn = 0;
    int valueColumn = 0;
    if (const int firstDataRow = findHeaderRow(rows, &nameColumn, &valueColumn)) {
        Extraction fromHeader = extractWith(rows, nameColumn, valueColumn, firstDataRow);
        if (!fromHeader.table.isEmpty()) return fromHeader;
    }

    // No usable header. Rather than assuming the table starts in column A, try
    // the populated columns against each other and keep whichever pairing
    // actually yields hardpoints -- which is the only real evidence available.
    const std::vector<int> columns = candidateColumns(rows);
    const int firstRow = rows.empty() ? 1 : rows.begin()->first;

    Extraction best;
    for (const int name : columns) {
        for (const int value : columns) {
            if (name == value) continue;
            Extraction candidate = extractWith(rows, name, value, firstRow);
            if (candidate.table.size() > best.table.size()) best = std::move(candidate);
        }
    }
    return best;
}

// ---------------------------------------------------------------------------
// Writing values back into a worksheet part
// ---------------------------------------------------------------------------

/// The shortest decimal text that still reads back as exactly @p value. Excel
/// writes full 17-digit precision, and echoing that back for an edited number
/// would fill the cell with digits the user never typed.
QString numberToXml(double value)
{
    for (int precision = 1; precision <= 17; ++precision) {
        const QString candidate = QString::number(value, 'g', precision);
        if (candidate.toDouble() == value) return candidate;
    }
    return QString::number(value, 'g', 17);
}

/// XML's own definition of whitespace, spelled out rather than going through
/// QChar so a raw byte cannot take a locale-aware path.
bool isXmlSpace(char ch)
{
    return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';
}

struct Attribute {
    QByteArray name;
    qsizetype begin = 0; ///< first byte of the whole ` name="value"` run
    qsizetype end = 0;   ///< one past its last byte
    QByteArray value;
};

std::vector<Attribute> parseAttributes(const QByteArray& text)
{
    std::vector<Attribute> attributes;
    qsizetype i = 0;
    while (i < text.size()) {
        while (i < text.size() && isXmlSpace(text[i])) ++i;
        if (i >= text.size()) break;

        const qsizetype begin = i;
        const qsizetype nameBegin = i;
        while (i < text.size() && text[i] != '=' && !isXmlSpace(text[i])) ++i;
        const QByteArray name = text.mid(nameBegin, i - nameBegin);

        while (i < text.size() && isXmlSpace(text[i])) ++i;
        if (i >= text.size() || text[i] != '=') continue; // valueless attribute; skip it
        ++i;
        while (i < text.size() && isXmlSpace(text[i])) ++i;
        if (i >= text.size()) break;

        const char quote = text[i];
        if (quote != '"' && quote != '\'') break; // malformed; stop parsing here
        ++i;
        const qsizetype valueBegin = i;
        while (i < text.size() && text[i] != quote) ++i;
        const QByteArray value = text.mid(valueBegin, i - valueBegin);
        if (i < text.size()) ++i; // the closing quote

        attributes.push_back({ name, begin, i, value });
    }
    return attributes;
}

/// Rewrite the value of specific cells, leaving every other byte of the sheet
/// exactly as it was.
///
/// This is a splice rather than a parse-and-reserialise on purpose: a worksheet
/// carries conditional formatting, data validation, merged ranges and markup
/// from Excel extensions that a round trip through a generic XML writer would
/// reorder or drop. Only the cells being edited are touched.
std::optional<QByteArray> patchCellValues(const QByteArray& xml,
                                          const QHash<QString, QString>& values, QString* error)
{
    QByteArray out;
    out.reserve(xml.size() + 32 * values.size());

    QSet<QString> written;
    qsizetype copied = 0;
    qsizetype i = 0;

    // A cell may leave out its reference and simply follow the previous one, so
    // the row and column are tracked here exactly as the reader tracks them.
    // Otherwise such a cell could be read but never written back.
    int row = 0;
    int column = 0;

    while ((i = xml.indexOf('<', i)) >= 0) {
        const bool isRow = (xml.mid(i, 5) == "<row " || xml.mid(i, 5) == "<row>");
        const bool isCell = !isRow && xml.mid(i, 2) == "<c"
            && [&] {
                   // "<cols>" and "<cellWatch>" also start with "<c".
                   const char after = (i + 2 < xml.size()) ? xml[i + 2] : '\0';
                   return after == ' ' || after == '\t' || after == '\r' || after == '\n'
                       || after == '>' || after == '/';
               }();
        if (!isRow && !isCell) {
            ++i;
            continue;
        }

        // Find the end of the start tag, ignoring '>' inside attribute values.
        qsizetype cursor = i + (isRow ? 4 : 2);
        char quote = '\0';
        for (; cursor < xml.size(); ++cursor) {
            const char ch = xml[cursor];
            if (quote != '\0') {
                if (ch == quote) quote = '\0';
            } else if (ch == '"' || ch == '\'') {
                quote = ch;
            } else if (ch == '>') {
                break;
            }
        }
        if (cursor >= xml.size()) break; // truncated markup; leave the tail alone

        const bool selfClosing = xml[cursor - 1] == '/';
        const qsizetype attributesBegin = i + (isRow ? 4 : 2);
        const qsizetype attributesEnd = selfClosing ? cursor - 1 : cursor;
        const QByteArray attributeText = xml.mid(attributesBegin, attributesEnd - attributesBegin);
        const std::vector<Attribute> attributes = parseAttributes(attributeText);

        QByteArray reference;
        for (const Attribute& attribute : attributes)
            if (attribute.name == "r") reference = attribute.value;

        if (isRow) {
            row = reference.toInt();
            column = 0;
            i = cursor + 1;
            continue;
        }

        QString ref;
        if (reference.isEmpty()) {
            ++column;
            ref = cellRef(row, column);
        } else {
            ref = QString::fromLatin1(reference);
            parseCellRef(ref, &row, &column);
        }

        const auto replacement = values.find(ref);
        if (replacement == values.end()) {
            i = cursor + 1;
            continue;
        }

        qsizetype end = cursor + 1;
        if (!selfClosing) {
            const qsizetype close = xml.indexOf("</c>", cursor + 1);
            if (close < 0) {
                if (error) *error = tr("The worksheet XML is malformed near cell %1.").arg(ref);
                return std::nullopt;
            }
            end = close + 4;
        }

        // Keep every attribute except the type: the cell now holds a number, and
        // a stale t="s" would send Excel to the shared-string table instead.
        // Dropping the old body also drops any formula, which would otherwise
        // recompute over the top of the value being written.
        QByteArray keptAttributes;
        if (reference.isEmpty()) {
            // Spelling the reference out is always legal, and it keeps the cell
            // addressable if anything later shifts around it.
            keptAttributes.append(" r=\"").append(ref.toLatin1()).append('"');
        }
        for (const Attribute& attribute : attributes) {
            if (attribute.name == "t") continue;
            // The recorded span starts at the attribute name, so the separator
            // has to be put back or the tag reassembles as <cr="B2"s="1">.
            keptAttributes.append(' ');
            keptAttributes.append(attributeText.mid(attribute.begin, attribute.end - attribute.begin));
        }

        out.append(xml.constData() + copied, i - copied);
        out.append("<c");
        out.append(keptAttributes);
        out.append("><v>");
        out.append(replacement->toUtf8());
        out.append("</v></c>");

        written.insert(ref);
        copied = end;
        i = end;
    }
    out.append(xml.constData() + copied, xml.size() - copied);

    if (written.size() != values.size()) {
        for (auto it = values.begin(); it != values.end(); ++it) {
            if (!written.contains(it.key())) {
                if (error)
                    *error = tr("Cell %1 is no longer in the worksheet.").arg(it.key());
                return std::nullopt;
            }
        }
    }
    return out;
}

/// Ask Excel to recalculate on open, so formulas elsewhere in the workbook that
/// read these cells do not show stale cached results. Only ever edits a calcPr
/// element that is already there -- inserting one would mean getting the
/// workbook schema's element order right, which is not worth the risk.
QByteArray withFullCalcOnLoad(const QByteArray& workbookXml)
{
    const qsizetype begin = workbookXml.indexOf("<calcPr");
    if (begin < 0) return workbookXml;
    const qsizetype end = workbookXml.indexOf('>', begin);
    if (end < 0) return workbookXml;

    QByteArray element = workbookXml.mid(begin, end - begin + 1);
    if (element.contains("fullCalcOnLoad")) return workbookXml;

    const qsizetype insertAt = element.endsWith("/>") ? element.size() - 2 : element.size() - 1;
    element.insert(insertAt, " fullCalcOnLoad=\"1\"");

    QByteArray out = workbookXml;
    out.replace(begin, end - begin + 1, element);
    return out;
}

} // namespace

HardpointLoadResult readHardpointsXlsx(const QString& path)
{
    QElapsedTimer timer;
    timer.start();

    HardpointLoadResult result;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        result.error = tr("Cannot open the file: %1").arg(file.errorString());
        return result;
    }
    QByteArray bytes = file.readAll();
    file.close();

    QString error;
    const std::optional<zip::Archive> archive = zip::Archive::open(bytes, &error);
    if (!archive) {
        result.error = tr("This is not a readable .xlsx workbook. %1").arg(error);
        return result;
    }

    // Package root -> the workbook part.
    QString workbookPart = QStringLiteral("xl/workbook.xml");
    if (const std::optional<QByteArray> rootRels = archive->extract(QStringLiteral("_rels/.rels"))) {
        const std::vector<Relationship> relationships = parseRelationships(*rootRels);
        if (const Relationship* main = findByType(relationships, QLatin1StringView("/officeDocument")))
            workbookPart = resolveTarget(QString(), main->target);
    }

    const std::optional<QByteArray> workbookXml = archive->extract(workbookPart, &error);
    if (!workbookXml) {
        result.error = tr("The workbook part is missing or unreadable. %1").arg(error);
        return result;
    }

    std::vector<Relationship> workbookRels;
    if (const std::optional<QByteArray> rels = archive->extract(relsPartFor(workbookPart)))
        workbookRels = parseRelationships(*rels);

    QStringList sharedStrings;
    {
        QString sharedPart;
        if (const Relationship* relationship =
                findByType(workbookRels, QLatin1StringView("/sharedStrings")))
            sharedPart = resolveTarget(workbookPart, relationship->target);
        else
            sharedPart = QStringLiteral("xl/sharedStrings.xml");
        if (const std::optional<QByteArray> shared = archive->extract(sharedPart))
            sharedStrings = parseSharedStrings(*shared);
    }

    // Sheets, in the order the workbook lists them.
    struct SheetRef {
        QString name;
        QString part;
    };
    std::vector<SheetRef> sheets;
    {
        QXmlStreamReader reader(*workbookXml);
        while (!reader.atEnd()) {
            if (reader.readNext() != QXmlStreamReader::StartElement) continue;
            if (reader.name() != u"sheet") continue;
            const QXmlStreamAttributes attributes = reader.attributes();
            const QString id = attributes.value(kRelNamespace, u"id").toString();
            const Relationship* relationship = findById(workbookRels, id);
            if (!relationship) continue;
            sheets.push_back({ attributes.value(u"name").toString(),
                               resolveTarget(workbookPart, relationship->target) });
        }
    }
    if (sheets.empty()) {
        result.error = tr("The workbook has no worksheets.");
        return result;
    }

    // The first sheet that actually yields points wins, so a workbook with a
    // cover sheet in front of the table still imports.
    for (const SheetRef& sheet : sheets) {
        const std::optional<QByteArray> sheetXml = archive->extract(sheet.part);
        if (!sheetXml) continue;

        Extraction extraction = extractHardpoints(parseSheet(*sheetXml, sharedStrings));
        if (extraction.table.isEmpty()) continue;

        result.table = std::move(extraction.table);
        result.warnings = std::move(extraction.warnings);
        result.source.workbook = std::move(bytes);
        result.source.workbookPart = workbookPart;
        result.source.sheetPart = sheet.part;
        result.source.sheetName = sheet.name;
        result.source.cells = std::move(extraction.cells);
        result.source.text = std::move(extraction.text);
        result.elapsedMs = timer.elapsed();
        return result;
    }

    result.error = tr("No hardpoints found. The sheet needs a Name column and a Value column, "
                      "with one row per coordinate -- for example F_LCA_O_x, F_LCA_O_y and "
                      "F_LCA_O_z for the point F_LCA_O.");
    return result;
}

QString writeHardpointsXlsx(const QString& path, const HardpointTable& table,
                            const XlsxHardpointSource& source)
{
    if (!source.isValid())
        return tr("There is no source workbook to write into.");
    if (table.points.size() != source.cells.size() || table.points.size() != source.text.size())
        return tr("The hardpoints no longer match the workbook they were read from.");

    QString error;
    const std::optional<zip::Archive> archive = zip::Archive::open(source.workbook, &error);
    if (!archive) return tr("The source workbook can no longer be read. %1").arg(error);

    QHash<QString, QString> values;
    values.reserve(static_cast<qsizetype>(table.points.size()) * 3);
    for (std::size_t i = 0; i < table.points.size(); ++i) {
        for (int axis = 0; axis < 3; ++axis) {
            const QString& ref = source.cells[i][axis];
            if (ref.isEmpty()) continue;
            const QString& original = source.text[i][axis];
            // An untouched coordinate is written back with the very text the
            // workbook already had, so saving a file nobody edited leaves its
            // cells byte for byte as they were.
            const double value = table.points[i].coord[axis];
            if (!std::isfinite(value)) {
                return tr("\"%1\" has a coordinate that is not a number, so nothing was written.")
                    .arg(table.points[i].name);
            }
            bool ok = false;
            const double previous = original.toDouble(&ok);
            values.insert(ref, (ok && previous == value) ? original : numberToXml(value));
        }
    }

    const std::optional<QByteArray> sheetXml = archive->extract(source.sheetPart, &error);
    if (!sheetXml) return tr("The worksheet could not be read back. %1").arg(error);

    const std::optional<QByteArray> patched = patchCellValues(*sheetXml, values, &error);
    if (!patched) return error;

    QHash<QString, QByteArray> replacements;
    replacements.insert(source.sheetPart, *patched);
    if (const std::optional<QByteArray> workbookXml = archive->extract(source.workbookPart)) {
        QByteArray updated = withFullCalcOnLoad(*workbookXml);
        if (updated != *workbookXml) replacements.insert(source.workbookPart, std::move(updated));
    }

    const QByteArray rebuilt = zip::rebuild(*archive, replacements, &error);
    if (rebuilt.isEmpty()) return tr("The workbook could not be rebuilt. %1").arg(error);

    // QSaveFile writes to a temporary and renames, so a failure part way through
    // cannot leave the user with a half-written workbook where their data was.
    QSaveFile out(path);
    if (!out.open(QIODevice::WriteOnly))
        return tr("Cannot write to %1: %2").arg(path, out.errorString());
    if (out.write(rebuilt) != rebuilt.size() || !out.commit())
        return tr("Cannot write to %1: %2").arg(path, out.errorString());

    return QString();
}

QString hardpointFileFilter()
{
    return QCoreApplication::translate("XlsxHardpoints",
                                       "Excel workbooks (*.xlsx *.XLSX);;All files (*)");
}

} // namespace suspkin
