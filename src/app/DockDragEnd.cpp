#include "app/DockDragEnd.h"

#include <QCoreApplication>
#include <QDockWidget>
#include <QMouseEvent>

namespace suspkin {
namespace {

bool isLeftButtonReleaseOnFrame(const QEvent& event)
{
    return event.type() == QEvent::NonClientAreaMouseButtonRelease
           && static_cast<const QMouseEvent&>(event).button() == Qt::LeftButton;
}

/// Watches one dock for the release that ends a drag on its native title bar.
class NativeDragEnder final : public QObject {
public:
    explicit NativeDragEnder(QDockWidget* dock) : QObject(dock), m_dock(dock) {}

    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (watched == m_dock && isLeftButtonReleaseOnFrame(*event))
            endDragAt(static_cast<const QMouseEvent&>(*event));
        return false; // the release itself still goes to the dock
    }

private:
    /// A non-client move is what QDockWidget ends a native-frame drag on, and
    /// all it does with one when no such drag is under way is nothing -- so it
    /// is handed one, at the release's own position.
    void endDragAt(const QMouseEvent& release)
    {
        QMouseEvent move(QEvent::NonClientAreaMouseMove, release.position(),
                         release.scenePosition(), release.globalPosition(), Qt::NoButton,
                         Qt::NoButton, release.modifiers());
        QCoreApplication::sendEvent(m_dock, &move);
    }

    QDockWidget* m_dock;
};

} // namespace

void endNativeDockDragOnRelease(QDockWidget* dock)
{
    dock->installEventFilter(new NativeDragEnder(dock));
}

} // namespace suspkin
