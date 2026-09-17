#include "app/framework/CommandRegistry.h"

#include "app/Ribbon.h"

#include <QAction>
#include <QObject>

namespace suspkin {

CommandRegistry::CommandRegistry(QObject* parent) : m_parent(parent) {}

QAction* CommandRegistry::add(CommandSpec spec)
{
    auto* action = new QAction(spec.text, m_parent);
    if (spec.icon != Icon::Count) action->setIcon(Icons::get(spec.icon));
    if (!spec.iconText.isEmpty()) action->setIconText(spec.iconText);
    if (!spec.shortcut.isEmpty()) action->setShortcut(spec.shortcut);
    if (!spec.statusTip.isEmpty()) action->setStatusTip(spec.statusTip);

    if (spec.checkable) {
        action->setCheckable(true);
        // Before the connection below, so a command that starts checked does
        // not report that as the user having just switched it on.
        action->setChecked(spec.checkedByDefault);
        // A menu marks a checkable entry with its tick or its radio button, not
        // with an icon, so the icon is for the ribbon alone.
        action->setIconVisibleInMenu(false);
        if (spec.onToggled) QObject::connect(action, &QAction::toggled, m_parent, spec.onToggled);
    }
    // A checkable command may have both: onToggled for the state it carries,
    // run for the act of pressing it -- which is what an exclusive group wants,
    // where pressing the one already down is still a choice.
    if (spec.run) QObject::connect(action, &QAction::triggered, m_parent, spec.run);
    if (spec.textWhen) m_texts.emplace_back(action, std::move(spec.textWhen));

    return adopt(action, spec.id, std::move(spec.ribbon), std::move(spec.enabledWhen));
}

QAction* CommandRegistry::adopt(QAction* action, const QString& id, std::vector<RibbonSlot> ribbon,
                                std::function<bool()> enabledWhen)
{
    m_all << action;
    m_byId.insert(id, action);
    for (RibbonSlot& slot : ribbon)
        if (slot.onRibbon()) m_placements.push_back(RegisteredCommand{ action, std::move(slot) });
    if (enabledWhen) {
        // Asked once now, so a command that starts unusable starts that way
        // rather than for the length of one event loop turn.
        action->setEnabled(enabledWhen());
        m_rules.emplace_back(action, std::move(enabledWhen));
    }
    return action;
}

QAction* CommandRegistry::action(const QString& id) const
{
    return m_byId.value(id, nullptr);
}

void CommandRegistry::refreshEnabled() const
{
    for (const auto& [action, rule] : m_rules) action->setEnabled(rule());
}

void CommandRegistry::refreshText() const
{
    for (const auto& [action, text] : m_texts) {
        const QString now = text();
        if (now == action->text()) continue;
        action->setText(now);
        // A tool button shows the tooltip, which was built from the old name.
        action->setToolTip(commandToolTip(action));
    }
}

} // namespace suspkin
