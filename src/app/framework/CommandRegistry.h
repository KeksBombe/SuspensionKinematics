#pragma once

#include "app/framework/Command.h"

#include <QHash>
#include <QList>
#include <QString>

#include <vector>

class QAction;
class QObject;

namespace suspkin {

/// A command and where it asked to be put, for whoever lays out the ribbon.
struct RegisteredCommand {
    QAction* action = nullptr;
    RibbonSlot ribbon;
};

/// Every command the application has, however many features contributed them.
///
/// A command is one QAction whatever shows it, so the ribbon button, the menu
/// entry and the shortcut cannot disagree about whether it is on. What changed
/// is who owns the list: features add to this, and nothing has to hold a
/// complete list of commands to build the window.
class CommandRegistry {
public:
    explicit CommandRegistry(QObject* parent);

    /// Make @p spec's action, wire it to what the spec says it does, and keep
    /// it. Returns the action for the rare caller that needs it by hand.
    QAction* add(CommandSpec spec);

    /// Keep an action a feature made itself -- a PanelAction, which is a
    /// QAction subclass with behaviour of its own and so cannot come out of a
    /// description.
    QAction* adopt(QAction* action, const QString& id, std::vector<RibbonSlot> ribbon,
                   std::function<bool()> enabledWhen = {});

    /// The command @p id names, or nothing. How one feature reaches another's
    /// command without either including the other.
    QAction* action(const QString& id) const;

    /// Every command, in the order they were registered.
    const QList<QAction*>& all() const { return m_all; }
    /// Only those that said where they go on the ribbon.
    const std::vector<RegisteredCommand>& placements() const { return m_placements; }

    /// Ask every command whether it can be used, and set it so. Cheap -- a
    /// predicate each -- so anything that changes the window's state can call
    /// it rather than working out which commands it touched.
    void refreshEnabled() const;
    /// Ask every command whose name follows the state what it is called now,
    /// and rename it -- tooltip and all. Only the few that asked to be.
    void refreshText() const;

private:
    QObject* m_parent = nullptr;
    QList<QAction*> m_all;
    QHash<QString, QAction*> m_byId;
    std::vector<RegisteredCommand> m_placements;
    /// Kept beside the actions rather than on them: a QAction has nowhere to
    /// put a predicate.
    std::vector<std::pair<QAction*, std::function<bool()>>> m_rules;
    std::vector<std::pair<QAction*, std::function<QString()>>> m_texts;
};

} // namespace suspkin
