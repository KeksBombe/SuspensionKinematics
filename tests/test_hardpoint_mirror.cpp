#include "model/HardpointMirror.h"

#include <QTest>

using namespace suspkin;

namespace {

Hardpoint make(const char* name, double x, double y, double z)
{
    Hardpoint point;
    point.name = QLatin1String(name);
    point.coord[0] = x;
    point.coord[1] = y;
    point.coord[2] = z;
    return point;
}

HardpointTable sample()
{
    HardpointTable table;
    table.points.push_back(make("F_LCA_O", -544.26, 554.412, 138.408));
    table.points.push_back(make("F_LCA_F", -300.0, 220.0, 150.0));
    table.points.push_back(make("CHASSIS_CG", 0.0, 0.0, 300.0));
    return table;
}

} // namespace

class TestHardpointMirror : public QObject {
    Q_OBJECT

private slots:
    void suffixMirrorNegatesTheChosenAxis();
    void mirroringOnlyTheSelectedRow();
    void mirroringTwiceDoesNotProduceMirrorsOfMirrors();
    void existingTargetsAreUpdatedOrKept();
    void replaceNamingSkipsNamesItDoesNotMatch();
    void aRuleThatWouldRenameOntoItselfIsRefused();
    void specsSurviveTheirTextForm();
};

void TestHardpointMirror::suffixMirrorNegatesTheChosenAxis()
{
    MirrorSpec spec;
    spec.affix = QStringLiteral("_R");

    const MirrorOutcome outcome = mirrorHardpoints(sample(), {}, spec);
    QCOMPARE(outcome.added, 3);
    QCOMPARE(outcome.updated, 0);
    QCOMPARE(int(outcome.table.size()), 6);

    const Hardpoint* mirrored = outcome.table.find(QStringLiteral("F_LCA_O_R"));
    QVERIFY(mirrored);
    // Only Y flips: X and Z have to come through untouched, or a mirrored corner
    // would not sit at the same station or ride height.
    QCOMPARE(mirrored->coord[0], -544.26);
    QCOMPARE(mirrored->coord[1], -554.412);
    QCOMPARE(mirrored->coord[2], 138.408);
    QCOMPARE(mirrored->mirrorOf, QStringLiteral("F_LCA_O"));

    // The originals keep their place, so a workbook still reads in its own order.
    QCOMPARE(outcome.table.points.front().name, QStringLiteral("F_LCA_O"));

    // A point on the mirror plane comes back as a plain zero, not a negative one.
    const Hardpoint* onPlane = outcome.table.find(QStringLiteral("CHASSIS_CG_R"));
    QVERIFY(onPlane);
    QCOMPARE(onPlane->coord[1], 0.0);
    QVERIFY(!std::signbit(onPlane->coord[1]));
}

void TestHardpointMirror::mirroringOnlyTheSelectedRow()
{
    MirrorSpec spec;
    spec.affix = QStringLiteral("_R");

    const MirrorOutcome outcome = mirrorHardpoints(sample(), { 1 }, spec);
    QCOMPARE(outcome.added, 1);
    QCOMPARE(int(outcome.table.size()), 4);
    QVERIFY(outcome.table.find(QStringLiteral("F_LCA_F_R")));
    QVERIFY(!outcome.table.find(QStringLiteral("F_LCA_O_R")));
}

void TestHardpointMirror::mirroringTwiceDoesNotProduceMirrorsOfMirrors()
{
    MirrorSpec spec;
    spec.affix = QStringLiteral("_R");

    const MirrorOutcome first = mirrorHardpoints(sample(), {}, spec);
    const MirrorOutcome second = mirrorHardpoints(first.table, {}, spec);

    QCOMPARE(second.added, 0);
    QCOMPARE(second.updated, 3); // the three originals, recomputed onto themselves
    QCOMPARE(int(second.table.size()), 6);
    QVERIFY(!second.table.find(QStringLiteral("F_LCA_O_R_R")));
}

void TestHardpointMirror::existingTargetsAreUpdatedOrKept()
{
    HardpointTable table = sample();
    table.points.push_back(make("F_LCA_O_R", 1.0, 2.0, 3.0));

    MirrorSpec spec;
    spec.affix = QStringLiteral("_R");
    spec.updateExisting = true;
    const MirrorOutcome updated = mirrorHardpoints(table, { 0 }, spec);
    QCOMPARE(updated.updated, 1);
    QCOMPARE(updated.table.find(QStringLiteral("F_LCA_O_R"))->coord[1], -554.412);

    spec.updateExisting = false;
    const MirrorOutcome kept = mirrorHardpoints(table, { 0 }, spec);
    QCOMPARE(kept.updated, 0);
    QCOMPARE(kept.skipped, 1);
    QCOMPARE(kept.table.find(QStringLiteral("F_LCA_O_R"))->coord[1], 2.0);
    QVERIFY(!kept.notes.isEmpty());
}

void TestHardpointMirror::replaceNamingSkipsNamesItDoesNotMatch()
{
    HardpointTable table;
    table.points.push_back(make("L_UCA_O", 100.0, 500.0, 400.0));
    table.points.push_back(make("STEERING_RACK", 0.0, 0.0, 200.0));

    MirrorSpec spec;
    spec.naming = MirrorNaming::Replace;
    spec.findText = QStringLiteral("L_");
    spec.replaceText = QStringLiteral("R_");

    const MirrorOutcome outcome = mirrorHardpoints(table, {}, spec);
    QCOMPARE(outcome.added, 1);
    QCOMPARE(outcome.skipped, 1);
    QVERIFY(outcome.table.find(QStringLiteral("R_UCA_O")));
    QCOMPARE(outcome.table.find(QStringLiteral("R_UCA_O"))->coord[1], -500.0);
}

void TestHardpointMirror::aRuleThatWouldRenameOntoItselfIsRefused()
{
    MirrorSpec spec;
    spec.affix.clear();
    QVERIFY(mirroredName(QStringLiteral("F_LCA_O"), spec).isEmpty());

    spec.naming = MirrorNaming::Replace;
    spec.findText = QStringLiteral("F");
    spec.replaceText = QStringLiteral("F");
    QVERIFY(mirroredName(QStringLiteral("F_LCA_O"), spec).isEmpty());
}

void TestHardpointMirror::specsSurviveTheirTextForm()
{
    for (const MirrorAxis axis : { MirrorAxis::X, MirrorAxis::Y, MirrorAxis::Z })
        QCOMPARE(mirrorAxisFromString(mirrorAxisToString(axis)), axis);
    for (const MirrorNaming naming :
         { MirrorNaming::Suffix, MirrorNaming::Prefix, MirrorNaming::Replace })
        QCOMPARE(mirrorNamingFromString(mirrorNamingToString(naming)), naming);

    // Unknown text falls back rather than throwing the project's rule away.
    QCOMPARE(mirrorAxisFromString(QStringLiteral("w"), MirrorAxis::Z), MirrorAxis::Z);
}

QTEST_APPLESS_MAIN(TestHardpointMirror)
#include "test_hardpoint_mirror.moc"
