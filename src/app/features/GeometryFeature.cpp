#include "app/framework/AppContext.h"
#include "app/framework/CommandRegistry.h"
#include "app/framework/Feature.h"
#include "app/framework/FeatureRegistry.h"
#include "app/framework/WindowActions.h"
#include "project/Project.h"

#include <QCoreApplication>
#include <QKeySequence>

namespace suspkin {
namespace {

QString tr(const char* text) { return QCoreApplication::translate("GeometryFeature", text); }

/// The car the suspension is bolted to.
///
/// "Chassis" rather than "geometry": the wheels are geometry too, and they are
/// their own feature.
class GeometryFeature : public Feature {
public:
    explicit GeometryFeature(AppContext& context) : m_context(context) {}

    QString id() const override { return QStringLiteral("geometry"); }

    void registerCommands(CommandRegistry& commands) override
    {
        auto& actions = *m_context.windowActions();

        commands.add({
            .id = QStringLiteral("geometry.importChassis"),
            .text = tr("&Import Chassis..."),
            .icon = Icon::FileImport,
            .iconText = tr("Import\nChassis"),
            .shortcut = QKeySequence::Open,
            .statusTip = tr("Bring in the chassis or monocoque as STEP or STL. It is copied "
                            "into the project."),
            .ribbon = { { .page = QStringLiteral("geometry"),
                          .group = tr("Chassis"),
                          .button = RibbonButton::Large,
                          .order = 10 } },
            .run = [&actions] { actions.importChassisDialog(); },
        });

        commands.add({
            .id = QStringLiteral("geometry.removeChassis"),
            .text = tr("&Remove Chassis"),
            .icon = Icon::FileX,
            .shortcut = QKeySequence::Close,
            .statusTip = tr("Take the chassis out of the project. The file it was imported from "
                            "is not touched."),
            .ribbon = { { .page = QStringLiteral("geometry"),
                          .group = tr("Chassis"),
                          .order = 20 } },
            .run = [&actions] { actions.removeChassis(); },
            .enabledWhen = [this] { return !m_context.project().geometry().isEmpty(); },
        });
    }

private:
    AppContext& m_context;
};

} // namespace

SUSPKIN_FEATURE(GeometryFeature)

} // namespace suspkin
