#include "app/framework/RibbonPages.h"

#include "app/Ribbon.h"
#include "app/framework/CommandRegistry.h"

#include <QAction>
#include <QCoreApplication>

#include <algorithm>

namespace suspkin {
namespace {

QString tr(const char* text) { return QCoreApplication::translate("Ribbon", text); }

/// The commands on one page, in the order they asked to appear.
std::vector<RegisteredCommand> commandsOnPage(const CommandRegistry& commands, const QString& key)
{
    std::vector<RegisteredCommand> onPage;
    for (const RegisteredCommand& command : commands.placements())
        if (command.ribbon.page == key) onPage.push_back(command);

    // Stable, so two commands that asked for the same place keep the order
    // their features registered them in rather than swapping about between
    // builds.
    std::stable_sort(onPage.begin(), onPage.end(),
                     [](const RegisteredCommand& a, const RegisteredCommand& b) {
                         return a.ribbon.order < b.ribbon.order;
                     });
    return onPage;
}

/// Fill @p page in. Groups appear in the order of their earliest command, which
/// is what lets a feature place a whole group by numbering its commands.
void fillPage(RibbonPage* page, const std::vector<RegisteredCommand>& commands)
{
    RibbonGroup* group = nullptr;
    QString openGroup;
    for (const RegisteredCommand& command : commands) {
        if (!group || command.ribbon.group != openGroup) {
            group = page->addGroup(command.ribbon.group);
            openGroup = command.ribbon.group;
        }
        switch (command.ribbon.button) {
        case RibbonButton::Small: group->addSmall(command.action); break;
        case RibbonButton::Large: group->addLarge(command.action); break;
        case RibbonButton::Split: group->addSplit(command.action, command.ribbon.menu); break;
        }
    }
}

/// The strip beside the tabs. Separators are not commands, so the one between
/// the panels and the chevron is put in by order: everything before 100 is a
/// panel, everything after it is not.
void addTrailingCommands(Ribbon* ribbon, const CommandRegistry& commands)
{
    bool separated = false;
    for (const RegisteredCommand& command : commandsOnPage(commands, ribbonTrailingPage())) {
        if (!separated && command.ribbon.order >= 100) {
            ribbon->addTrailingSeparator();
            separated = true;
        }
        ribbon->addTrailingAction(command.action);
    }
}

} // namespace

const std::vector<RibbonPageSpec>& ribbonPages()
{
    // Alt+P for Help, not Alt+H: the Hardpoints tab has that, and two tabs
    // sharing a mnemonic means neither of them answers to it.
    static const std::vector<RibbonPageSpec> pages = {
        { QStringLiteral("geometry"), tr("&Geometry") },
        { QStringLiteral("hardpoints"), tr("&Hardpoints") },
        { QStringLiteral("linkage"), tr("&Linkage") },
        { QStringLiteral("analysis"), tr("&Analysis") },
        { QStringLiteral("view"), tr("&View") },
        { QStringLiteral("help"), tr("Hel&p") },
    };
    return pages;
}

void buildRibbonFrom(Ribbon* ribbon, const CommandRegistry& commands)
{
    for (const RibbonPageSpec& spec : ribbonPages()) {
        const std::vector<RegisteredCommand> onPage = commandsOnPage(commands, spec.key);
        // A page no feature put anything on is not drawn: an empty tab is a
        // question the user cannot answer.
        if (onPage.empty()) continue;
        fillPage(ribbon->addPage(spec.key, spec.title), onPage);
    }
    addTrailingCommands(ribbon, commands);
}

} // namespace suspkin
