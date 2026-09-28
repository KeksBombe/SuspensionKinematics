#include "app/DockDragEnd.h"

#include <QDockWidget>
#include <QLabel>
#include <QMainWindow>
#include <QMouseEvent>
#include <QTest>

using namespace suspkin;

// Offscreen is a platform Qt gives floating docks a native frame on, as it
// does on Windows, so the events here take the same path through QDockWidget
// that a drag on a Windows title bar takes: a press on the frame, the window
// moved by the system's move loop, and a release Qt synthesises when that
// loop ends -- with no move after it.

namespace {

/// A point on a floating dock's native title bar, in the dock's own
/// coordinates: just above its client area.
const QPoint kOnTitleBar(20, -1);
constexpr int kMoveSteps = 10;

/// A main window with one dock on its right, allowed on the left and the right
/// -- the hardpoint panel's arrangement.
struct DockedPanel {
    QMainWindow window;
    QDockWidget* dock = nullptr;

    DockedPanel()
    {
        window.setCentralWidget(new QLabel(QStringLiteral("centre")));
        window.resize(1000, 700);
        // No animation: where a drop settles is then there when the call returns.
        window.setAnimated(false);

        dock = new QDockWidget(QStringLiteral("Panel"), &window);
        dock->setObjectName(QStringLiteral("panel"));
        dock->setWidget(new QLabel(QStringLiteral("panel")));
        dock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
        window.addDockWidget(Qt::RightDockWidgetArea, dock);
        endNativeDockDragOnRelease(dock);
    }

    bool showFloating()
    {
        window.show();
        if (!QTest::qWaitForWindowExposed(&window)) return false;
        dock->setFloating(true);
        dock->move(window.mapToGlobal(QPoint(300, 300)));
        QCoreApplication::processEvents();
        return dock->isFloating();
    }

    void sendFrameEvent(QEvent::Type type, Qt::MouseButton button, Qt::MouseButtons buttons)
    {
        QMouseEvent event(type, QPointF(kOnTitleBar), QPointF(dock->mapToGlobal(kOnTitleBar)),
                          button, buttons, Qt::NoModifier);
        QCoreApplication::sendEvent(dock, &event);
    }

    /// Take the dock by its native title bar and move it until the pointer is
    /// at @p windowPoint in the main window, as the system's move loop would:
    /// only the window moves, no pointer events arrive. Not let go.
    void dragTitleBarTo(QPoint windowPoint)
    {
        sendFrameEvent(QEvent::NonClientAreaMouseButtonPress, Qt::LeftButton, Qt::LeftButton);
        const QPoint start = dock->pos();
        const QPoint end = window.mapToGlobal(windowPoint) - kOnTitleBar;
        for (int step = 1; step <= kMoveSteps; ++step) {
            dock->move(start + (end - start) * step / kMoveSteps);
            QCoreApplication::processEvents();
        }
    }

    /// The release Qt synthesises when the move loop ends.
    void letGo()
    {
        sendFrameEvent(QEvent::NonClientAreaMouseButtonRelease, Qt::LeftButton, Qt::NoButton);
        QCoreApplication::processEvents();
    }
};

} // namespace

class TestDockRedock : public QObject {
    Q_OBJECT

private slots:
    void lettingGoOverAnEdgeDocksThePanel();
    void lettingGoOverTheOtherEdgeDocksItThere();
    void lettingGoAwayFromEveryEdgeLeavesItFloating();
    void doubleClickingTheTitleBarDocksThePanel();
    void theDockedTitleBarStillDragsThePanelOut();
};

void TestDockRedock::lettingGoOverAnEdgeDocksThePanel()
{
    DockedPanel panel;
    QVERIFY(panel.showFloating());

    panel.dragTitleBarTo(QPoint(panel.window.width() - 5, 350));
    panel.letGo();

    // Docked on the release itself -- not left floating over the gap until a
    // move that, on Windows, may never reach the dock's title bar.
    QVERIFY(!panel.dock->isFloating());
    QCOMPARE(panel.window.dockWidgetArea(panel.dock), Qt::RightDockWidgetArea);
}

void TestDockRedock::lettingGoOverTheOtherEdgeDocksItThere()
{
    DockedPanel panel;
    QVERIFY(panel.showFloating());

    panel.dragTitleBarTo(QPoint(5, 350));
    panel.letGo();

    QVERIFY(!panel.dock->isFloating());
    QCOMPARE(panel.window.dockWidgetArea(panel.dock), Qt::LeftDockWidgetArea);
}

void TestDockRedock::lettingGoAwayFromEveryEdgeLeavesItFloating()
{
    DockedPanel panel;
    QVERIFY(panel.showFloating());

    panel.dragTitleBarTo(QPoint(panel.window.width() / 2, 350));
    panel.letGo();

    QVERIFY(panel.dock->isFloating());
    // And the drag is over: a later move over the title bar docks nothing.
    panel.sendFrameEvent(QEvent::NonClientAreaMouseMove, Qt::NoButton, Qt::NoButton);
    QCoreApplication::processEvents();
    QVERIFY(panel.dock->isFloating());
}

void TestDockRedock::doubleClickingTheTitleBarDocksThePanel()
{
    DockedPanel panel;
    QVERIFY(panel.showFloating());

    // Windows' own order: the first click's release is swallowed by the move
    // loop and synthesised after it, the second press arrives as a press, and
    // Qt follows it with the double-click.
    panel.sendFrameEvent(QEvent::NonClientAreaMouseButtonPress, Qt::LeftButton, Qt::LeftButton);
    panel.letGo();
    panel.sendFrameEvent(QEvent::NonClientAreaMouseButtonPress, Qt::LeftButton, Qt::LeftButton);
    panel.sendFrameEvent(QEvent::NonClientAreaMouseButtonDblClick, Qt::LeftButton,
                         Qt::LeftButton);
    panel.letGo();

    QVERIFY(!panel.dock->isFloating());
    QCOMPARE(panel.window.dockWidgetArea(panel.dock), Qt::RightDockWidgetArea);
}

void TestDockRedock::theDockedTitleBarStillDragsThePanelOut()
{
    DockedPanel panel;
    panel.window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&panel.window));
    QVERIFY(!panel.dock->isFloating());

    // Docked, the title bar is Qt's own and the drag is ordinary mouse events;
    // nothing here may end it early.
    const QPoint onTitle(20, 8);
    QTest::mousePress(panel.dock, Qt::LeftButton, Qt::NoModifier, onTitle);
    QTest::mouseMove(panel.dock, onTitle + QPoint(40, 40));
    QTest::mouseMove(panel.dock, QPoint(-400, 200));
    QTest::mouseRelease(panel.dock, Qt::LeftButton, Qt::NoModifier, QPoint(-400, 200));
    QCoreApplication::processEvents();

    QVERIFY(panel.dock->isFloating());
}

QTEST_MAIN(TestDockRedock)
#include "test_dock_redock.moc"
