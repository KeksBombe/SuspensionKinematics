#pragma once

#include <QString>

#include <functional>

class QMainWindow;
class QMenu;

namespace suspkin {

class CommandRegistry;
class Ribbon;

/// The File menu, which the ribbon's accent button opens: the project commands,
/// the recent projects, and the way out. It is the only menu the window has --
/// everything else is a tab. @p openRecent is called with the manifest of a
/// recent project the user picked; @p currentManifest is left off the list.
QMenu* buildFileMenu(QMainWindow* window, const CommandRegistry& commands,
                     const QString& currentManifest,
                     std::function<void(const QString& manifestPath)> openRecent);

/// The ribbon, built from where the commands asked to go and installed as the
/// window's menu widget. There is no menu bar above it.
Ribbon* buildRibbon(QMainWindow* window, QMenu* fileMenu, const CommandRegistry& commands);

/// Give every command its tooltip and put it on the window, so its shortcut
/// works whichever ribbon tab is showing.
void finishCommands(QMainWindow* window, const CommandRegistry& commands);

} // namespace suspkin
