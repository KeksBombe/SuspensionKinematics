#pragma once

#include <QString>

namespace suspkin {

class AppContext;
class CommandRegistry;

/// One area of the application: its commands, and the work behind them.
///
/// A feature is handed the context at construction and registers what it offers;
/// the window never names it. Adding an area of the application is therefore a
/// new file under src/app/features/ and nothing else -- see FeatureRegistry.
class Feature {
public:
    virtual ~Feature();

    /// For diagnostics and for a feature that needs to find another. Stable,
    /// lower case, not shown to the user.
    virtual QString id() const = 0;

    /// Add this feature's commands. Called once, before the ribbon is built, so
    /// anything registered here has somewhere to appear.
    virtual void registerCommands(CommandRegistry& commands) = 0;

    /// The window is up and its project is loaded. Where a feature does the
    /// work it would otherwise have to do in a constructor that is too early.
    virtual void windowReady() {}
};

} // namespace suspkin
