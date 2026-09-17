#include "app/framework/AppContext.h"
#include "app/framework/CommandRegistry.h"
#include "app/framework/Feature.h"
#include "app/framework/FeatureRegistry.h"
#include "app/framework/WindowActions.h"
#include "app/HardpointModel.h"
#include "project/Project.h"
#include "render/ViewportWidget.h"

#include <QCoreApplication>

namespace suspkin {
namespace {

QString tr(const char* text) { return QCoreApplication::translate("WheelsFeature", text); }

/// A wheel is the tyre and the rim together, which is why the commands say
/// "wheels" and the dialog asks for a tyre model and a rim model.
class WheelsFeature : public Feature {
public:
    explicit WheelsFeature(AppContext& context) : m_context(context) {}

    QString id() const override { return QStringLiteral("wheels"); }

    void registerCommands(CommandRegistry& commands) override
    {
        auto& actions = *m_context.windowActions();

        commands.add({
            .id = QStringLiteral("wheels.add"),
            .text = tr("Add &Wheels..."),
            .icon = Icon::Wheel,
            .iconText = tr("Add\nWheels"),
            .statusTip = tr("Draw a tyre and a rim model at the four wheel centres."),
            .ribbon = { { .page = QStringLiteral("geometry"),
                          .group = tr("Wheels"),
                          .button = RibbonButton::Large,
                          .order = 30 } },
            .run = [&actions] { actions.addWheelsDialog(); },
            .enabledWhen = [this] { return m_context.hardpoints()->rowCount() > 0; },
        });

        commands.add({
            .id = QStringLiteral("wheels.remove"),
            .text = tr("Remove Wh&eels"),
            .icon = Icon::Trash,
            .statusTip = tr("Take the wheel models out of the project. The files they came from "
                            "are not touched."),
            .ribbon = { { .page = QStringLiteral("geometry"),
                          .group = tr("Wheels"),
                          .order = 40 } },
            .run = [&actions] { actions.removeWheels(); },
            .enabledWhen = [this] { return !m_context.project().wheels().isEmpty(); },
        });

        // Drawing them is a view matter, so it sits on the View tab -- which a
        // feature may do without the View tab knowing anything about wheels.
        commands.add({
            .id = QStringLiteral("wheels.show"),
            .text = tr("Show &Wheels"),
            .icon = Icon::Wheel,
            .iconText = tr("Wheels"),
            .shortcut = QKeySequence(QStringLiteral("Ctrl+Shift+W")),
            .statusTip = tr("Draw the tyre and rim models at the wheel centres."),
            .checkable = true,
            .checkedByDefault = true,
            .ribbon = { { .page = QStringLiteral("view"), .group = tr("Show"), .order = 70 } },
            .onToggled = [this](bool on) { m_context.viewport()->setWheelsVisible(on); },
            .enabledWhen = [this] { return !m_context.wheelPlacements().empty(); },
        });
    }

private:
    AppContext& m_context;
};

} // namespace

SUSPKIN_FEATURE(WheelsFeature)

} // namespace suspkin
