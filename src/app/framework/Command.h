#pragma once

#include "app/Icons.h"

#include <QKeySequence>
#include <QString>

#include <functional>
#include <vector>

class QMenu;

namespace suspkin {

/// How a ribbon button is drawn.
enum class RibbonButton {
    Small, ///< icon and label side by side: the rest
    Large, ///< icon above the label: what a tab is for
    Split, ///< large, with an arrow under it that opens a menu of its own
};

/// Where a command appears on the ribbon.
///
/// The command says where it goes rather than a layout file naming the command,
/// so adding one touches only the feature it belongs to.
struct RibbonSlot {
    /// A page key from ribbonPages(). Empty means the command is not on the
    /// ribbon at all -- it lives in the File menu, or is a shortcut only.
    QString page;
    /// The group's title within that page. Groups appear in the order of their
    /// earliest command.
    QString group;
    RibbonButton button = RibbonButton::Small;
    /// What the arrow under a Split button opens. Ignored otherwise.
    QMenu* menu = nullptr;
    /// Ascending within a group, and what orders the groups themselves. Spaced
    /// in tens by convention, so a command can be put between two others
    /// without renumbering either.
    int order = 0;

    bool onRibbon() const { return !page.isEmpty(); }
};

/// The page key for the strip beside the tabs, which every tab shows: the
/// panels people reach for most, and the chevron that folds the ribbon away.
inline const QString& ribbonTrailingPage()
{
    static const QString key = QStringLiteral("@trailing");
    return key;
}

/// Everything one command is: what it says, where it appears, what it does, and
/// when it can be used.
///
/// All four in one place on purpose. Splitting them across a description table,
/// a layout file and a state function is what made the enabled state, the
/// tooltip and the button drift apart in the first place.
struct CommandSpec {
    /// Unique, dotted, and how anything else refers to this command --
    /// "hardpoints.addPoint". Never shown to the user.
    QString id;
    QString text;
    Icon icon = Icon::Count;
    /// The ribbon's shorter label, which menus never see. A \n in it is where a
    /// large button's label breaks.
    QString iconText;
    QKeySequence shortcut;
    QString statusTip;
    bool checkable = false;
    bool checkedByDefault = false;
    /// Everywhere it appears. Usually one place, sometimes two -- Show Parts is
    /// on both the Linkage tab and the View tab, and it is one command in two
    /// views, not two commands.
    std::vector<RibbonSlot> ribbon;

    /// What it does. For a checkable command this is not used -- @ref onToggled
    /// is, because what such a command carries is a state and not an event.
    std::function<void()> run;
    std::function<void(bool)> onToggled;

    /// Whether it can be used at all right now, asked again whenever anything
    /// changes. Nothing means always. The rule lives here, beside the command
    /// it governs, rather than in one function that has to know every command
    /// in the application.
    std::function<bool()> enabledWhen;
};

} // namespace suspkin
