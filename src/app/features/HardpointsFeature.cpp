#include "app/HardpointModel.h"
#include "app/MirrorDialog.h"
#include "app/PointDialog.h"
#include "app/features/SharedCommands.h"
#include "app/framework/AppContext.h"
#include "app/framework/CommandRegistry.h"
#include "app/framework/Feature.h"
#include "app/framework/FeatureRegistry.h"
#include "app/session/ProjectSession.h"
#include "model/HardpointMirror.h"
#include "project/Project.h"
#include "render/ViewportWidget.h"

#include <QCoreApplication>
#include <QInputDialog>
#include <QKeySequence>
#include <QMainWindow>
#include <QMessageBox>

#include <algorithm>

namespace suspkin {
namespace {

QString tr(const char* text) { return QCoreApplication::translate("HardpointsFeature", text); }
/// The plural form: %n in @p text is @p n.
QString tr(const char* text, const char* disambiguation, int n)
{
    return QCoreApplication::translate("HardpointsFeature", text, disambiguation, n);
}

/// The points themselves: making them, mirroring them, and editing the ones
/// there are. Where they come from and go back to is HardpointWorkbookFeature's.
class HardpointsFeature : public Feature {
public:
    explicit HardpointsFeature(AppContext& context) : m_context(context) {}

    QString id() const override { return QStringLiteral("hardpoints"); }

    void registerCommands(CommandRegistry& commands) override
    {
        registerPointCommands(commands);
        registerShowCommands(commands);
    }

    void collectViewState(ViewState& view) const override
    {
        const ViewportWidget* viewport = m_context.viewport();
        view.labelsVisible = viewport->hardpointLabelsVisible();
        view.selectedHardpoint = viewport->selectedHardpoint();
        view.selection = viewport->selectedHardpoints();
    }

    void applyViewState(const ViewState& view) override
    {
        m_showLabels->setChecked(view.labelsVisible);
        m_context.viewport()->setHardpointLabelsVisible(view.labelsVisible);

        // The whole selection, in the order it was picked -- a chain half-picked
        // for a new part comes back half-picked.
        QList<int> selection;
        for (const int row : view.selection)
            if (row >= 0 && row < m_context.hardpoints()->rowCount()) selection.append(row);
        if (!selection.isEmpty()) m_context.selectPoints(selection, view.selectedHardpoint);
    }

private:
    /// Making points rather than importing them, and editing the ones there are.
    void registerPointCommands(CommandRegistry& commands)
    {
        const QString page = QStringLiteral("hardpoints");

        commands.add({
            .id = QStringLiteral("hardpoints.newTable"),
            .text = tr("&New Hardpoint Table..."),
            .icon = Icon::TablePlus,
            .iconText = tr("New Table"),
            .statusTip = tr("Start a table of points here rather than importing one. The project "
                            "gets a workbook of its own to hold them."),
            .ribbon = { { page, tr("Create"), RibbonButton::Small, nullptr, 60 } },
            // The same dialog as Add Point: in a project with no workbook yet,
            // adding the first point is what makes one.
            .run = [this] { addPointDialog(); },
            .enabledWhen = [this] { return m_context.project().hardpoints().isEmpty(); },
        });

        const QString points = tr("Points");

        // The icon has the mirror line upright, which is how the car's centre
        // plane -- the one mirroring flips across -- looks from above.
        commands.add({
            .id = QStringLiteral("hardpoints.mirror"),
            .text = tr("&Mirror Hardpoints..."),
            .icon = Icon::FlipHorizontal,
            .iconText = tr("Mirror"),
            .shortcut = QKeySequence(QStringLiteral("Ctrl+M")),
            .statusTip =
                tr("Copy points to the other side of the car, named by the project's own rule."),
            .ribbon = { { page, points, RibbonButton::Large, nullptr, 70 } },
            .run = [this] { mirrorHardpointsDialog(); },
            .enabledWhen = [this] { return hasPoints(); },
        });

        commands.add({
            .id = QStringLiteral("hardpoints.addPoint"),
            .text = tr("&Add Point..."),
            .icon = Icon::RowInsertBottom,
            .shortcut = QKeySequence(Qt::Key_Insert),
            .statusTip = tr("Add a point next to the selected one."),
            .ribbon = { { page, points, RibbonButton::Small, nullptr, 80 } },
            .run = [this] { addPointDialog(); },
        });

        commands.add({
            .id = QStringLiteral("hardpoints.renamePoint"),
            .text = tr("Re&name Point..."),
            .icon = Icon::Forms,
            .statusTip = tr("Give the selected point a different name."),
            .ribbon = { { page, points, RibbonButton::Small, nullptr, 90 } },
            .run = [this] { renamePointDialog(); },
            .enabledWhen = [this] { return m_context.viewport()->selectedHardpoint() >= 0; },
        });

        commands.add({
            .id = QStringLiteral("hardpoints.deletePoint"),
            .text = tr("&Delete Point"),
            .icon = Icon::RowRemove,
            .shortcut = QKeySequence::Delete,
            .statusTip = tr("Delete the selected points. A point the workbook holds stays in it "
                            "until the workbook is overwritten."),
            .ribbon = { { page, points, RibbonButton::Small, nullptr, 100 } },
            .run = [this] { deleteSelectedPoints(); },
            .enabledWhen = [this] { return !m_context.viewport()->selectedHardpoints().empty(); },
        });
    }

    void registerShowCommands(CommandRegistry& commands)
    {
        m_showLabels = commands.add({
            .id = QStringLiteral("hardpoints.showLabels"),
            .text = tr("Show Hardpoint &Labels"),
            .icon = Icon::Tag,
            .iconText = tr("Labels"),
            .shortcut = QKeySequence(QStringLiteral("Ctrl+L")),
            .statusTip = tr("Write each hardpoint's name beside its marker."),
            .checkable = true,
            .checkedByDefault = true,
            .ribbon = { { QStringLiteral("hardpoints"), tr("Show"), RibbonButton::Small, nullptr,
                          120 },
                        { QStringLiteral("view"), tr("Show"), RibbonButton::Small, nullptr, 50 } },
            .onToggled = [this](bool on) { m_context.viewport()->setHardpointLabelsVisible(on); },
        });
    }

    /// A point of the user's own, next to the selected one. In a project with
    /// no workbook yet this is also what makes one: New Hardpoint Table.
    void addPointDialog()
    {
        const HardpointTable& table = m_context.hardpoints()->table();
        const int current = m_context.viewport()->selectedHardpoint();

        // Seeded from the selection, so the new point starts where the one being
        // worked on is -- with a name that is free.
        Hardpoint seed;
        seed.name = QStringLiteral("P1");
        if (current >= 0 && current < static_cast<int>(table.size())) {
            seed = table.points[static_cast<std::size_t>(current)];
            seed.mirrorOf.clear();
        }

        const bool creating = m_context.project().hardpoints().isEmpty();
        const QString note =
            creating ? tr("This project has no hardpoint workbook yet. Adding a point makes one "
                          "inside the project, hardpoints/hardpoints.xlsx, and the table grows "
                          "from there.")
                     : QString();
        PointDialog dialog(table, seed, note, m_context.window());
        if (creating) dialog.setWindowTitle(tr("New Hardpoint Table"));
        if (dialog.exec() != QDialog::Accepted) return;
        const Hardpoint point = dialog.point();

        if (creating) {
            HardpointTable first;
            first.points.push_back(point);
            if (!adoptNewWorkbook(m_context, first)) return;
            m_context.selectPoints({ 0 }, 0);
            m_context.showStatus(tr("Started a hardpoint table with %1").arg(point.name), 6000);
            return;
        }

        // Right under the one it was seeded from, where it belongs; the edits file
        // remembers the place, so it comes back there too.
        const int row = current >= 0 ? current + 1 : m_context.hardpoints()->rowCount();
        if (!m_context.hardpoints()->insertPoint(row, point)) return;
        m_context.syncTableToViewport(false);
        m_context.selectPoints({ row }, row);
        m_context.markDirty();
        m_context.session().recordEdit(tr("Add %1").arg(point.name));
        m_context.showStatus(tr("Added %1").arg(point.name), 5000);
    }

    void mirrorHardpointsDialog()
    {
        if (!hasPoints()) return;

        HardpointModel* model = m_context.hardpoints();
        Project& project = m_context.project();
        MirrorDialog dialog(model->table(), m_context.viewport()->selectedHardpoints(),
                            project.mirror(), m_context.window());
        if (dialog.exec() != QDialog::Accepted) return;

        const MirrorSpec spec = dialog.spec();
        const MirrorOutcome outcome = mirrorHardpoints(model->table(), dialog.rows(), spec);

        // The rule is remembered whether or not it changed anything: it is the
        // user's convention, and they will want it again next time.
        project.setMirror(spec);

        if (outcome.added == 0 && outcome.updated == 0) {
            QMessageBox::information(m_context.window(), tr("Nothing to mirror"),
                                     outcome.notes.isEmpty()
                                         ? tr("No hardpoints were mirrored.")
                                         : outcome.notes.join(QStringLiteral("\n\n")));
            m_context.markDirty();
            return;
        }

        m_context.setHardpointTable(outcome.table, false);
        m_context.session().hardpoints().captureMirrorProvenance();
        m_context.markDirty();
        m_context.session().recordEdit(
            tr("Mirror %n point(s)", "", outcome.added + outcome.updated));

        m_context.showStatus(
            tr("Mirrored %1 hardpoint(s), replaced %2").arg(outcome.added).arg(outcome.updated),
            6000);

        if (!outcome.notes.isEmpty()) {
            QMessageBox::information(m_context.window(), tr("Mirrored with notes"),
                                     outcome.notes.join(QStringLiteral("\n\n")));
        }
    }

    void renamePointDialog()
    {
        const int row = m_context.viewport()->selectedHardpoint();
        const HardpointTable& table = m_context.hardpoints()->table();
        if (row < 0 || row >= static_cast<int>(table.size())) return;
        const QString current = table.points[static_cast<std::size_t>(row)].name;

        QString proposed = current;
        for (;;) {
            bool ok = false;
            proposed = QInputDialog::getText(m_context.window(), tr("Rename Point"),
                                             tr("New name for %1:").arg(current),
                                             QLineEdit::Normal, proposed, &ok)
                           .trimmed();
            if (!ok || proposed == current) return;
            // Asked here as well as in the model, so a refusal is a message the
            // user can act on and try again from, not an edit that silently fails.
            const QString problem = hardpointNameProblem(proposed, table, row);
            if (problem.isEmpty()) break;
            QMessageBox::information(m_context.window(), tr("Rename Point"), problem);
        }
        // The model's pointRenamed does the rest.
        m_context.hardpoints()->renamePoint(row, proposed);
    }

    void deleteSelectedPoints()
    {
        const QList<int> rows = m_context.viewport()->selectedHardpoints();
        if (rows.isEmpty()) return;

        HardpointModel* model = m_context.hardpoints();
        // Not asked about: Ctrl+Z brings them back. A point the workbook holds
        // stays in it until the workbook is overwritten, as the command says.
        const QString label =
            rows.size() == 1
                ? tr("Delete %1").arg(model->table().points[static_cast<std::size_t>(rows.front())].name)
                : tr("Delete %n point(s)", "", rows.size());

        const int first = *std::min_element(rows.begin(), rows.end());
        model->removePoints(std::vector<int>(rows.begin(), rows.end()));
        // The model has dropped their configuration; the project, which the table
        // is refilled from, has to agree before anything is resolved again.
        m_context.session().hardpoints().captureConfig();
        m_context.session().hardpoints().captureMirrorProvenance();
        m_context.syncTableToViewport(false);

        const int next = std::min(first, model->rowCount() - 1);
        if (next >= 0) m_context.selectPoints({ next }, next);
        m_context.markDirty();
        m_context.session().recordEdit(label);
        m_context.showStatus(
            tr("Deleted %n point(s). Ctrl+Z brings them back.", "", rows.size()), 6000);
    }

    bool hasPoints() const { return m_context.hardpoints()->rowCount() > 0; }

    AppContext& m_context;
    QAction* m_showLabels = nullptr;
};

} // namespace

SUSPKIN_FEATURE(HardpointsFeature)

} // namespace suspkin
