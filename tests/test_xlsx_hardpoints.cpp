#include "io/XlsxHardpoints.h"
#include "io/Zip.h"

#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using namespace suspkin;

namespace {

QString dataPath(const char* name)
{
    return QStringLiteral(SUSPKIN_TEST_DATA_DIR) + QLatin1Char('/') + QLatin1String(name);
}

const Hardpoint* findPoint(const HardpointTable& table, const char* name)
{
    for (const Hardpoint& point : table.points)
        if (point.name == QLatin1String(name)) return &point;
    return nullptr;
}

/// The raw bytes of one member of a workbook on disk, for assertions about what
/// saving did and did not touch.
QByteArray partOf(const QString& path, const char* member)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    const std::optional<zip::Archive> archive = zip::Archive::open(file.readAll());
    if (!archive) return {};
    return archive->extract(QLatin1String(member)).value_or(QByteArray());
}

} // namespace

class TestXlsxHardpoints : public QObject {
    Q_OBJECT

private slots:
    void readsNamedTriples();
    void warnsAboutRowsItCannotUse();
    void findsTheTableOnALaterSheet();
    void findsTheTableWithoutAHeaderOrConventionalColumns();
    void readsAndWritesCellsThatCarryNoReference();
    void reportsAFileThatIsNotAWorkbook();

    void saveWithoutEditsLeavesTheValuesAlone();
    void saveWritesEditedCoordinates();
    void saveKeepsPartsItWasNotAskedToChange();
    void saveAsLeavesTheOriginalUntouched();
    void roundTripsThroughItsOwnOutput();
    void savingAddsRowsForPointsTheWorkbookDoesNotHave();
    void reReadingPicksUpAppendedRowsSoTheyAreNotAddedTwice();

    void aDeletedPointsRowKeepsItsNeighbouringColumns();
    void aRenamedPointLeavesNoTraceOfItsOldName();
    void appendingGoesBelowARowThatHoldsOnlyFormatting();
    void theBlankWorkbookIsFilledNeverReadFirst();
};

void TestXlsxHardpoints::readsNamedTriples()
{
    const HardpointLoadResult result = readHardpointsXlsx(dataPath("hardpoints.xlsx"));
    QVERIFY2(result.ok(), qPrintable(result.error));
    QCOMPARE(result.source.sheetName, QStringLiteral("Geometry"));

    // Three complete points; R_Damper_I has no z and F_UCA_O does, so the count
    // is not simply "every base name that appeared".
    QCOMPARE(result.table->size(), std::size_t(4));
    QCOMPARE(result.table->points.front().name, QStringLiteral("F_LCA_O"));

    const Hardpoint* point = findPoint(*result.table, "F_LCA_O");
    QVERIFY(point);
    QCOMPARE(point->coord[0], -544.26);
    QCOMPARE(point->coord[1], 554.41200000000003);
    QCOMPARE(point->coord[2], 138.408);

    // Full double precision survives the read; a float would lose the last digits.
    const Hardpoint* damper = findPoint(*result.table, "F_Damper_I");
    QVERIFY(damper);
    QCOMPARE(damper->coord[0], -343.80632000000003);

    // The cells each coordinate came from are what makes writing back possible.
    QCOMPARE(result.source.rows.front().ref[0], QStringLiteral("B2"));
    QCOMPARE(result.source.rows.front().ref[2], QStringLiteral("B4"));
}

void TestXlsxHardpoints::warnsAboutRowsItCannotUse()
{
    const HardpointLoadResult result = readHardpointsXlsx(dataPath("hardpoints.xlsx"));
    QVERIFY(result.ok());
    QCOMPARE(result.warnings.size(), 2);

    // "Wheelbase" is a number with a name that is not a coordinate.
    QVERIFY(result.warnings.join(QLatin1Char('\n')).contains(QStringLiteral("Wheelbase")));
    // R_Damper_I has x and y in the fixture but no z.
    QVERIFY(result.warnings.join(QLatin1Char('\n')).contains(QStringLiteral("R_Damper_I")));
    QVERIFY(!findPoint(*result.table, "R_Damper_I"));
}

void TestXlsxHardpoints::findsTheTableOnALaterSheet()
{
    const HardpointLoadResult result = readHardpointsXlsx(dataPath("second_sheet.xlsx"));
    QVERIFY2(result.ok(), qPrintable(result.error));
    QCOMPARE(result.source.sheetName, QStringLiteral("Points"));
    QCOMPARE(result.table->size(), std::size_t(4));
}

void TestXlsxHardpoints::findsTheTableWithoutAHeaderOrConventionalColumns()
{
    // Inline strings, no Name/Value header, and the table in columns C and D.
    const HardpointLoadResult result = readHardpointsXlsx(dataPath("inline_strings.xlsx"));
    QVERIFY2(result.ok(), qPrintable(result.error));
    QCOMPARE(result.table->size(), std::size_t(4));

    const Hardpoint* point = findPoint(*result.table, "F_UCA_O");
    QVERIFY(point);
    QCOMPARE(point->coord[2], 318.38799999999998);
    QCOMPARE(result.source.rows.front().ref[0], QStringLiteral("D2"));
}

void TestXlsxHardpoints::readsAndWritesCellsThatCarryNoReference()
{
    // A cell is allowed to omit its r attribute and simply follow the previous
    // one. Reading such a sheet is only half the job: the write-back has to
    // arrive at the same addresses, or saving fails on a file that imported fine.
    const HardpointLoadResult result = readHardpointsXlsx(dataPath("no_cell_refs.xlsx"));
    QVERIFY2(result.ok(), qPrintable(result.error));
    QCOMPARE(result.table->size(), std::size_t(4));
    QCOMPARE(result.source.rows.front().ref[0], QStringLiteral("B2"));

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString out = directory.filePath(QStringLiteral("positional.xlsx"));

    HardpointTable table = *result.table;
    table.points[0].coord[0] = -1.5;
    QCOMPARE(writeHardpointsXlsx(out, table, result.source), QString());

    const HardpointLoadResult reread = readHardpointsXlsx(out);
    QVERIFY2(reread.ok(), qPrintable(reread.error));
    QCOMPARE(findPoint(*reread.table, "F_LCA_O")->coord[0], -1.5);
    QCOMPARE(findPoint(*reread.table, "F_UCA_O")->coord[1], 548.23699999999997);
}

void TestXlsxHardpoints::reportsAFileThatIsNotAWorkbook()
{
    const HardpointLoadResult result = readHardpointsXlsx(dataPath("not_a_zip.xlsx"));
    QVERIFY(!result.ok());
    QVERIFY(!result.error.isEmpty());

    const HardpointLoadResult missing = readHardpointsXlsx(dataPath("no_such_file.xlsx"));
    QVERIFY(!missing.ok());
    QVERIFY(!missing.error.isEmpty());
}

void TestXlsxHardpoints::saveWithoutEditsLeavesTheValuesAlone()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString out = directory.filePath(QStringLiteral("copy.xlsx"));

    const HardpointLoadResult result = readHardpointsXlsx(dataPath("hardpoints.xlsx"));
    QVERIFY(result.ok());
    QCOMPARE(writeHardpointsXlsx(out, *result.table, result.source), QString());

    // Saving something nobody edited must not reformat it. The 17-digit text
    // Excel wrote has to come back out character for character.
    const QByteArray sheet = partOf(out, "xl/worksheets/sheet1.xml");
    QVERIFY(!sheet.isEmpty());
    QVERIFY(sheet.contains("<v>554.41200000000003</v>"));
    QVERIFY(sheet.contains("<v>-343.80632000000003</v>"));

    // The style on the value cells carries their number format, so it stays.
    QVERIFY(sheet.contains(" s=\"1\""));

    const HardpointLoadResult reread = readHardpointsXlsx(out);
    QVERIFY2(reread.ok(), qPrintable(reread.error));
    QCOMPARE(reread.table->size(), result.table->size());
    for (std::size_t i = 0; i < reread.table->size(); ++i) {
        QCOMPARE(reread.table->points[i].name, result.table->points[i].name);
        for (int axis = 0; axis < 3; ++axis)
            QCOMPARE(reread.table->points[i].coord[axis], result.table->points[i].coord[axis]);
    }
}

void TestXlsxHardpoints::saveWritesEditedCoordinates()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString out = directory.filePath(QStringLiteral("edited.xlsx"));

    HardpointLoadResult result = readHardpointsXlsx(dataPath("hardpoints.xlsx"));
    QVERIFY(result.ok());

    HardpointTable table = *result.table;
    table.points[0].coord[1] = 560.5;
    QCOMPARE(writeHardpointsXlsx(out, table, result.source), QString());

    const QByteArray sheet = partOf(out, "xl/worksheets/sheet1.xml");
    // Shortest round-tripping text, not the 17 digits a naive formatter emits.
    QVERIFY(sheet.contains("<v>560.5</v>"));
    QVERIFY(!sheet.contains("<v>554.41200000000003</v>"));

    const HardpointLoadResult reread = readHardpointsXlsx(out);
    QVERIFY2(reread.ok(), qPrintable(reread.error));
    QCOMPARE(reread.table->size(), table.size());
    QCOMPARE(findPoint(*reread.table, "F_LCA_O")->coord[1], 560.5);
    // Everything else came back bit-identical.
    QCOMPARE(findPoint(*reread.table, "F_Damper_I")->coord[0], -343.80632000000003);
}

void TestXlsxHardpoints::saveKeepsPartsItWasNotAskedToChange()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString out = directory.filePath(QStringLiteral("kept.xlsx"));

    HardpointLoadResult result = readHardpointsXlsx(dataPath("hardpoints.xlsx"));
    QVERIFY(result.ok());
    HardpointTable table = *result.table;
    table.points[1].coord[0] = -1.0;
    QCOMPARE(writeHardpointsXlsx(out, table, result.source), QString());

    // A part no spreadsheet library knows about. Rewriting a workbook through a
    // generic writer would drop it; copying the archive through keeps it.
    QCOMPARE(partOf(out, "customXml/item1.xml"),
             partOf(dataPath("hardpoints.xlsx"), "customXml/item1.xml"));
    QCOMPARE(partOf(out, "xl/styles.xml"), partOf(dataPath("hardpoints.xlsx"), "xl/styles.xml"));
    QCOMPARE(partOf(out, "xl/sharedStrings.xml"),
             partOf(dataPath("hardpoints.xlsx"), "xl/sharedStrings.xml"));

    // Formulas elsewhere would otherwise show cached results for the old values.
    QVERIFY(partOf(out, "xl/workbook.xml").contains("fullCalcOnLoad=\"1\""));
}

void TestXlsxHardpoints::saveAsLeavesTheOriginalUntouched()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    // Work on a copy, so the fixture is what it is at the end of the test.
    const QString source = directory.filePath(QStringLiteral("source.xlsx"));
    QVERIFY(QFile::copy(dataPath("hardpoints.xlsx"), source));
    QFile sourceFile(source);
    QVERIFY(sourceFile.open(QIODevice::ReadOnly));
    const QByteArray before = sourceFile.readAll();
    sourceFile.close();

    HardpointLoadResult result = readHardpointsXlsx(source);
    QVERIFY(result.ok());
    HardpointTable table = *result.table;
    table.points[0].coord[0] = 42.0;

    const QString out = directory.filePath(QStringLiteral("variant.xlsx"));
    QCOMPARE(writeHardpointsXlsx(out, table, result.source), QString());

    QVERIFY(sourceFile.open(QIODevice::ReadOnly));
    QCOMPARE(sourceFile.readAll(), before);

    const HardpointLoadResult variant = readHardpointsXlsx(out);
    QVERIFY2(variant.ok(), qPrintable(variant.error));
    QCOMPARE(findPoint(*variant.table, "F_LCA_O")->coord[0], 42.0);
}

void TestXlsxHardpoints::roundTripsThroughItsOwnOutput()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    // Our own writer's output has to be readable by our own reader, repeatedly:
    // that is what happens when somebody saves, edits and saves again.
    QString current = dataPath("hardpoints.xlsx");
    for (int generation = 0; generation < 3; ++generation) {
        HardpointLoadResult result = readHardpointsXlsx(current);
        QVERIFY2(result.ok(), qPrintable(result.error));
        QCOMPARE(result.table->size(), std::size_t(4));

        HardpointTable table = *result.table;
        table.points[0].coord[2] += 1.0;

        const QString out =
            directory.filePath(QStringLiteral("generation%1.xlsx").arg(generation));
        QCOMPARE(writeHardpointsXlsx(out, table, result.source), QString());
        current = out;
    }

    const HardpointLoadResult final = readHardpointsXlsx(current);
    QVERIFY(final.ok());
    QCOMPARE(findPoint(*final.table, "F_LCA_O")->coord[2], 138.408 + 3.0);
}

void TestXlsxHardpoints::savingAddsRowsForPointsTheWorkbookDoesNotHave()
{
    const HardpointLoadResult result = readHardpointsXlsx(dataPath("hardpoints.xlsx"));
    QVERIFY2(result.ok(), qPrintable(result.error));
    const int before = int(result.table->size());

    // What mirroring produces: a point with no cells anywhere in the file.
    HardpointTable table = *result.table;
    Hardpoint mirrored;
    mirrored.name = QStringLiteral("F_LCA_O_R");
    mirrored.coord[0] = -544.26;
    mirrored.coord[1] = -554.412;
    mirrored.coord[2] = 138.408;
    mirrored.mirrorOf = QStringLiteral("F_LCA_O");
    table.points.push_back(mirrored);

    QTemporaryDir directory;
    const QString path = directory.filePath(QStringLiteral("with_mirror.xlsx"));
    const QString error = writeHardpointsXlsx(path, table, result.source);
    QVERIFY2(error.isEmpty(), qPrintable(error));

    const HardpointLoadResult reread = readHardpointsXlsx(path);
    QVERIFY2(reread.ok(), qPrintable(reread.error));
    QCOMPARE(int(reread.table->size()), before + 1);

    const Hardpoint* point = findPoint(*reread.table, "F_LCA_O_R");
    QVERIFY(point);
    QCOMPARE(point->coord[0], -544.26);
    QCOMPARE(point->coord[1], -554.412);
    QCOMPARE(point->coord[2], 138.408);

    // The points that were already there are untouched by the append.
    const Hardpoint* original = findPoint(*reread.table, "F_LCA_O");
    QVERIFY(original);
    QCOMPARE(original->coord[1], 554.412);

    // And the parts the writer was not asked to change still come through.
    QVERIFY(partOf(path, "customXml/item1.xml").contains("vaultMetadata"));
}

void TestXlsxHardpoints::reReadingPicksUpAppendedRowsSoTheyAreNotAddedTwice()
{
    const HardpointLoadResult first = readHardpointsXlsx(dataPath("hardpoints.xlsx"));
    QVERIFY(first.ok());

    HardpointTable table = *first.table;
    Hardpoint mirrored;
    mirrored.name = QStringLiteral("F_LCA_O_R");
    mirrored.coord[1] = -554.412;
    table.points.push_back(mirrored);

    QTemporaryDir directory;
    const QString path = directory.filePath(QStringLiteral("twice.xlsx"));
    QVERIFY(writeHardpointsXlsx(path, table, first.source).isEmpty());

    // Saving again against the re-read source patches the appended cells rather
    // than appending them a second time -- which is why the application re-reads
    // the workbook after overwriting it.
    const HardpointLoadResult second = readHardpointsXlsx(path);
    QVERIFY(second.ok());
    HardpointTable edited = *second.table;
    edited.points[std::size_t(edited.indexOf(QStringLiteral("F_LCA_O_R")))].coord[1] = -1.5;

    const QString again = directory.filePath(QStringLiteral("twice_again.xlsx"));
    QVERIFY(writeHardpointsXlsx(again, edited, second.source).isEmpty());

    const HardpointLoadResult third = readHardpointsXlsx(again);
    QVERIFY(third.ok());
    QCOMPARE(int(third.table->size()), int(second.table->size()));
    QCOMPARE(findPoint(*third.table, "F_LCA_O_R")->coord[1], -1.5);
}

void TestXlsxHardpoints::aDeletedPointsRowKeepsItsNeighbouringColumns()
{
    const HardpointLoadResult result = readHardpointsXlsx(dataPath("extra_columns.xlsx"));
    QVERIFY2(result.ok(), qPrintable(result.error));
    QCOMPARE(result.table->size(), std::size_t(2));
    QCOMPARE(result.source.rows.front().nameRef[0], QStringLiteral("A2"));

    HardpointTable table = *result.table;
    table.points.erase(table.points.begin()); // F_LCA_O

    QTemporaryDir directory;
    const QString out = directory.filePath(QStringLiteral("deleted.xlsx"));
    QCOMPARE(writeHardpointsXlsx(out, table, result.source), QString());

    const QByteArray sheet = partOf(out, "xl/worksheets/sheet1.xml");
    // The name and the value are gone, and their cells -- with the style the
    // value column was formatted in -- are still there, empty.
    QVERIFY(sheet.contains("<c r=\"A2\"/>"));
    QVERIFY(sheet.contains("<c r=\"B2\" s=\"1\"/>"));
    QVERIFY(!sheet.contains("<v>-544.26</v>"));
    // The unit and the note beside it are the author's, and stay.
    QVERIFY(sheet.contains("<c r=\"C2\" t=\"s\">"));
    QVERIFY(sheet.contains("<c r=\"D2\" t=\"s\">"));
    QVERIFY(sheet.contains("<row r=\"2\">"));

    const HardpointLoadResult reread = readHardpointsXlsx(out);
    QVERIFY2(reread.ok(), qPrintable(reread.error));
    QCOMPARE(reread.table->size(), std::size_t(1));
    QVERIFY(!findPoint(*reread.table, "F_LCA_O"));
    QCOMPARE(findPoint(*reread.table, "F_UCA_O")->coord[0], -556.894);
    // A row with a unit in it and no name is a row with nothing to say, not one
    // to warn about.
    QVERIFY(reread.warnings.isEmpty());
}

void TestXlsxHardpoints::aRenamedPointLeavesNoTraceOfItsOldName()
{
    const HardpointLoadResult result = readHardpointsXlsx(dataPath("hardpoints.xlsx"));
    QVERIFY2(result.ok(), qPrintable(result.error));

    HardpointTable table = *result.table;
    const int row = table.indexOf(QStringLiteral("F_LCA_O"));
    QVERIFY(row >= 0);
    table.points[std::size_t(row)].name = QStringLiteral("F_LCA_OUTER");

    QTemporaryDir directory;
    const QString out = directory.filePath(QStringLiteral("renamed.xlsx"));
    QCOMPARE(writeHardpointsXlsx(out, table, result.source), QString());

    const HardpointLoadResult reread = readHardpointsXlsx(out);
    QVERIFY2(reread.ok(), qPrintable(reread.error));
    QCOMPARE(reread.table->size(), result.table->size());
    QVERIFY(!findPoint(*reread.table, "F_LCA_O"));
    const Hardpoint* renamed = findPoint(*reread.table, "F_LCA_OUTER");
    QVERIFY(renamed);
    QCOMPARE(renamed->coord[1], 554.41200000000003);
    // Nothing is left of it that the reader could half-recognise as a point.
    QCOMPARE(reread.warnings.size(), result.warnings.size());
}

void TestXlsxHardpoints::appendingGoesBelowARowThatHoldsOnlyFormatting()
{
    // Row 10 of the fixture is styled and holds nothing. The last row with a
    // value in it is 7, and appending at 8, 9 and then 10 would write a second
    // row 10 -- which Excel refuses to open.
    const HardpointLoadResult result = readHardpointsXlsx(dataPath("extra_columns.xlsx"));
    QVERIFY2(result.ok(), qPrintable(result.error));
    QCOMPARE(result.source.lastRow, 10);

    HardpointTable table = *result.table;
    Hardpoint added;
    added.name = QStringLiteral("F_TieRod_O");
    added.coord[0] = -600.0;
    added.coord[1] = 560.0;
    added.coord[2] = 150.0;
    table.points.push_back(added);

    QTemporaryDir directory;
    const QString out = directory.filePath(QStringLiteral("appended.xlsx"));
    QCOMPARE(writeHardpointsXlsx(out, table, result.source), QString());

    const QByteArray sheet = partOf(out, "xl/worksheets/sheet1.xml");
    QCOMPARE(sheet.count("<row r=\"10\""), qsizetype(1));
    QVERIFY(sheet.contains("<row r=\"11\">"));
    QVERIFY(sheet.contains("<row r=\"13\">"));

    const HardpointLoadResult reread = readHardpointsXlsx(out);
    QVERIFY2(reread.ok(), qPrintable(reread.error));
    QCOMPARE(findPoint(*reread.table, "F_TieRod_O")->coord[2], 150.0);
}

void TestXlsxHardpoints::theBlankWorkbookIsFilledNeverReadFirst()
{
    const QByteArray blank = blankHardpointWorkbookBytes();
    QVERIFY(!blank.isEmpty());

    QTemporaryDir directory;
    // Read on its own it is refused, which is exactly why it is never read
    // first: a sheet with no points in it is what the reader rejects.
    const QString empty = directory.filePath(QStringLiteral("empty.xlsx"));
    QFile file(empty);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(blank);
    file.close();
    QVERIFY(!readHardpointsXlsx(empty).ok());

    QString error;
    const std::optional<XlsxHardpointSource> source = blankHardpointSource(blank, &error);
    QVERIFY2(source.has_value(), qPrintable(error));
    QVERIFY(source->canAppend());
    QVERIFY(source->rows.empty());

    HardpointTable table;
    const char* names[] = { "F_LCA_O", "F_LCA_IF", "F_WheelCenter" };
    for (int i = 0; i < 3; ++i) {
        Hardpoint point;
        point.name = QLatin1String(names[i]);
        point.coord[0] = -539.1 - i;
        point.coord[1] = 554.41200000000003 + i;
        point.coord[2] = 1.0 / 3.0 + i;
        table.points.push_back(point);
    }

    const QString out = directory.filePath(QStringLiteral("new.xlsx"));
    QCOMPARE(writeNewHardpointsXlsx(out, table), QString());

    // Written, then read: exactly the points that went in, in that order, to
    // the last bit.
    const HardpointLoadResult reread = readHardpointsXlsx(out);
    QVERIFY2(reread.ok(), qPrintable(reread.error));
    QCOMPARE(reread.source.sheetName, QStringLiteral("Hardpoints"));
    QVERIFY(reread.warnings.isEmpty());
    QCOMPARE(reread.table->size(), table.size());
    for (std::size_t i = 0; i < table.size(); ++i) {
        QCOMPARE(reread.table->points[i].name, table.points[i].name);
        for (int axis = 0; axis < 3; ++axis)
            QCOMPARE(reread.table->points[i].coord[axis], table.points[i].coord[axis]);
    }
    // Under the header, not on top of it.
    QCOMPARE(reread.source.rows.front().ref[0], QStringLiteral("B2"));
}

QTEST_MAIN(TestXlsxHardpoints)
#include "test_xlsx_hardpoints.moc"
