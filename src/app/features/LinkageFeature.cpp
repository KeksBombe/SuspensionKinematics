#include "app/framework/AppContext.h"
#include "app/framework/CommandRegistry.h"
#include "app/framework/Feature.h"
#include "app/framework/FeatureRegistry.h"
#include "app/framework/WindowActions.h"
#include "io/LinkageTemplate.h"
#include "model/Linkage.h"
#include "model/Simulation.h"
#include "render/ViewportWidget.h"

#include <QCoreApplication>
#include <QKeySequence>

namespace suspkin {
namespace {

QString tr(const char* text) { return QCoreApplication::translate("LinkageFeature", text); }

/// What is drawn between the hardpoints, and what the solver makes of it: the
/// template's parts, the steering rack, and the static angles.
class LinkageFeature : public Feature {
public:
    explicit LinkageFeature(AppContext& context) : m_context(context) {}

    QString id() const override { return QStringLiteral("linkage"); }

    void registerCommands(CommandRegistry& commands) override
    {
        registerPartCommands(commands);
        registerMechanismCommands(commands);
        registerTemplateCommands(commands);
    }

private:
    void registerPartCommands(CommandRegistry& commands)
    {
        auto& actions = *m_context.windowActions();
        const QString page = QStringLiteral("linkage");

        commands.add({
            .id = QStringLiteral("linkage.showParts"),
            .text = tr("Show &Parts"),
            .icon = Icon::Vector,
            .iconText = tr("Parts"),
            .shortcut = QKeySequence(QStringLiteral("Ctrl+P")),
            .statusTip =
                tr("Draw the wishbones, rods and bodies the linkage template describes."),
            .checkable = true,
            .checkedByDefault = true,
            .ribbon = { { page, tr("Show"), RibbonButton::Large, nullptr, 10 },
                        { QStringLiteral("view"), tr("Show"), RibbonButton::Small, nullptr, 60 } },
            .onToggled = [this](bool on) { m_context.viewport()->setLinkageVisible(on); },
            .enabledWhen = [this] { return !m_context.linkage().isEmpty(); },
        });

        commands.add({
            .id = QStringLiteral("linkage.newPart"),
            .text = tr("&New Part from Selection..."),
            .icon = Icon::Line,
            .iconText = tr("New Part"),
            .statusTip =
                tr("Draw a part through the selected points, in the order they were picked."),
            .ribbon = { { page, tr("Parts"), RibbonButton::Small, nullptr, 20 } },
            .run = [&actions] { actions.newPartFromSelection(); },
            .enabledWhen = [this] {
                return templateLoaded() && m_context.viewport()->selectedHardpoints().size() >= 2;
            },
        });

        commands.add({
            .id = QStringLiteral("linkage.editParts"),
            .text = tr("&Edit Parts..."),
            .icon = Icon::Edit,
            .statusTip = tr("Rename or delete the parts the template draws."),
            .ribbon = { { page, tr("Parts"), RibbonButton::Small, nullptr, 30 } },
            .run = [&actions] { actions.editPartsDialog(); },
            .enabledWhen = [this] { return templateLoaded(); },
        });
    }

    /// What the car is made of, as far as the solver is concerned.
    void registerMechanismCommands(CommandRegistry& commands)
    {
        auto& actions = *m_context.windowActions();
        const QString page = QStringLiteral("linkage");

        commands.add({
            .id = QStringLiteral("linkage.steering"),
            .text = tr("Steering &Rack..."),
            .icon = Icon::SteeringWheel,
            .iconText = tr("Steering\nRack"),
            .statusTip = tr("Say where the steering rack is attached: which axle has one, and "
                            "which points it moves. An axle without a rack is not offered a "
                            "steer sweep."),
            .ribbon = { { page, tr("Steering"), RibbonButton::Large, nullptr, 40 } },
            .run = [&actions] { actions.steeringDialog(); },
            .enabledWhen = [this] {
                // A template with no corners has no axle to ask about, and one
                // that failed to load has nothing to write into.
                return templateLoaded() && !m_context.linkageTemplate().corners.empty();
            },
        });

        commands.add({
            .id = QStringLiteral("linkage.staticAngles"),
            .text = tr("Static &Camber and Toe..."),
            .icon = Icon::Angle,
            .iconText = tr("Camber\nand Toe"),
            .statusTip = tr("Set each axle's static camber and toe as numbers, the way Lotus's "
                            "Set Static Angles does. The wheel axis and the contact patch are "
                            "computed from them."),
            .ribbon = { { page, tr("Alignment"), RibbonButton::Large, nullptr, 50 } },
            .run = [&actions] { actions.staticAnglesDialog(); },
            // Angles are set on a wheel, so there has to be an axle that solves.
            .enabledWhen = [this] { return !m_context.simulation().isEmpty(); },
        });
    }

    void registerTemplateCommands(CommandRegistry& commands)
    {
        auto& actions = *m_context.windowActions();
        const QString page = QStringLiteral("linkage");
        const QString group = tr("Template");

        commands.add({
            .id = QStringLiteral("linkage.importTemplate"),
            .text = tr("&Import Template..."),
            .icon = Icon::Template,
            .statusTip =
                tr("Replace the rule that says which hardpoints are joined by which part."),
            .ribbon = { { page, group, RibbonButton::Small, nullptr, 60 } },
            .run = [&actions] { actions.importLinkageTemplateDialog(); },
        });

        commands.add({
            .id = QStringLiteral("linkage.resetTemplate"),
            .text = tr("&Reset to Built-in Template"),
            .icon = Icon::Restore,
            .iconText = tr("Reset Template"),
            .statusTip = tr("Replace the project's template with the one the application ships."),
            .ribbon = { { page, group, RibbonButton::Small, nullptr, 70 } },
            .run = [&actions] { actions.resetLinkageTemplate(); },
        });

        commands.add({
            .id = QStringLiteral("linkage.revealTemplate"),
            .text = tr("Show &Template File"),
            .icon = Icon::Braces,
            .statusTip =
                tr("Open the project's template in whatever edits JSON on this machine."),
            .ribbon = { { page, group, RibbonButton::Small, nullptr, 80 } },
            .run = [&actions] { actions.revealTemplateFile(); },
        });
    }

    bool templateLoaded() const { return !m_context.linkageTemplate().isEmpty(); }

    AppContext& m_context;
};

} // namespace

SUSPKIN_FEATURE(LinkageFeature)

} // namespace suspkin
