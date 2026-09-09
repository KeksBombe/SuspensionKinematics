#include "io/LinkageTemplate.h"
#include "model/Mechanism.h"
#include "model/Linkage.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
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
        QStringLiteral("F_WheelAxis"),       QStringLiteral("F_ContactPatch"),
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

    void theBuiltinTemplateSaysWhichPointsMakeTheMechanism();
    void theBuiltinTemplateSteersTheFrontAxleAndNotTheRear();
    void aCornersSteeringSurvivesAWriteAndAReadBack();
    void patchingTheSteeringLeavesTheRestOfTheFileAlone();
    void theMechanismSurvivesAWriteAndAReadBack();
    void aTemplateWithoutAMechanismStillLoads();
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

void TestLinkage::theBuiltinTemplateSaysWhichPointsMakeTheMechanism()
{
    const LinkageTemplate templ = builtinLinkageTemplate();
    QVERIFY(templ.canSimulate());

    const MechanismTemplate& mechanism = templ.mechanism;
    QCOMPARE(mechanism.lowerFront, QStringLiteral("{corner}_LCA_IF"));
    QCOMPARE(mechanism.upperOuter, QStringLiteral("{corner}_UCA_O"));
    QCOMPARE(mechanism.tieRodOutboard, QStringLiteral("{corner}_TieRod_O"));
    QCOMPARE(mechanism.wheelCenter, QStringLiteral("{corner}_WheelCenter"));
    // Which way the wheel points, which is what turns the model with the
    // steering instead of leaving it pointing straight ahead all day.
    QCOMPARE(mechanism.wheelAxis, QStringLiteral("{corner}_WheelAxis"));
    QVERIFY(mechanism.hasRocker());
    QVERIFY(mechanism.hasAntiRoll());

    // The workbook this was written against mounts the pushrod on the upper
    // wishbone, and the solver has to be told the same thing the drawing is.
    QCOMPARE(mechanism.pushrodMount, PushrodMount::UpperArm);

    // Every name the mechanism asks for is one the parts already draw, so a
    // table that draws also solves.
    const Linkage linkage = buildLinkage(templ, tableOf(frontCornerNames()), suffixMirror());
    QVERIFY(!linkage.isEmpty());
    for (const QString& name : mechanism.allNames())
        QVERIFY2(frontCornerNames().contains(QString(name).replace(QStringLiteral("{corner}"),
                                                                   QStringLiteral("F"))),
                 qPrintable(name));
}

void TestLinkage::theBuiltinTemplateSteersTheFrontAxleAndNotTheRear()
{
    const LinkageTemplate templ = builtinLinkageTemplate();
    QVERIFY(templ.steeringDeclared());
    QCOMPARE(templ.corners.size(), std::size_t(2));

    // Which axle has a rack is the corner's own business: the mechanism block is
    // one block for every corner and cannot say it.
    QCOMPARE(templ.corners[0].token, QStringLiteral("F"));
    QCOMPARE(templ.corners[0].steeringRack, QStringLiteral("{corner}_TieRod_I"));
    QCOMPARE(templ.corners[1].token, QStringLiteral("R"));
    QVERIFY(templ.corners[1].steeringRack.isEmpty());
}

void TestLinkage::aCornersSteeringSurvivesAWriteAndAReadBack()
{
    const LinkageTemplate original = builtinLinkageTemplate();
    const LinkageTemplateLoadResult reloaded =
        readLinkageTemplate(writeLinkageTemplate(original), QStringLiteral("round trip"));
    QVERIFY2(reloaded.ok(), qPrintable(reloaded.error));
    QCOMPARE(reloaded.templ->corners[0].steeringRack, original.corners[0].steeringRack);
    QVERIFY(reloaded.templ->corners[1].steeringRack.isEmpty());

    // A corner written as a bare string is a token and nothing else, so it names
    // no rack -- and a template of nothing but those says nothing at all, which
    // is what leaves an older project's axles steering.
    const QByteArray shorthand = R"({
        "format": "suspkin-linkage-template",
        "formatVersion": 1,
        "name": "shorthand corners",
        "corners": ["F", "R"],
        "parts": [ { "id": "link", "points": ["F_TieRod_I", "F_TieRod_O"] } ]
    })";
    const LinkageTemplateLoadResult bare =
        readLinkageTemplate(shorthand, QStringLiteral("shorthand"));
    QVERIFY2(bare.ok(), qPrintable(bare.error));
    QVERIFY(!bare.templ->steeringDeclared());
}

void TestLinkage::patchingTheSteeringLeavesTheRestOfTheFileAlone()
{
    // A template with a note, a part and a key this version knows nothing about.
    const QByteArray before = R"({
        "format": "suspkin-linkage-template",
        "formatVersion": 1,
        "name": "somebody's own template",
        "notes": ["hand written, do not lose me"],
        "somethingFromTheFuture": { "keep": "me" },
        "corners": [
            { "token": "F", "label": "Front", "colour": "red" },
            "R"
        ],
        "parts": [ { "id": "link", "points": ["F_TieRod_I", "F_TieRod_O"] } ]
    })";

    std::vector<CornerSpec> corners;
    CornerSpec front;
    front.token = QStringLiteral("F");
    front.steeringRack = QStringLiteral("{corner}_TieRod_I");
    corners.push_back(front);
    CornerSpec rear;
    rear.token = QStringLiteral("R");
    corners.push_back(rear); // no rack: left as the shorthand it was

    QString error;
    const QByteArray after = setTemplateSteering(before, corners, &error);
    QVERIFY2(!after.isEmpty(), qPrintable(error));

    // Byte for byte outside the corners array: the notes keep their order, the
    // unknown key keeps its shape, and nothing is reformatted. A file somebody
    // edits by hand has to survive being edited by us.
    QVERIFY(after.contains(R"("somethingFromTheFuture": { "keep": "me" })"));
    QVERIFY(after.contains(R"("notes": ["hand written, do not lose me"])"));
    QVERIFY(after.contains(
        R"({ "token": "F", "label": "Front", "colour": "red", "steering": "{corner}_TieRod_I" })"));
    QVERIFY(after.indexOf("\"name\"") < after.indexOf("\"notes\""));
    QVERIFY(after.indexOf("\"notes\"") < after.indexOf("\"corners\""));
    QVERIFY(after.indexOf("\"corners\"") < after.indexOf("\"parts\""));

    const QJsonObject root = QJsonDocument::fromJson(after).object();
    // The file is the user's: nothing but the steering was touched.
    QCOMPARE(root.value(QStringLiteral("name")).toString(),
             QStringLiteral("somebody's own template"));
    QCOMPARE(root.value(QStringLiteral("notes")).toArray().first().toString(),
             QStringLiteral("hand written, do not lose me"));
    QCOMPARE(root.value(QStringLiteral("somethingFromTheFuture"))
                 .toObject()
                 .value(QStringLiteral("keep"))
                 .toString(),
             QStringLiteral("me"));

    const QJsonArray patched = root.value(QStringLiteral("corners")).toArray();
    const QJsonObject frontOut = patched.at(0).toObject();
    QCOMPARE(frontOut.value(QStringLiteral("steering")).toString(),
             QStringLiteral("{corner}_TieRod_I"));
    QCOMPARE(frontOut.value(QStringLiteral("colour")).toString(), QStringLiteral("red"));
    // A corner that gains no rack keeps the shorthand it was written in.
    QVERIFY(patched.at(1).isString());

    const LinkageTemplateLoadResult reread =
        readLinkageTemplate(after, QStringLiteral("patched"));
    QVERIFY2(reread.ok(), qPrintable(reread.error));
    QVERIFY(reread.templ->steeringDeclared());
    QCOMPARE(reread.templ->corners[0].steeringRack, QStringLiteral("{corner}_TieRod_I"));
    QVERIFY(reread.templ->corners[1].steeringRack.isEmpty());

    // And a rack taken away again leaves the file without the key -- and with
    // the corner otherwise exactly as it was before any of this.
    corners[0].steeringRack.clear();
    corners[0].steeringStated = false;
    const QByteArray cleared = setTemplateSteering(after, corners, &error);
    QVERIFY2(!cleared.isEmpty(), qPrintable(error));
    QVERIFY(cleared.contains(R"({ "token": "F", "label": "Front", "colour": "red" })"));
    QVERIFY(!QJsonDocument::fromJson(cleared)
                 .object()
                 .value(QStringLiteral("corners"))
                 .toArray()
                 .at(0)
                 .toObject()
                 .contains(QStringLiteral("steering")));
}

void TestLinkage::theMechanismSurvivesAWriteAndAReadBack()
{
    const LinkageTemplate original = builtinLinkageTemplate();
    const LinkageTemplateLoadResult reloaded =
        readLinkageTemplate(writeLinkageTemplate(original), QStringLiteral("round trip"));
    QVERIFY2(reloaded.ok(), qPrintable(reloaded.error));

    const MechanismTemplate& before = original.mechanism;
    const MechanismTemplate& after = reloaded.templ->mechanism;
    QCOMPARE(after.allNames(), before.allNames());
    QCOMPARE(after.pushrodMount, before.pushrodMount);
    QCOMPARE(after.lowerRear, before.lowerRear);
    QCOMPARE(after.antiRollArmPivot, before.antiRollArmPivot);
    QCOMPARE(after.wheelAxis, before.wheelAxis);
    QCOMPARE(after.contactPatch, before.contactPatch);
}

void TestLinkage::aTemplateWithoutAMechanismStillLoads()
{
    // The block is additive: a template written before the solver existed is a
    // perfectly good template, it just does not say which point is which. It is
    // read with the built-in roles assumed, because a file like this is almost
    // certainly a copy of the built-in template -- and without them nothing
    // downstream can do anything at all: no corner solves, and every hardpoint
    // in the table reads "unassigned".
    const QByteArray bare = R"({
        "format": "suspkin-linkage-template",
        "formatVersion": 1,
        "name": "drawing only",
        "parts": [ { "id": "link", "points": ["A", "B"] } ]
    })";
    const LinkageTemplateLoadResult result =
        readLinkageTemplate(bare, QStringLiteral("bare template"));
    QVERIFY2(result.ok(), qPrintable(result.error));
    QVERIFY(!result.templ->isEmpty());
    QVERIFY(result.templ->canSimulate());
    QCOMPARE(result.templ->mechanism.lowerOuter, builtinMechanismTemplate().lowerOuter);
    // Flagged, because it is a guess and not the user's own statement.
    QVERIFY(result.templ->mechanismAssumed);

    // A template that does say so keeps what it says, and is not flagged.
    QVERIFY(!builtinLinkageTemplate().mechanismAssumed);
    QVERIFY(!builtinMechanismTemplate().isEmpty());
}

QTEST_MAIN(TestLinkage)
#include "test_linkage.moc"
