#include "app/framework/AppContext.h"
#include "app/framework/CommandRegistry.h"
#include "app/framework/Feature.h"
#include "app/framework/FeatureRegistry.h"
#include "app/framework/WindowActions.h"

#include <QCoreApplication>

namespace suspkin {
namespace {

QString tr(const char* text) { return QCoreApplication::translate("AnalysisFeature", text); }

/// Putting the suspension through its travel, and what comes out when it is.
class AnalysisFeature : public Feature {
public:
    explicit AnalysisFeature(AppContext& context) : m_context(context) {}

    QString id() const override { return QStringLiteral("analysis"); }

    void registerCommands(CommandRegistry& commands) override
    {
        auto& actions = *m_context.windowActions();

        commands.add({
            .id = QStringLiteral("analysis.exportSweep"),
            .text = tr("Export Sweep as &CSV..."),
            .icon = Icon::FileTypeCsv,
            .iconText = tr("Export CSV"),
            .statusTip =
                tr("Write the sweep on screen out as a spreadsheet, one row per position."),
            .ribbon = { { .page = QStringLiteral("analysis"),
                          .group = tr("Sweep"),
                          .order = 30 } },
            .run = [&actions] { actions.exportSweepCsv(); },
        });
    }

private:
    AppContext& m_context;
};

} // namespace

SUSPKIN_FEATURE(AnalysisFeature)

} // namespace suspkin
