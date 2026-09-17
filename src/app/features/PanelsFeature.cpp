#include "app/AnalysisPanel.h"
#include "app/Icons.h"
#include "app/PanelAction.h"
#include "app/Ribbon.h"
#include "app/framework/AppContext.h"
#include "app/framework/CommandRegistry.h"
#include "app/framework/Feature.h"
#include "app/framework/FeatureRegistry.h"
#include "app/framework/WindowActions.h"

#include <QCoreApplication>
#include <QKeySequence>

namespace suspkin {
namespace {

QString tr(const char* text) { return QCoreApplication::translate("PanelsFeature", text); }

/// Opening, closing and arranging the panels.
///
/// Panel toggles are not the docks' own toggleViewAction(): that one closes a
/// panel tabbed behind another when the user was reaching for it. And the
/// shortcuts are on these alone -- a key on two actions fires neither. Being a
/// QAction subclass with behaviour of its own, a PanelAction is made here and
/// adopted rather than described.
class PanelsFeature : public Feature {
public:
    explicit PanelsFeature(AppContext& context) : m_context(context) {}

    QString id() const override { return QStringLiteral("panels"); }

    void registerCommands(CommandRegistry& commands) override
    {
        registerPanelToggles(commands);
        registerLayoutCommands(commands);
    }

    void windowReady() override
    {
        QWidget* window = m_context.window();
        AnalysisPanel* analysis = m_context.analysisPanel();

        QObject::connect(analysis, &AnalysisPanel::parametersVisibilityChanged, m_parameters,
                         &PanelAction::sync);
        // Whichever way it opened or closed -- the button, its own Close,
        // Escape -- it is the user's arrangement, and the project keeps it.
        QObject::connect(analysis, &AnalysisPanel::parametersVisibilityChanged, window,
                         [this] { m_context.markDirty(); });

        Ribbon* ribbon = m_context.ribbon();
        QObject::connect(ribbon, &Ribbon::collapsedChanged, window, [this] { showCollapseState(); });
        showCollapseState();
    }

private:
    void registerPanelToggles(CommandRegistry& commands)
    {
        const QString panels = tr("Panels");
        const QString view = QStringLiteral("view");

        auto* table = new PanelAction(tr("Show Hardpoint &Table"), m_context.hardpointDock(),
                                      m_context.window());
        table->setShortcut(QKeySequence(QStringLiteral("Ctrl+H")));
        table->setIcon(Icons::get(Icon::Table));
        table->setIconVisibleInMenu(false);
        table->setIconText(tr("Table"));
        table->setStatusTip(
            tr("Open the table of hardpoints, bring it to the front, or close it."));
        commands.adopt(table, QStringLiteral("panels.hardpoints"),
                       { { QStringLiteral("hardpoints"), tr("Show"), RibbonButton::Small, nullptr,
                           110 },
                         { view, panels, RibbonButton::Small, nullptr, 80 },
                         { ribbonTrailingPage(), {}, RibbonButton::Small, nullptr, 10 } });

        auto* analysis = new PanelAction(tr("Show &Analysis"), m_context.analysisDock(),
                                         m_context.window());
        analysis->setShortcut(QKeySequence(QStringLiteral("Ctrl+K")));
        analysis->setIcon(Icons::get(Icon::ChartLine));
        analysis->setIconVisibleInMenu(false);
        analysis->setIconText(tr("Analysis"));
        analysis->setStatusTip(
            tr("Open the analysis panel -- the bump, roll and steer sweeps and their curves -- "
               "bring it to the front, or close it."));
        commands.adopt(analysis, QStringLiteral("panels.analysis"),
                       { { QStringLiteral("analysis"), tr("Panel"), RibbonButton::Large, nullptr,
                           10 },
                         { view, panels, RibbonButton::Small, nullptr, 90 },
                         { ribbonTrailingPage(), {}, RibbonButton::Small, nullptr, 20 } });

        // A window of its own rather than a dock, kept above this one, so when
        // it is open it is in front.
        PanelAction::Target parameters;
        parameters.isOpen = [this] { return m_context.analysisPanel()->parametersVisible(); };
        parameters.isOnScreen = parameters.isOpen;
        parameters.open = [this] { m_context.analysisPanel()->showParameters(); };
        parameters.close = [this] { m_context.analysisPanel()->hideParameters(); };
        m_parameters =
            new PanelAction(tr("Show Sweep &Parameters"), parameters, m_context.window());
        m_parameters->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+P")));
        m_parameters->setIcon(Icons::get(Icon::AdjustmentsHorizontal));
        m_parameters->setIconVisibleInMenu(false);
        m_parameters->setIconText(tr("Sweep\nParameters"));
        m_parameters->setStatusTip(
            tr("How far each sweep travels and how finely it is solved, in a window that can "
               "stay open beside the viewport."));
        commands.adopt(m_parameters, QStringLiteral("panels.parameters"),
                       { { QStringLiteral("analysis"), tr("Sweep"), RibbonButton::Large, nullptr,
                           20 },
                         { view, panels, RibbonButton::Small, nullptr, 100 },
                         { ribbonTrailingPage(), {}, RibbonButton::Small, nullptr, 30 } });
    }

    void registerLayoutCommands(CommandRegistry& commands)
    {
        auto& actions = *m_context.windowActions();

        commands.add({
            .id = QStringLiteral("panels.resetLayout"),
            .text = tr("Reset Panel &Layout"),
            .icon = Icon::LayoutDashboard,
            .iconText = tr("Reset Layout"),
            .statusTip = tr("Dock every panel where a new project has it, keeping open the ones "
                            "that are open. Brings back a panel left on a monitor that is not "
                            "connected."),
            .ribbon = { { QStringLiteral("view"), tr("Panels"), RibbonButton::Small, nullptr,
                          110 } },
            .run = [&actions] { actions.resetPanelLayout(); },
        });

        // Its text and icon say what it will do, and follow the ribbon: see
        // showCollapseState(). Order 100 or more puts it past the separator.
        m_collapse = commands.add({
            .id = QStringLiteral("panels.collapseRibbon"),
            .text = tr("Collapse the &Ribbon"),
            .icon = Icon::ChevronUp,
            .shortcut = QKeySequence(QStringLiteral("Ctrl+F1")),
            .ribbon = { { ribbonTrailingPage(), {}, RibbonButton::Small, nullptr, 110 } },
            .run = [this] { m_context.ribbon()->setCollapsed(!m_context.ribbon()->collapsed()); },
        });
    }

    /// Make the chevron say what pressing it will do.
    void showCollapseState()
    {
        const bool collapsed = m_context.ribbon() && m_context.ribbon()->collapsed();
        m_collapse->setText(collapsed ? tr("Expand the &Ribbon") : tr("Collapse the &Ribbon"));
        m_collapse->setIcon(Icons::get(collapsed ? Icon::ChevronDown : Icon::ChevronUp));
        m_collapse->setStatusTip(collapsed
                                     ? tr("Show the ribbon's buttons under its tabs again.")
                                     : tr("Keep only the ribbon's tabs. Clicking one brings its "
                                          "buttons back."));
        m_collapse->setToolTip(commandToolTip(m_collapse));
    }

    AppContext& m_context;
    PanelAction* m_parameters = nullptr;
    QAction* m_collapse = nullptr;
};

} // namespace

SUSPKIN_FEATURE(PanelsFeature)

} // namespace suspkin
