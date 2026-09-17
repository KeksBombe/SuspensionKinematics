#pragma once

#include <QString>

namespace suspkin {

class AppContext;
class CommandRegistry;
struct ViewState;

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

    /// Write this feature's share of the view into @p view, which the project
    /// saves. A feature with any state of its own keeps it this way: persisting
    /// it is part of the feature, not somebody else's follow-up.
    virtual void collectViewState(ViewState& /*view*/) const {}
    /// Put this feature's share of @p view back, while the project is opening.
    /// Every feature is asked, in no particular order, so no feature's share
    /// may depend on another's having been restored first.
    virtual void applyViewState(const ViewState& /*view*/) {}
};

} // namespace suspkin
