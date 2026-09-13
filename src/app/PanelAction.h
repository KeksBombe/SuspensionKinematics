#pragma once

#include <QAction>

#include <functional>

class QDockWidget;

namespace suspkin {

/// Opens, raises or closes a panel -- a dock, or a window of its own -- in the
/// way someone reaching for it means.
///
/// A dock's own toggleViewAction() does the wrong thing when the dock is tabbed
/// behind another: it is still checked, because the dock is still open, so
/// clicking it *closes* the panel the user was trying to get to. This one asks
/// what is on screen instead:
///
/// - closed: show it, and bring it to the front;
/// - open but out of sight (tabbed behind another dock): bring it to the front;
/// - open and on screen: close it.
///
/// Checked means open, whether or not it is the tab in front, which is what the
/// menu's tick and the ribbon's highlight both want to say.
class PanelAction : public QAction {
    Q_OBJECT

public:
    /// What the action needs to know about, and do to, a panel that is not a
    /// dock. Every member is required.
    struct Target {
        /// Shown, even if something else is in front of it.
        std::function<bool()> isOpen;
        /// Shown and actually in front, where the user can see it.
        std::function<bool()> isOnScreen;
        /// Show it if it is not shown, and bring it to the front either way.
        std::function<void()> open;
        std::function<void()> close;
    };

    /// A panel that is not a dock. Whoever owns it calls sync() whenever it
    /// opens or closes by some other route, so the check mark follows.
    PanelAction(const QString& text, Target target, QObject* parent);

    /// A dock, followed by itself: the check mark tracks the dock's own
    /// toggleViewAction(), and whether it is in front is asked of the dock at
    /// the moment it is clicked.
    PanelAction(const QString& text, QDockWidget* dock, QObject* parent);

    /// Read the checked state from the panel again.
    void sync();

private:
    void act();

    Target m_target;
};

} // namespace suspkin
