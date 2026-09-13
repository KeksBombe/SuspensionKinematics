#include "app/PanelAction.h"

#include <QDockWidget>
#include <QPointer>

namespace suspkin {

PanelAction::PanelAction(const QString& text, Target target, QObject* parent)
    : QAction(text, parent), m_target(std::move(target))
{
    setCheckable(true);
    connect(this, &QAction::triggered, this, &PanelAction::act);
    sync();
}

PanelAction::PanelAction(const QString& text, QDockWidget* dock, QObject* parent)
    : QAction(text, parent)
{
    setCheckable(true);

    QPointer<QDockWidget> guard(dock);
    m_target.isOpen = [guard] { return guard && guard->toggleViewAction()->isChecked(); };
    // Whether the dock is the tab in front. isVisible() cannot say: a dock
    // tabbed behind another is not hidden but moved out of the window, where
    // none of it can be seen -- which is what visibleRegion() reports. A
    // floating dock is its own window, and all of it is visible.
    m_target.isOnScreen = [guard] {
        return guard && guard->toggleViewAction()->isChecked() && guard->isVisible()
               && !guard->visibleRegion().isEmpty();
    };
    m_target.open = [guard] {
        if (!guard) return;
        guard->show();
        guard->raise();
    };
    m_target.close = [guard] {
        if (guard) guard->close();
    };

    // The dock's own action is the truth about open and closed: restoreState(),
    // its title bar's X and anything else that shows or hides it all go
    // through it.
    connect(dock->toggleViewAction(), &QAction::toggled, this, [this] { sync(); });
    connect(this, &QAction::triggered, this, &PanelAction::act);
    sync();
}

void PanelAction::sync()
{
    if (!m_target.isOpen) return;
    setChecked(m_target.isOpen());
}

void PanelAction::act()
{
    // A checkable action has already flipped itself by the time triggered()
    // arrives, so what it says now is not what the panel is. The panel is
    // asked instead, and the check put right afterwards.
    if (!m_target.isOpen()) {
        m_target.open();
    } else if (!m_target.isOnScreen()) {
        m_target.open();
    } else {
        m_target.close();
    }
    sync();
}

} // namespace suspkin
