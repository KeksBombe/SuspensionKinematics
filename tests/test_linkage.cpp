#include "io/LinkageTemplate.h"
#include "model/Linkage.h"

#include <QTemporaryDir>
#include <QTest>

using namespace suspkin;

namespace {

/// A table holding exactly @p names, at coordinates nothing depends on: what is
/// under test is which points a part connects, never where they are.
HardpointTable tableOf(const QStringList& names)
{
    HardpointTable table;
    double value = 0.0;
    for (const QString& name : names) {
        Hardpoint point;
        point.name = name;
        point.coord[0] = value;
        point.coord[1] = value + 1.0;
        point.coord[2] = value + 2.0;
        value += 10.0;
        table.points.push_back(point);
    }
    return table;
}

/// The names the shipped template asks for, for one corner and one side.
QStringList frontCornerNames()
{
    return QStringList{
        QStringLiteral("F_LCA_IF"),          QStringLiteral("F_LCA_IR"),
        QStringLiteral("F_LCA_O"),           QStringLiteral("F_UCA_IF"),
        QStringLiteral("F_UCA_IR"),          QStringLiteral("F_UCA_O"),
        QStringLiteral("F_TieRod_I"),        QStringLiteral("F_TieRod_O"),
        QStringLiteral("F_PushRod_I"),       QStringLiteral("F_PushRod_O"),
        QStringLiteral("F_Rocker_Center"),   QStringLiteral("F_Rocker_AxisPoint"),
        QStringLiteral("F_Damper_I"),        QStringLiteral("F_Damper_O"),
        QStringLiteral("F_AntiRoll_Center"), QStringLiteral("F_AntiRoll_I"),
        QStringLiteral("F_AntiRoll_O"),      QStringLiteral("F_WheelCenter"),
        QStringLiteral("F_ContactPatch"),
    };
}

const LinkagePart* findPart(const Linkage& linkage, const QString& id)
{
    for (const LinkagePart& part : linkage.parts)
        if (part.id == id) return &part;
    return nullptr;
}

/// The default: mirror about y, "_M" on the end, which is what the workbook
/// this template was written against uses.
MirrorSpec suffixMirror()
{
    return MirrorSpec{};
}

} // namespace

class TestLinkage : public QObject {
    Q_OBJECT

private slots:
    void theBuiltInTemplateIsReadable();
    void aWishboneIsAClosedTriangle();
    void everyPartOfACornerResolves();
    void theFarSideIsAbsentUntilTheTableIsMirrored();
    void theFarSideComesFromTheProjectsOwnMirrorRule();
    void aMissingPointIsReportedAndTheRestIsStillDrawn();
    void anOptionalPartSaysNothingWhenItIsAbsent();
    void chainsShorthandAndLongFormMeanTheSame();
    void aTemplateRoundTripsThroughItsFile();
    void somethingThatIsNotATemplateIsRejected();
};

void TestLinkage::theBuiltInTemplateIsReadable()
{
    const LinkageTemplateLoadResult result =
        readLinkageTemplate(builtinLinkageTemplateBytes(), QStringLiteral("built-in"));
    QVERIFY2(result.ok(), qPrintable(result.error));
    QVERIFY(result.warnings.isEmpty());

    const LinkageTemplate& templ = result.templ.value();
    QCOMPARE(templ.corners.size(), std::size_t(2));
    QCOMPARE(templ.corners[0].token, QStringLiteral("F"));
    QCOMPARE(templ.corners[1].token, QStringLiteral("R"));
    QCOMPARE(templ.baseSideLabel, QStringLiteral("left"));
    QCOMPARE(templ.mirroredSideLabel, QStringLiteral("right"));
    QVERIFY(!templ.notes.isEmpty());

    // The parts a double-wishbone corner is made of, all of them present.
    for (const char* id : { "lowerWishbone", "upperWishbone", "upright", "tieRod", "pushRod",
                            "rocker", "damper", "antiRollArm", "antiRollDropLink", "wheel" }) {
        bool found = false;
        for (const PartTemplate& part : templ.parts)
            found = found || part.id == QLatin1String(id);
        QVERIFY2(found, id);
    }
}

void TestLinkage::aWishboneIsAClosedTriangle()
{
    const Linkage linkage =
        buildLinkage(builtinLinkageTemplate(), tableOf(frontCornerNames()), suffixMirror());

    const LinkagePart* wishbone = findPart(linkage, QStringLiteral("lowerWishbone@F:base"));
    QVERIFY(wishbone);
    QCOMPARE(wishbone->kind, PartKind::Wishbone);
    QCOMPARE(wishbone->label, QStringLiteral("Front left lower wishbone"));
    QCOMPARE(wishbone->chains.size(), std::size_t(1));
    QVERIFY(wishbone->chains.front().closed);
    // Three points, closed: two legs and the chassis-side edge between them.
    QCOMPARE(wishbone->chains.front().points.size(), std::size_t(3));
    QCOMPARE(wishbone->segmentCount(), 3);
}

void TestLinkage::everyPartOfACornerResolves()
{
    const Linkage linkage =
        buildLinkage(builtinLinkageTemplate(), tableOf(frontCornerNames()), suffixMirror());

    QVERIFY2(linkage.warnings.isEmpty(), qPrintable(linkage.warnings.join(QLatin1Char('\n'))));
    // Eleven parts in the template, one corner's worth of points in the table.
    QCOMPARE(linkage.parts.size(), std::size_t(11));
    for (const LinkagePart& part : linkage.parts) {
        QVERIFY2(part.segmentCount() > 0, qPrintable(part.id));
        QCOMPARE(part.corner, QStringLiteral("F"));
        QVERIFY(!part.mirrored);
    }

    // The upright is the one part with two chains: the triangle its three joints
    // make, and the wheel hanging off the kingpin.
    const LinkagePart* upright = findPart(linkage, QStringLiteral("upright@F:base"));
    QVERIFY(upright);
    QCOMPARE(upright->chains.size(), std::size_t(2));
    QCOMPARE(upright->segmentCount(), 5);
}

void TestLinkage::theFarSideIsAbsentUntilTheTableIsMirrored()
{
    const Linkage linkage =
        buildLinkage(builtinLinkageTemplate(), tableOf(frontCornerNames()), suffixMirror());

    for (const LinkagePart& part : linkage.parts) QVERIFY(!part.mirrored);
    // Not a warning either: a table that has not been mirrored is not a table
    // that is missing half its points.
    QVERIFY(linkage.warnings.isEmpty());
}

void TestLinkage::theFarSideComesFromTheProjectsOwnMirrorRule()
{
    QStringList names = frontCornerNames();
    // A workbook that carries both sides with an "_R" suffix rather than the
    // default "_M": the rule is the project's, and the template never says.
    for (const QString& name : frontCornerNames()) names.append(name + QStringLiteral("_R"));

    MirrorSpec spec;
    spec.affix = QStringLiteral("_R");

    const Linkage linkage = buildLinkage(builtinLinkageTemplate(), tableOf(names), spec);
    QVERIFY(linkage.warnings.isEmpty());
    QCOMPARE(linkage.parts.size(), std::size_t(22));

    const LinkagePart* mirrored = findPart(linkage, QStringLiteral("lowerWishbone@F:mirror"));
    QVERIFY(mirrored);
    QVERIFY(mirrored->mirrored);
    QCOMPARE(mirrored->label, QStringLiteral("Front right lower wishbone"));
    QCOMPARE(mirrored->segmentCount(), 3);
}

void TestLinkage::aMissingPointIsReportedAndTheRestIsStillDrawn()
{
    QStringList names = frontCornerNames();
    names.removeAll(QStringLiteral("F_LCA_IR"));

    const Linkage linkage = buildLinkage(builtinLinkageTemplate(), tableOf(names), suffixMirror());

    QCOMPARE(linkage.warnings.size(), 1);
    QVERIFY(linkage.warnings.front().contains(QStringLiteral("F_LCA_IR")));

    // One leg is more use than nothing, but the arm must not be closed across a
    // pickup point that is not there.
    const LinkagePart* wishbone = findPart(linkage, QStringLiteral("lowerWishbone@F:base"));
    QVERIFY(wishbone);
    QCOMPARE(wishbone->chains.size(), std::size_t(1));
    QVERIFY(!wishbone->chains.front().closed);
    QCOMPARE(wishbone->segmentCount(), 1);
}

void TestLinkage::anOptionalPartSaysNothingWhenItIsAbsent()
{
    QStringList names = frontCornerNames();
    for (const char* absent : { "F_AntiRoll_Center", "F_AntiRoll_I", "F_AntiRoll_O",
                                "F_Rocker_AxisPoint" })
        names.removeAll(QLatin1String(absent));

    const Linkage linkage = buildLinkage(builtinLinkageTemplate(), tableOf(names), suffixMirror());

    QVERIFY2(linkage.warnings.isEmpty(), qPrintable(linkage.warnings.join(QLatin1Char('\n'))));
    QVERIFY(!findPart(linkage, QStringLiteral("antiRollArm@F:base")));
    // The rocker loses its pivot axis and its anti-roll spoke, and keeps the two
    // spokes to the pushrod and the damper plus the plate edge between them.
    const LinkagePart* rocker = findPart(linkage, QStringLiteral("rocker@F:base"));
    QVERIFY(rocker);
    QCOMPARE(rocker->chains.size(), std::size_t(2));
    QCOMPARE(rocker->segmentCount(), 3);
}

void TestLinkage::chainsShorthandAndLongFormMeanTheSame()
{
    const QByteArray shorthand = R"({
        "format": "suspkin-linkage-template",
        "formatVersion": 1,
        "parts": [{ "id": "arm", "kind": "wishbone",
                    "points": ["A", "B", "C"], "closed": true }]
    })";
    const QByteArray longForm = R"({
        "format": "suspkin-linkage-template",
        "formatVersion": 1,
        "parts": [{ "id": "arm", "kind": "wishbone",
                    "chains": [{ "points": ["A", "B", "C"], "closed": true }] }]
    })";

    for (const QByteArray& bytes : { shorthand, longForm }) {
        const LinkageTemplateLoadResult result =
            readLinkageTemplate(bytes, QStringLiteral("inline"));
        QVERIFY2(result.ok(), qPrintable(result.error));
        QCOMPARE(result.templ->parts.size(), std::size_t(1));
        QCOMPARE(result.templ->parts.front().chains.size(), std::size_t(1));
        QVERIFY(result.templ->parts.front().chains.front().closed);

        const HardpointTable table = tableOf({ QStringLiteral("A"), QStringLiteral("B"),
                                               QStringLiteral("C") });
        QCOMPARE(buildLinkage(*result.templ, table, suffixMirror()).segmentCount(), 3);
    }
}

void TestLinkage::aTemplateRoundTripsThroughItsFile()
{
    const LinkageTemplate original = builtinLinkageTemplate();
    QVERIFY(!original.isEmpty());

    const LinkageTemplateLoadResult reread =
        readLinkageTemplate(writeLinkageTemplate(original), QStringLiteral("round trip"));
    QVERIFY2(reread.ok(), qPrintable(reread.error));

    const LinkageTemplate& copy = reread.templ.value();
    QCOMPARE(copy.name, original.name);
    QCOMPARE(copy.notes, original.notes);
    QCOMPARE(copy.corners.size(), original.corners.size());
    QCOMPARE(copy.baseSideLabel, original.baseSideLabel);
    QCOMPARE(copy.parts.size(), original.parts.size());
    for (std::size_t i = 0; i < copy.parts.size(); ++i) {
        QCOMPARE(copy.parts[i].id, original.parts[i].id);
        QCOMPARE(copy.parts[i].label, original.parts[i].label);
        QCOMPARE(copy.parts[i].kind, original.parts[i].kind);
        QCOMPARE(copy.parts[i].optional, original.parts[i].optional);
        QCOMPARE(copy.parts[i].chains.size(), original.parts[i].chains.size());
        for (std::size_t c = 0; c < copy.parts[i].chains.size(); ++c) {
            QCOMPARE(copy.parts[i].chains[c].points, original.parts[i].chains[c].points);
            QCOMPARE(copy.parts[i].chains[c].closed, original.parts[i].chains[c].closed);
            QCOMPARE(copy.parts[i].chains[c].optional, original.parts[i].chains[c].optional);
        }
    }

    // And through an actual file, which is how a project holds it.
    QTemporaryDir directory;
    const QString path = directory.filePath(QStringLiteral("template.json"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(writeLinkageTemplate(original));
    file.close();

    const LinkageTemplateLoadResult fromFile = readLinkageTemplateFile(path);
    QVERIFY2(fromFile.ok(), qPrintable(fromFile.error));
    QCOMPARE(fromFile.templ->parts.size(), original.parts.size());
}

void TestLinkage::somethingThatIsNotATemplateIsRejected()
{
    QVERIFY(!readLinkageTemplate(QByteArray("not json at all"), QStringLiteral("x")).ok());
    QVERIFY(!readLinkageTemplate(QByteArray(R"({"format":"something-else"})"),
                                 QStringLiteral("x"))
                 .ok());
    // Right format, nothing in it: there is no linkage to draw, and saying so is
    // more use than an empty viewport.
    QVERIFY(!readLinkageTemplate(
                 QByteArray(R"({"format":"suspkin-linkage-template","parts":[]})"),
                 QStringLiteral("x"))
                 .ok());
    QVERIFY(!readLinkageTemplateFile(QStringLiteral("/nowhere/at/all.json")).ok());
}

QTEST_MAIN(TestLinkage)
#include "test_linkage.moc"
