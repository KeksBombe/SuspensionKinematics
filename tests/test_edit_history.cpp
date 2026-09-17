#include "model/EditHistory.h"

#include <QTest>

using namespace suspkin;

namespace {

Hardpoint point(const QString& name, double x, double y = 0.0, double z = 0.0)
{
    Hardpoint p;
    p.name = name;
    p.coord[0] = x;
    p.coord[1] = y;
    p.coord[2] = z;
    return p;
}

/// A state of two points, A at @p ax and B at 20.
EditState twoPoints(double ax = 10.0)
{
    EditState state;
    state.table.points = { point(QStringLiteral("A"), ax), point(QStringLiteral("B"), 20.0) };
    return state;
}

double xOfA(const EditState& state)
{
    return state.table.find(QStringLiteral("A"))->x();
}

} // namespace

class TestEditHistory : public QObject {
    Q_OBJECT

private slots:
    void aNewHistoryHasNothingToUndoOrRedo();
    void anUndoPutsBackThePreviousStateAndARedoTheNext();
    void theLabelsNameTheStepEachWayWouldTake();
    void aNewEditAfterAnUndoDropsWhatCouldHaveBeenRedone();
    void anEditThatChangedNothingIsNotAStep();
    void mirrorProvenanceAndConfigurationAndAnglesAreState();
    void aResetForgetsEveryStep();
    void theLimitDropsTheOldestSteps();
    void undoingPastTheFirstStateStaysThere();
    void aRelabelledBodyIsRenamedInEveryState();
    void aRenameIsReadOffTheRowItHappenedIn();
    void anythingOtherThanARenameIsNotOne();
    void theTouchedPointsAreWhatTheStepChanged();
};

void TestEditHistory::aNewHistoryHasNothingToUndoOrRedo()
{
    EditHistory history;
    history.reset(twoPoints());
    QVERIFY(!history.canUndo());
    QVERIFY(!history.canRedo());
    QCOMPARE(history.undoCount(), 0);
    QCOMPARE(history.redoCount(), 0);
    QVERIFY(history.undoLabel().isEmpty());
    QVERIFY(history.redoLabel().isEmpty());
}

void TestEditHistory::anUndoPutsBackThePreviousStateAndARedoTheNext()
{
    EditHistory history;
    history.reset(twoPoints(10.0));
    QVERIFY(history.record(QStringLiteral("Move A"), twoPoints(11.0)));
    QVERIFY(history.record(QStringLiteral("Move A"), twoPoints(12.0)));
    QCOMPARE(history.undoCount(), 2);

    QCOMPARE(xOfA(history.undo()), 11.0);
    QCOMPARE(xOfA(history.undo()), 10.0);
    QVERIFY(!history.canUndo());
    QCOMPARE(history.redoCount(), 2);

    QCOMPARE(xOfA(history.redo()), 11.0);
    QCOMPARE(xOfA(history.redo()), 12.0);
    QVERIFY(!history.canRedo());
    QCOMPARE(xOfA(history.current()), 12.0);
}

void TestEditHistory::theLabelsNameTheStepEachWayWouldTake()
{
    EditHistory history;
    history.reset(twoPoints(10.0));
    history.record(QStringLiteral("Move A"), twoPoints(11.0));
    history.record(QStringLiteral("Nudge A"), twoPoints(12.0));

    QCOMPARE(history.undoLabel(), QStringLiteral("Nudge A"));
    history.undo();
    // The step just taken back is the one a redo would do again.
    QCOMPARE(history.redoLabel(), QStringLiteral("Nudge A"));
    QCOMPARE(history.undoLabel(), QStringLiteral("Move A"));
}

void TestEditHistory::aNewEditAfterAnUndoDropsWhatCouldHaveBeenRedone()
{
    EditHistory history;
    history.reset(twoPoints(10.0));
    history.record(QStringLiteral("Move A"), twoPoints(11.0));
    history.record(QStringLiteral("Move A"), twoPoints(12.0));
    history.undo();
    history.undo();

    QVERIFY(history.record(QStringLiteral("Move A elsewhere"), twoPoints(50.0)));
    QVERIFY(!history.canRedo());
    QCOMPARE(history.undoCount(), 1);
    QCOMPARE(xOfA(history.undo()), 10.0);
}

void TestEditHistory::anEditThatChangedNothingIsNotAStep()
{
    EditHistory history;
    history.reset(twoPoints(10.0));
    QVERIFY(!history.record(QStringLiteral("Move A"), twoPoints(10.0)));
    QVERIFY(!history.canUndo());

    // Nor does an unchanged state throw away a redo the user may still want.
    history.record(QStringLiteral("Move A"), twoPoints(11.0));
    history.undo();
    QVERIFY(!history.record(QStringLiteral("Move A"), twoPoints(10.0)));
    QVERIFY(history.canRedo());
}

void TestEditHistory::mirrorProvenanceAndConfigurationAndAnglesAreState()
{
    const EditState plain = twoPoints();

    EditState mirrored = plain;
    mirrored.table.points[1].mirrorOf = QStringLiteral("A");
    QVERIFY(mirrored != plain);

    EditState configured = plain;
    configured.config.insert(QStringLiteral("A"), HardpointConfig{ PointType::Solved, {}, {} });
    QVERIFY(configured != plain);

    EditState aligned = plain;
    aligned.alignment.insert(QStringLiteral("F"), StaticAlignment{ -1.5, 0.1 });
    QVERIFY(aligned != plain);

    EditState reordered = plain;
    std::swap(reordered.table.points[0], reordered.table.points[1]);
    QVERIFY(reordered != plain);

    QVERIFY(twoPoints() == plain);
}

void TestEditHistory::aResetForgetsEveryStep()
{
    EditHistory history;
    history.reset(twoPoints(10.0));
    history.record(QStringLiteral("Move A"), twoPoints(11.0));
    history.record(QStringLiteral("Move A"), twoPoints(12.0));
    history.undo();

    history.reset(twoPoints(99.0));
    QVERIFY(!history.canUndo());
    QVERIFY(!history.canRedo());
    QCOMPARE(xOfA(history.current()), 99.0);
}

void TestEditHistory::theLimitDropsTheOldestSteps()
{
    EditHistory history(3);
    history.reset(twoPoints(0.0));
    for (int step = 1; step <= 5; ++step)
        history.record(QStringLiteral("Move A to %1").arg(step), twoPoints(step));

    QCOMPARE(history.undoCount(), 3);
    history.undo();
    history.undo();
    QCOMPARE(xOfA(history.undo()), 2.0);
    QVERIFY(!history.canUndo());
    QVERIFY(history.undoLabel().isEmpty());
    // Every step kept can still be done again.
    QCOMPARE(history.redoCount(), 3);
    QCOMPARE(history.redoLabel(), QStringLiteral("Move A to 3"));
}

void TestEditHistory::undoingPastTheFirstStateStaysThere()
{
    EditHistory history;
    history.reset(twoPoints(10.0));
    QCOMPARE(xOfA(history.undo()), 10.0);
    QCOMPARE(xOfA(history.redo()), 10.0);
    QCOMPARE(history.undoCount(), 0);
    QCOMPARE(history.redoCount(), 0);
}

void TestEditHistory::aRelabelledBodyIsRenamedInEveryState()
{
    EditState first = twoPoints(10.0);
    first.config.insert(QStringLiteral("A"),
                        HardpointConfig{ PointType::Solved, QStringLiteral("Upper wishbone"),
                                         QStringLiteral("Upright") });
    EditState second = first;
    second.table.points[0].coord[0] = 11.0;

    EditHistory history;
    history.reset(first);
    history.record(QStringLiteral("Move A"), second);
    history.renameBody(QStringLiteral("Upper wishbone"), QStringLiteral("UCA"));

    QCOMPARE(history.current().config.value(QStringLiteral("A")).part1, QStringLiteral("UCA"));
    QCOMPARE(history.undo().config.value(QStringLiteral("A")).part1, QStringLiteral("UCA"));
    QCOMPARE(history.current().config.value(QStringLiteral("A")).part2, QStringLiteral("Upright"));
}

void TestEditHistory::aRenameIsReadOffTheRowItHappenedIn()
{
    const EditState before = twoPoints();
    EditState after = before;
    after.table.points[1].name = QStringLiteral("B2");

    const QHash<QString, QString> renamed = renamedPoints(before.table, after.table);
    QCOMPARE(renamed.size(), 1);
    QCOMPARE(renamed.value(QStringLiteral("B")), QStringLiteral("B2"));
    // And back, for an undo of it.
    QCOMPARE(renamedPoints(after.table, before.table).value(QStringLiteral("B2")),
             QStringLiteral("B"));
}

void TestEditHistory::anythingOtherThanARenameIsNotOne()
{
    const EditState before = twoPoints();

    EditState moved = before;
    moved.table.points[0].coord[2] = 5.0;
    QVERIFY(renamedPoints(before.table, moved.table).isEmpty());

    // A point deleted from the middle shifts every row under it; none of them
    // was renamed.
    EditState deleted = before;
    deleted.table.points.erase(deleted.table.points.begin());
    QVERIFY(renamedPoints(before.table, deleted.table).isEmpty());

    EditState added = before;
    added.table.points.insert(added.table.points.begin(), point(QStringLiteral("C"), 10.0));
    QVERIFY(renamedPoints(before.table, added.table).isEmpty());
}

void TestEditHistory::theTouchedPointsAreWhatTheStepChanged()
{
    EditState before = twoPoints();
    before.table.points.push_back(point(QStringLiteral("C"), 30.0));
    before.table.points.push_back(point(QStringLiteral("D"), 40.0));

    EditState after = before;
    after.table.points[0].coord[1] = 1.0;                         // A moved
    after.table.points[2].name = QStringLiteral("C2");            // C renamed
    after.config.insert(QStringLiteral("D"), HardpointConfig{ PointType::ToBody, {}, {} });
    after.table.points.push_back(point(QStringLiteral("E"), 50.0)); // E added

    QCOMPARE(touchedPoints(before, after),
             (QStringList{ QStringLiteral("A"), QStringLiteral("C2"), QStringLiteral("D"),
                           QStringLiteral("E") }));
    // Going back, what comes back is touched and what goes away is not there
    // to select.
    QCOMPARE(touchedPoints(after, before),
             (QStringList{ QStringLiteral("A"), QStringLiteral("C"), QStringLiteral("D") }));
    QVERIFY(touchedPoints(before, before).isEmpty());
}

QTEST_APPLESS_MAIN(TestEditHistory)
#include "test_edit_history.moc"
