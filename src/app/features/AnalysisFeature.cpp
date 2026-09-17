#include "app/AnalysisPanel.h"
#include "app/framework/AppContext.h"
#include "app/framework/CommandRegistry.h"
#include "app/framework/Feature.h"
#include "app/framework/FeatureRegistry.h"
#include "app/session/ProjectSession.h"
#include "project/Project.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileDialog>
#include <QMainWindow>
#include <QMessageBox>
#include <QSaveFile>

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
            .run = [this] { exportSweepCsv(); },
        });
    }

    void windowReady() override
    {
        // The panel has an export button of its own, which is this command.
        QObject::connect(m_context.analysisPanel(), &AnalysisPanel::exportCsvRequested,
                         m_context.window(), [this] { exportSweepCsv(); });
    }

    void collectViewState(ViewState& view) const override
    {
        const AnalysisPanel* panel = m_context.analysisPanel();
        SimulationState& simulation = view.simulation;
        simulation.active = panel->simulating();
        simulation.axle = panel->axle();
        simulation.kind = panel->kind();
        simulation.sweep = panel->settings();
        simulation.position = panel->position();
        for (const SweepMeasure measure : panel->measures())
            simulation.measures << sweepMeasureKey(measure);
        simulation.sides = panel->sides();
        simulation.animating = panel->animating();
        simulation.animationSeconds = panel->animationSeconds();
        simulation.allAxles = panel->movesAllAxles();
        simulation.parametersOpen = panel->parametersVisible();
    }

    void applyViewState(const ViewState& view) override
    {
        AnalysisPanel* panel = m_context.analysisPanel();
        // The travel and the kind have to be set before the position, because
        // between them they decide the range the position is allowed to take.
        const SimulationState& simulation = view.simulation;
        panel->setSettings(simulation.sweep);
        panel->setKind(simulation.kind);
        if (!simulation.axle.isEmpty()) panel->setAxle(simulation.axle);
        const QList<SweepMeasure> measures = knownMeasures(simulation.measures);
        if (!measures.isEmpty()) panel->setMeasures(measures);
        panel->setSides(simulation.sides);
        panel->setPosition(simulation.position);
        panel->setMovesAllAxles(simulation.allAxles);
        panel->setAnimationSeconds(simulation.animationSeconds);
        panel->setParametersVisible(simulation.parametersOpen);
        panel->setSimulating(simulation.active);
        m_context.session().resolveSimulation();
        // Last, because starting it turns the simulation on and moves the model,
        // and both of those have to be settled first.
        panel->setAnimating(simulation.animating && simulation.active);
    }

private:
    void exportSweepCsv()
    {
        const Project& project = m_context.project();
        const AxleSolver* axle =
            m_context.session().simulation().simulation().axleFor(m_context.analysisPanel()->axle());
        if (!axle) {
            QMessageBox::information(m_context.window(), tr("Export Sweep"),
                                     tr("There is no axle to sweep yet. Import hardpoints, and "
                                        "check that the linkage template names the mechanism."));
            return;
        }

        const SweepSpec spec = m_context.analysisPanel()->spec();
        const SweepResult result = runSweep(*axle, spec);

        const QString suggested = QDir(project.rootPath())
                                      .filePath(QStringLiteral("%1-%2-%3.csv")
                                                    .arg(project.name().isEmpty()
                                                             ? QStringLiteral("sweep")
                                                             : project.name(),
                                                         axle->label().isEmpty()
                                                             ? axle->cornerToken()
                                                             : axle->label(),
                                                         sweepKindToString(spec.kind)));
        const QString path =
            QFileDialog::getSaveFileName(m_context.window(), tr("Export Sweep as CSV"), suggested,
                                         tr("CSV files (*.csv);;All files (*)"));
        if (path.isEmpty()) return;

        QSaveFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)
            || file.write(sweepToCsv(result)) < 0 || !file.commit()) {
            QMessageBox::warning(m_context.window(), tr("Export Sweep"),
                                 tr("Could not write %1: %2")
                                     .arg(QDir::toNativeSeparators(path), file.errorString()));
            return;
        }
        m_context.showStatus(tr("Sweep written to %1").arg(QDir::toNativeSeparators(path)), 5000);
    }

    /// The curves @p keys name that this build knows. One a later release added
    /// is left out rather than read as the fallback, which would put a plot on
    /// screen that nobody asked for.
    static QList<SweepMeasure> knownMeasures(const QStringList& keys)
    {
        QList<SweepMeasure> measures;
        for (const QString& key : keys) {
            const SweepMeasure measure = sweepMeasureFromKey(key);
            if (sweepMeasureKey(measure) == key) measures << measure;
        }
        return measures;
    }

    AppContext& m_context;
};

} // namespace

SUSPKIN_FEATURE(AnalysisFeature)

} // namespace suspkin
