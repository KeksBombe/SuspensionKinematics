#include "app/HardpointModel.h"
#include "app/WheelDialog.h"
#include "app/features/SharedCommands.h"
#include "app/framework/AppContext.h"
#include "app/framework/CommandRegistry.h"
#include "app/framework/Feature.h"
#include "app/framework/FeatureRegistry.h"
#include "app/session/ProjectSession.h"
#include "project/Project.h"
#include "render/ViewportWidget.h"

#include <QCoreApplication>
#include <QMainWindow>
#include <QMessageBox>

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
            .run = [this] { addWheelsDialog(); },
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
            .run = [this] { removeWheels(); },
            .enabledWhen = [this] { return !m_context.project().wheels().isEmpty(); },
        });

        // Drawing them is a view matter, so it sits on the View tab -- which a
        // feature may do without the View tab knowing anything about wheels.
        m_show = commands.add({
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
            .enabledWhen = [this] { return !m_context.session().wheels().placements().empty(); },
        });
    }

    void collectViewState(ViewState& view) const override
    {
        view.wheelsVisible = m_context.viewport()->wheelsVisible();
    }

    void applyViewState(const ViewState& view) override
    {
        m_show->setChecked(view.wheelsVisible);
        m_context.viewport()->setWheelsVisible(view.wheelsVisible);
    }

private:
    void addWheelsDialog()
    {
        if (m_context.hardpoints()->rowCount() == 0) return; // the action is disabled

        const Project& project = m_context.project();
        const WheelsRef& wheels = project.wheels();
        WheelDialog dialog(m_context.hardpoints()->table(), wheels.spec,
                           project.absolutePath(wheels.tyre.relativePath),
                           project.absolutePath(wheels.rim.relativePath),
                           geometryDialogDirectory(project), m_context.window());
        if (dialog.exec() != QDialog::Accepted) return;

        const WheelSpec spec = dialog.spec();
        SessionMessage failure;
        QList<SessionMessage> problems;
        const bool applied = m_context.session().wheels().apply(spec, dialog.tyrePath(),
                                                                dialog.rimPath(), &failure, &problems);
        for (const SessionMessage& problem : problems) m_context.showProblem(problem);
        if (!applied) {
            m_context.showProblem(failure);
            return;
        }

        // Read back out of the copies the project now holds, so what is on screen
        // is exactly what reopening it will show. That reads a file that was just
        // read to validate it, which a wheel is small enough for and which keeps
        // one function responsible for loading them.
        showProjectWheels();

        QStringList warnings;
        const std::vector<WheelPlacement> placements =
            resolveWheels(spec, m_context.hardpoints()->table(), &warnings);
        m_context.showStatus(tr("%1 wheel(s) placed").arg(placements.size()), 6000);
        if (!warnings.isEmpty()) {
            QMessageBox::information(m_context.window(), tr("Wheels added with warnings"),
                                     warnings.join(QStringLiteral("\n")));
        }
    }

    void removeWheels()
    {
        if (m_context.project().wheels().isEmpty()) return;

        const QMessageBox::StandardButton answer = QMessageBox::question(
            m_context.window(), tr("Remove wheels"),
            tr("Remove the wheels from this project?\n\nThe copies of the models inside the "
               "project folder are deleted. The files they were imported from are not touched."),
            QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
        if (answer != QMessageBox::Yes) return;

        m_context.session().wheels().remove();
        m_context.viewport()->clearWheels();
        m_context.refreshCommands();
    }

    /// Read the models the project holds, hand them to the viewport and place
    /// them.
    void showProjectWheels()
    {
        SessionMessage problem;
        std::optional<WheelModels> models = m_context.session().wheels().open(&problem);
        m_context.showProblem(problem);
        if (!models) {
            m_context.viewport()->clearWheels();
            return;
        }

        m_context.viewport()->setWheelModels(std::move(models->tyre), std::move(models->tyreEdges),
                                             std::move(models->rim), std::move(models->rimEdges));
        m_context.session().placeWheels();
    }

    AppContext& m_context;
    QAction* m_show = nullptr;
};

} // namespace

SUSPKIN_FEATURE(WheelsFeature)

} // namespace suspkin
