#include "app/PanelAction.h"

#include <QDockWidget>
#include <QLabel>
#include <QMainWindow>
#include <QTest>

using namespace suspkin;

namespace {

/// A main window with two docks tabbed together in one area, both open.
struct TwoTabbedDocks {
    QMainWindow window;
    QDockWidget* front = nullptr;
    QDockWidget* behind = nullptr;

    TwoTabbedDocks()
    {
        window.setCentralWidget(new QLabel(QStringLiteral("centre")));
        window.resize(800, 600);
        // No animation: the geometry a tab switch settles on is then there as
        // soon as the call returns, which is what a test can look at.
        window.setAnimated(false);

        behind = new QDockWidget(QStringLiteral("Behind"), &window);
        behind->setObjectName(QStringLiteral("behind"));
        behind->setWidget(new QLabel(QStringLiteral("behind")));
        front = new QDockWidget(QStringLiteral("Front"), &window);
        front->setObjectName(QStringLiteral("front"));
        front->setWidget(new QLabel(QStringLiteral("front")));
        window.addDockWidget(Qt::RightDockWidgetArea, behind);
        window.tabifyDockWidget(behind, front);
    }

    bool show()
    {
        window.show();
        if (!QTest::qWaitForWindowExposed(&window)) return false;
        front->raise();
        QCoreApplication::processEvents();
        return true;
    }
};

} // namespace

class TestPanelAction : public QObject {
    Q_OBJECT

private slots:
    void aDockBehindAnotherIsBroughtForwardNotClosed();
    void theCheckFollowsTheDocksOwnCloseButton();
    void aPanelThatIsNotADockGoesThroughItsTarget();
};

void TestPanelAction::aDockBehindAnotherIsBroughtForwardNotClosed()
{
    TwoTabbedDocks docks;
    QVERIFY(docks.show());

    PanelAction action(QStringLiteral("Behind"), docks.behind, nullptr);
    QVERIFY(action.isCheckable());

    // Open, but out of sight behind the other tab. Its own toggleViewAction()
    // says checked here, and triggering that would close it.
    QVERIFY(docks.behind->toggleViewAction()->isChecked());
    QVERIFY(action.isChecked());

    // Brought to the front, and still open.
    action.trigger();
    QCoreApplication::processEvents();
    QVERIFY(!docks.behind->isHidden());
    QVERIFY(action.isChecked());
    QVERIFY(!docks.behind->visibleRegion().isEmpty());
    QVERIFY(docks.front->visibleRegion().isEmpty());

    // Now it is in front, so the same click closes it.
    action.trigger();
    QCoreApplication::processEvents();
    QVERIFY(docks.behind->isHidden());
    QVERIFY(!action.isChecked());

    // And once more opens it again, in front.
    action.trigger();
    QCoreApplication::processEvents();
    QVERIFY(!docks.behind->isHidden());
    QVERIFY(action.isChecked());
    QVERIFY(!docks.behind->visibleRegion().isEmpty());
}

void TestPanelAction::theCheckFollowsTheDocksOwnCloseButton()
{
    TwoTabbedDocks docks;
    QVERIFY(docks.show());
    PanelAction action(QStringLiteral("Front"), docks.front, nullptr);
    QVERIFY(action.isChecked());

    // Closed some other way -- the title bar's X, restoreState() -- the check
    // goes with it, and the next trigger opens rather than closes.
    docks.front->close();
    QVERIFY(!action.isChecked());
    action.trigger();
    QCoreApplication::processEvents();
    QVERIFY(!docks.front->isHidden());
    QVERIFY(action.isChecked());

    docks.front->toggleViewAction()->trigger();
    QVERIFY(docks.front->isHidden());
    QVERIFY(!action.isChecked());
}

void TestPanelAction::aPanelThatIsNotADockGoesThroughItsTarget()
{
    bool open = false;
    bool inFront = false;
    int opened = 0;
    int closed = 0;

    PanelAction::Target target;
    target.isOpen = [&] { return open; };
    target.isOnScreen = [&] { return open && inFront; };
    target.open = [&] {
        open = true;
        inFront = true;
        ++opened;
    };
    target.close = [&] {
        open = false;
        ++closed;
    };
    PanelAction action(QStringLiteral("Window"), target, nullptr);
    QVERIFY(!action.isChecked());

    action.trigger();
    QVERIFY(open);
    QVERIFY(action.isChecked());

    // Open but behind something: raised, not closed.
    inFront = false;
    action.trigger();
    QCOMPARE(opened, 2);
    QCOMPARE(closed, 0);
    QVERIFY(action.isChecked());

    action.trigger();
    QVERIFY(!open);
    QCOMPARE(closed, 1);
    QVERIFY(!action.isChecked());

    // Opened by some other route, then told about it.
    open = true;
    QVERIFY(!action.isChecked());
    action.sync();
    QVERIFY(action.isChecked());
}

QTEST_MAIN(TestPanelAction)
#include "test_panel_action.moc"
