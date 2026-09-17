#pragma once

#include <QString>

#include <vector>

namespace suspkin {

class CommandRegistry;
class Ribbon;

/// A tab: the key a project stores, and the title it shows.
struct RibbonPageSpec {
    /// Stored in the project, so it must not change. Not shown.
    QString key;
    /// Shown, and translatable. A & marks its mnemonic; no two may share one,
    /// or neither tab answers to it.
    QString title;
};

/// The tabs, in the order they appear.
///
/// The one list that adding a *tab* touches. Adding a *command* to an existing
/// tab touches only the feature that command belongs to -- the command says
/// which page and group it wants, and the ribbon is built from what the
/// commands asked for.
const std::vector<RibbonPageSpec>& ribbonPages();

/// Build every page and group out of where the registered commands said they go.
void buildRibbonFrom(Ribbon* ribbon, const CommandRegistry& commands);

} // namespace suspkin
