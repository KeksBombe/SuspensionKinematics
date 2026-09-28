#include "app/framework/WindowChrome.h"

#include "app/Icons.h"
#include "app/RecentProjects.h"
#include "app/Ribbon.h"
#include "app/framework/CommandRegistry.h"
#include "app/framework/RibbonPages.h"

#include <QAction>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QMainWindow>
#include <QMenu>

#include <utility>

namespace suspkin {
namespace {

QString tr(const char* text) { return QCoreApplication::translate("WindowChrome", text); }

/// Fill @p menu with every recent project that still exists, other than the one
/// open now.
void refreshRecentProjects(QMenu* menu, const QString& currentManifest,
                           const std::function<void(const QString&)>& openRecent)
{
    menu->clear();

    const QString current = QFileInfo(currentManifest).absoluteFilePath();
    int shown = 0;
    for (const RecentProject& entry : RecentProjects::load()) {
        if (QFileInfo(entry.manifestPath).absoluteFilePath() == current) continue;
        if (!entry.exists()) continue;
        QAction* action = menu->addAction(
            QStringLiteral("%1  -  %2").arg(entry.name, QDir::toNativeSeparators(entry.directory())));
        const QString path = entry.manifestPath;
        QObject::connect(action, &QAction::triggered, menu, [openRecent, path] { openRecent(path); });
        ++shown;
    }
    menu->setEnabled(shown > 0);
}

} // namespace

QMenu* buildFileMenu(QMainWindow* window, const CommandRegistry& commands,
                     const QString& currentManifest,
                     std::function<void(const QString& manifestPath)> openRecent)
{
    // There is no menu bar, and this is the only menu: the ribbon's File
    // button opens it, because what is in it -- the project itself, and the
    // way out -- is not a tab's worth of commands. Everything else, Help
    // included, is a tab. The mnemonic still works: Alt+F opens this.
    //
    // By id rather than by name: these commands belong to a feature the window
    // knows nothing else about.
    auto* fileMenu = new QMenu(tr("&File"), window);
    fileMenu->addAction(commands.action(QStringLiteral("project.new")));
    fileMenu->addAction(commands.action(QStringLiteral("project.open")));
    QMenu* recent = fileMenu->addMenu(tr("Open &Recent"));
    recent->setIcon(Icons::get(Icon::History));
    fileMenu->addSeparator();
    fileMenu->addAction(commands.action(QStringLiteral("project.save")));
    fileMenu->addAction(commands.action(QStringLiteral("project.list")));
    fileMenu->addAction(commands.action(QStringLiteral("project.reveal")));
    fileMenu->addSeparator();
    fileMenu->addAction(commands.action(QStringLiteral("project.quit")));

    const auto refresh = [recent, currentManifest, openRecent = std::move(openRecent)] {
        refreshRecentProjects(recent, currentManifest, openRecent);
    };
    QObject::connect(fileMenu, &QMenu::aboutToShow, recent, refresh);
    refresh();
    return fileMenu;
}

Ribbon* buildRibbon(QMainWindow* window, QMenu* fileMenu, const CommandRegistry& commands)
{
    auto* ribbon = new Ribbon;
    ribbon->setApplicationMenu(fileMenu, tr("&File"));
    buildRibbonFrom(ribbon, commands);

    // The ribbon is the whole of the window's chrome: there is no menu bar
    // above it, because the tabs said the same words the menus did.
    //
    // From here on QMainWindow::menuBar() must never be called. On a window
    // whose menu widget is not a QMenuBar it makes one and installs it through
    // setMenuWidget(), which deleteLater()s what was there -- the ribbon.
    window->setMenuWidget(ribbon);
    return ribbon;
}

void finishCommands(QMainWindow* window, const CommandRegistry& commands)
{
    // A ribbon button shows its action's tooltip, and Qt adds neither the
    // shortcut nor the status tip to one by itself.
    for (QAction* action : commands.all()) action->setToolTip(commandToolTip(action));

    // On the window itself as well as wherever it is shown: a shortcut is live
    // only while some widget the action is on is visible, and with the menu
    // bar hidden and its button on a tab that is not showing, Ctrl+I would
    // otherwise do nothing at all.
    window->addActions(commands.all());
}

} // namespace suspkin
