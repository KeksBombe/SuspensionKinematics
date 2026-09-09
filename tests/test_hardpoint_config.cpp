#include "io/LinkageTemplate.h"
#include "model/HardpointConfig.h"

#include <QSet>
#include <QTest>

using namespace suspkin;

namespace {

/// A table holding exactly @p names. Where the points are is beside the point
/// here: what is under test is what the template says each of them is for.
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

/// The default: mirror about y, "_M" on the end, which is what the workbook
/// this template was written against uses.
MirrorSpec suffixMirror()
{
    return MirrorSpec{};
}

HardpointConfigMap inferFrontCorner()
{
    return inferHardpointConfig(tableOf(frontCornerNames()), builtinLinkageTemplate(),
                                suffixMirror());
}

const QString kGround = BodyCatalog::ground();
const QString kLowerWishbone = QStringLiteral("Lower wishbone");
const QString kUpperWishbone = QStringLiteral("Upper wishbone");
const QString kUpright = QStringLiteral("Upright");
const QString kPushrod = QStringLiteral("Pushrod");
const QString kWheel = QStringLiteral("Wheel");

} // namespace

class TestHardpointConfig : public QObject {
    Q_OBJECT

private slots:
    void pointTypesRoundTripThroughTheirTokens();
    void anUnknownTypeTokenFallsBack();
    void theCatalogIsTheTemplatesOwnBodies();
    void aChassisPivotIsGroundedAgainstItsOwnMember();
    void anOuterJointNamesBothMembersThatMeetThere();
    void thePushrodPicksUpOnTheArmItsMountNames();
    void whatTheUprightCarriesIsDependentOnIt();
    void theFarSideIsInferredThroughTheMirrorRule();
    void aPointTheTemplateDoesNotNameIsLeftAlone();
    void aJointBetweenOneBodyAndItselfIsRefused();
    void aBushingIndexOutsideTheRangeIsRefused();
    void aBodyThatIsNotInTheCatalogIsRefused();
    void anEmptyCatalogChecksNoBodyNames();
    void aGroundedPointWithoutTheChassisIsOnlyAWarning();
    void aSolvedPointHeldToTheChassisWarns();
    void aBushingNeedsTwoBodiesToActBetween();
    void inferenceDoesNotTalkOverWhatTheUserSet();
};

void TestHardpointConfig::pointTypesRoundTripThroughTheirTokens()
{
    for (const PointType type : kPointTypes)
        QCOMPARE(pointTypeFromString(pointTypeToString(type)), type);

    // The tokens are in project files, so they are part of the format.
    QCOMPARE(pointTypeToString(PointType::ToBody), QStringLiteral("toBody"));
    QCOMPARE(pointTypeFromString(QStringLiteral("SOLVED")), PointType::Solved);
}

void TestHardpointConfig::anUnknownTypeTokenFallsBack()
{
    QCOMPARE(pointTypeFromString(QStringLiteral("whatever")), PointType::Unassigned);
    QCOMPARE(pointTypeFromString(QString(), PointType::Solved), PointType::Solved);
}

void TestHardpointConfig::theCatalogIsTheTemplatesOwnBodies()
{
    const BodyCatalog catalog = bodyCatalog(builtinLinkageTemplate());

    QVERIFY(!catalog.isEmpty());
    QCOMPARE(catalog.bodies.first(), kGround);
    for (const QString& body : { kLowerWishbone, kUpperWishbone, kUpright, kWheel })
        QVERIFY2(catalog.contains(body), qPrintable(body));

    // The template writes its labels with {corner} and {side} still in them;
    // a dropdown of bodies must not.
    for (const QString& body : catalog.bodies) {
        QVERIFY(!body.contains(QLatin1Char('{')));
        QCOMPARE(body, body.trimmed());
    }
    // And each body is offered once, however many parts draw it.
    QCOMPARE(catalog.bodies.size(), QSet<QString>(catalog.bodies.begin(), catalog.bodies.end()).size());
}

void TestHardpointConfig::aChassisPivotIsGroundedAgainstItsOwnMember()
{
    const HardpointConfigMap config = inferFrontCorner();

    const HardpointConfig lower = config.value(QStringLiteral("F_LCA_IF"));
    QCOMPARE(lower.type, PointType::ToBody);
    QCOMPARE(lower.part1, kLowerWishbone);
    QCOMPARE(lower.part2, kGround);

    const HardpointConfig damper = config.value(QStringLiteral("F_Damper_I"));
    QCOMPARE(damper.type, PointType::ToBody);
    QCOMPARE(damper.part2, kGround);

    // What inference produces has to satisfy the rules it will be checked
    // against, or every row would open with a complaint on it.
    const BodyCatalog catalog = bodyCatalog(builtinLinkageTemplate());
    for (auto it = config.constBegin(); it != config.constEnd(); ++it) {
        const QStringList messages = issueMessages(validateHardpointConfig(it.value(), catalog));
        QVERIFY2(messages.isEmpty(),
                 qPrintable(it.key() + QStringLiteral(": ") + messages.join(QStringLiteral("; "))));
    }
}

void TestHardpointConfig::anOuterJointNamesBothMembersThatMeetThere()
{
    const HardpointConfigMap config = inferFrontCorner();

    const HardpointConfig ball = config.value(QStringLiteral("F_LCA_O"));
    QCOMPARE(ball.type, PointType::Solved);
    QCOMPARE(ball.part1, kLowerWishbone);
    QCOMPARE(ball.part2, kUpright);

    const HardpointConfig upper = config.value(QStringLiteral("F_UCA_O"));
    QCOMPARE(upper.type, PointType::Solved);
    QCOMPARE(upper.part1, kUpperWishbone);
    QCOMPARE(upper.part2, kUpright);
}

void TestHardpointConfig::thePushrodPicksUpOnTheArmItsMountNames()
{
    const HardpointConfigMap config = inferFrontCorner();

    // The template draws a "pushrod pickup" member to show which arm carries
    // the rod. That is a drawing, not a body: the mount says the upper arm, so
    // that is what the outer end has to be joined to.
    const HardpointConfig outer = config.value(QStringLiteral("F_PushRod_O"));
    QCOMPARE(outer.type, PointType::Solved);
    QCOMPARE(outer.part1, kPushrod);
    QCOMPARE(outer.part2, kUpperWishbone);

    const HardpointConfig inner = config.value(QStringLiteral("F_PushRod_I"));
    QCOMPARE(inner.part1, kPushrod);
    QCOMPARE(inner.part2, QStringLiteral("Rocker"));
}

void TestHardpointConfig::whatTheUprightCarriesIsDependentOnIt()
{
    const HardpointConfigMap config = inferFrontCorner();

    const HardpointConfig center = config.value(QStringLiteral("F_WheelCenter"));
    QCOMPARE(center.type, PointType::Dependent);
    QCOMPARE(center.part1, kUpright);
    QCOMPARE(center.part2, kWheel);

    const HardpointConfig patch = config.value(QStringLiteral("F_ContactPatch"));
    QCOMPARE(patch.type, PointType::Dependent);
    QCOMPARE(patch.part1, kWheel);
}

void TestHardpointConfig::theFarSideIsInferredThroughTheMirrorRule()
{
    QStringList names = frontCornerNames();
    for (const QString& name : frontCornerNames()) names.append(name + QStringLiteral("_M"));

    const HardpointConfigMap config =
        inferHardpointConfig(tableOf(names), builtinLinkageTemplate(), suffixMirror());

    const HardpointConfig mirrored = config.value(QStringLiteral("F_LCA_IF_M"));
    QCOMPARE(mirrored.type, PointType::ToBody);
    QCOMPARE(mirrored.part1, kLowerWishbone);
    QCOMPARE(mirrored.part2, kGround);
    // The far side is described the same way the near side is, part for part.
    QCOMPARE(config.value(QStringLiteral("F_LCA_O_M")).part2, kUpright);
}

void TestHardpointConfig::aPointTheTemplateDoesNotNameIsLeftAlone()
{
    QStringList names = frontCornerNames();
    names.append(QStringLiteral("Battery_Mount"));

    const HardpointConfigMap config =
        inferHardpointConfig(tableOf(names), builtinLinkageTemplate(), suffixMirror());

    // An empty row is honest; a guessed one is not.
    QVERIFY(!config.contains(QStringLiteral("Battery_Mount")));
}

void TestHardpointConfig::aJointBetweenOneBodyAndItselfIsRefused()
{
    HardpointConfig config;
    config.type = PointType::Solved;
    config.part1 = kUpright;
    config.part2 = kUpright;

    QVERIFY(hasError(validateHardpointConfig(config, bodyCatalog(builtinLinkageTemplate()))));
}

void TestHardpointConfig::aBushingIndexOutsideTheRangeIsRefused()
{
    const BodyCatalog catalog = bodyCatalog(builtinLinkageTemplate());
    HardpointConfig config;
    config.type = PointType::ToBody;
    config.part1 = kLowerWishbone;
    config.part2 = kGround;

    config.bushing = -1;
    QVERIFY(hasError(validateHardpointConfig(config, catalog)));

    config.bushing = kMaxBushingIndex + 1;
    QVERIFY(hasError(validateHardpointConfig(config, catalog)));

    config.bushing = kMaxBushingIndex;
    QVERIFY(!hasError(validateHardpointConfig(config, catalog)));

    config.bushing = kNoBushing;
    QVERIFY(validateHardpointConfig(config, catalog).empty());
}

void TestHardpointConfig::aBodyThatIsNotInTheCatalogIsRefused()
{
    HardpointConfig config;
    config.type = PointType::Solved;
    config.part1 = kUpright;
    config.part2 = QStringLiteral("Trailing arm");

    QVERIFY(hasError(validateHardpointConfig(config, bodyCatalog(builtinLinkageTemplate()))));
}

void TestHardpointConfig::anEmptyCatalogChecksNoBodyNames()
{
    HardpointConfig config;
    config.type = PointType::Solved;
    config.part1 = kUpright;
    config.part2 = QStringLiteral("Trailing arm");

    // A project whose template failed to load has no list to check against.
    // Refusing every edit until it does would be blaming the wrong person.
    QVERIFY(!hasError(validateHardpointConfig(config, BodyCatalog{})));
}

void TestHardpointConfig::aGroundedPointWithoutTheChassisIsOnlyAWarning()
{
    const BodyCatalog catalog = bodyCatalog(builtinLinkageTemplate());
    HardpointConfig config;
    config.type = PointType::ToBody;
    config.part1 = kLowerWishbone;
    config.part2 = kUpright;

    const std::vector<ConfigIssue> issues = validateHardpointConfig(config, catalog);
    QCOMPARE(issues.size(), std::size_t(1));
    QCOMPARE(issues.front().level, ConfigIssueLevel::Warning);
    QVERIFY(issues.front().message.contains(kGround));
    QVERIFY(!hasError(issues)); // half-finished is allowed to be stored
}

void TestHardpointConfig::aSolvedPointHeldToTheChassisWarns()
{
    HardpointConfig config;
    config.type = PointType::Solved;
    config.part1 = kLowerWishbone;
    config.part2 = kGround;

    const std::vector<ConfigIssue> issues =
        validateHardpointConfig(config, bodyCatalog(builtinLinkageTemplate()));
    QCOMPARE(issues.size(), std::size_t(1));
    QCOMPARE(issues.front().level, ConfigIssueLevel::Warning);
}

void TestHardpointConfig::aBushingNeedsTwoBodiesToActBetween()
{
    HardpointConfig config;
    config.type = PointType::Dependent;
    config.part1 = kUpright;
    config.bushing = 3;

    const std::vector<ConfigIssue> issues =
        validateHardpointConfig(config, bodyCatalog(builtinLinkageTemplate()));
    QCOMPARE(issues.size(), std::size_t(1));
    QCOMPARE(issues.front().level, ConfigIssueLevel::Warning);
    QVERIFY(!hasError(issues));
}

void TestHardpointConfig::inferenceDoesNotTalkOverWhatTheUserSet()
{
    HardpointConfigMap config;
    HardpointConfig mine;
    mine.type = PointType::Dependent;
    mine.part1 = kUpright;
    mine.bushing = 7;
    config.insert(QStringLiteral("F_LCA_IF"), mine);
    // A row cleared on purpose is an answer too, and stays cleared.
    config.insert(QStringLiteral("F_LCA_IR"), HardpointConfig{});

    const HardpointConfigMap inferred = inferFrontCorner();
    const int added = fillMissingConfig(config, inferred);

    QCOMPARE(config.value(QStringLiteral("F_LCA_IF")), mine);
    QVERIFY(config.value(QStringLiteral("F_LCA_IR")).isEmpty());
    QCOMPARE(config.value(QStringLiteral("F_LCA_O")).type, PointType::Solved);
    QCOMPARE(added, inferred.size() - 2);

    // Running it again has nothing left to do.
    QCOMPARE(fillMissingConfig(config, inferred), 0);
}

QTEST_MAIN(TestHardpointConfig)
#include "test_hardpoint_config.moc"
