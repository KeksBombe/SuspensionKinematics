#include "app/HardpointModel.h"
#include "app/features/SharedCommands.h"
#include "app/framework/AppContext.h"
#include "app/framework/CommandRegistry.h"
#include "app/framework/Feature.h"
#include "app/framework/FeatureRegistry.h"
#include "app/session/ProjectSession.h"
#include "project/Project.h"
#include "render/ViewportWidget.h"

#include <QCoreApplication>
#include <QDir>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QKeySequence>
#include <QMainWindow>
#include <QMessageBox>
#include <QPushButton>
#include <QStandardPaths>

namespace suspkin {
namespace {

QString tr(const char* text)
{
    return QCoreApplication::translate("HardpointWorkbookFeature", text);
}

/// The project's own copy of the workbook: where the points come from, and where
/// they go back to when the user asks.
///
/// Its buttons share the Hardpoints tab with HardpointsFeature's, in the order
/// RibbonSlot::order gives them.
class HardpointWorkbookFeature : public Feature {
public:
    explicit HardpointWorkbookFeature(AppContext& context) : m_context(context) {}

    QString id() const override { return QStringLiteral("workbook"); }

    void registerCommands(CommandRegistry& commands) override
    {
        const QString page = QStringLiteral("hardpoints");
        const QString group = tr("Workbook");

        commands.add({
            .id = QStringLiteral("hardpoints.import"),
            .text = tr("&Import Hardpoints..."),
            .icon = Icon::FileSpreadsheet,
            .iconText = tr("Import\nHardpoints"),
            .shortcut = QKeySequence(QStringLiteral("Ctrl+I")),
            .statusTip =
                tr("Bring in a hardpoint workbook (.xlsx). It is copied into the project."),
            .ribbon = { { page, group, RibbonButton::Large, nullptr, 10 } },
            .run = [this] { importHardpointsDialog(); },
        });

        commands.add({
            .id = QStringLiteral("hardpoints.overwriteWorkbook"),
            .text = tr("&Overwrite Workbook"),
            .icon = Icon::TableExport,
            .iconText = tr("Overwrite"),
            .shortcut = QKeySequence(QStringLiteral("Ctrl+Shift+S")),
            .statusTip = tr("Write the table into the project's own copy of the workbook."),
            .ribbon = { { page, group, RibbonButton::Small, nullptr, 20 } },
            .run = [this] { overwriteWorkbook(); },
            .enabledWhen = [this] {
                return hasPoints() && m_context.session().hardpoints().workbookWritable()
                       && !m_context.project().hardpoints().isEmpty();
            },
        });

        commands.add({
            .id = QStringLiteral("hardpoints.exportWorkbook"),
            .text = tr("&Export Workbook As..."),
            .icon = Icon::FileExport,
            .iconText = tr("Export As"),
            .shortcut = QKeySequence(QStringLiteral("Ctrl+E")),
            .statusTip = tr("Write the table into a workbook somewhere else. The project's own "
                            "copy is left as it is, and the edits stay pending."),
            .ribbon = { { page, group, RibbonButton::Small, nullptr, 30 } },
            .run = [this] { exportWorkbookAs(); },
            .enabledWhen = [this] {
                return hasPoints() && m_context.session().hardpoints().workbookWritable();
            },
        });

        commands.add({
            .id = QStringLiteral("hardpoints.remove"),
            .text = tr("&Remove Hardpoints"),
            .icon = Icon::TableMinus,
            .statusTip = tr("Take the hardpoints out of the project. The workbook they came from "
                            "is not touched."),
            .ribbon = { { page, group, RibbonButton::Small, nullptr, 40 } },
            .run = [this] { removeHardpoints(); },
            .enabledWhen = [this] { return hasPoints(); },
        });
    }

private:
    void importHardpointsDialog()
    {
        const QString path = QFileDialog::getOpenFileName(
            m_context.window(), tr("Import hardpoints"),
            hardpointDialogDirectory(m_context.project()), hardpointFileFilter());
        if (!path.isEmpty()) importHardpointFile(m_context, path);
    }

    void overwriteWorkbook()
    {
        HardpointDocument& document = m_context.session().hardpoints();
        const QString path = document.workbookPath();
        if (path.isEmpty()) return;

        // Every point deleted is a workbook with no table in it, which the reader
        // cannot open again -- and the project would lose its workbook with it.
        if (!hasPoints()) {
            QMessageBox::information(m_context.window(), tr("Overwrite Workbook"),
                                     tr("Every point has been deleted, so the workbook would be "
                                        "left with nothing in it that can be read back.\n\nTo "
                                        "take the hardpoints out of the project, use Remove "
                                        "Hardpoints."));
            return;
        }
        if (!confirmOverwrite(path, document.pendingEdits().count())) return;

        SessionMessage problem;
        if (!document.overwriteWorkbook(&problem)) {
            m_context.showProblem(problem);
            return;
        }
        m_context.setHardpointTable(document.baseline(), false);

        m_context.saveProject(); // clears the edits file, now that they are in the workbook
        m_context.showStatus(tr("Wrote the hardpoints into %1").arg(QFileInfo(path).fileName()),
                             6000);
    }

    /// Ask before @p changes changes are written into the workbook at @p path.
    bool confirmOverwrite(const QString& path, int changes) const
    {
        QMessageBox box(m_context.window());
        box.setIcon(QMessageBox::Warning);
        box.setWindowTitle(tr("Overwrite the project's workbook?"));
        box.setText(tr("Write %1 change(s) into %2?").arg(changes).arg(QFileInfo(path).fileName()));
        box.setInformativeText(
            tr("%1\n\nThe hardpoint cells are rewritten in place and mirrored points are added as "
               "new rows; everything else in the workbook -- other sheets, formatting and "
               "formulas -- is kept as it is.\n\nThis is the project's own copy. The file it was "
               "imported from is not touched; use Export Workbook As to write that one.")
                .arg(QDir::toNativeSeparators(path)));
        QPushButton* overwrite = box.addButton(tr("Overwrite"), QMessageBox::AcceptRole);
        box.addButton(QMessageBox::Cancel);
        box.setDefaultButton(overwrite);
        box.exec();
        return box.clickedButton() == overwrite;
    }

    void exportWorkbookAs()
    {
        Project& project = m_context.project();
        // Default to where the workbook came from: exporting back over the
        // original is the common case, and this makes it one click without
        // assuming it.
        QString suggestion = project.hardpoints().workbook.originalPath;
        if (suggestion.isEmpty()) {
            QString name = QFileInfo(project.hardpoints().workbook.relativePath).fileName();
            if (name.isEmpty()) name = QStringLiteral("hardpoints.xlsx");
            suggestion = QDir(hardpointDialogDirectory(project)).filePath(name);
        }

        QString path = QFileDialog::getSaveFileName(
            m_context.window(), tr("Export hardpoint workbook"), suggestion, hardpointFileFilter());
        if (path.isEmpty()) return;
        if (QFileInfo(path).suffix().isEmpty()) path += QStringLiteral(".xlsx");

        SessionMessage problem;
        if (!m_context.session().hardpoints().writeTo(path, &problem)) {
            m_context.showProblem(problem);
            return;
        }

        project.setLastHardpointDirectory(QFileInfo(path).absolutePath());
        m_context.markDirty();
        m_context.showStatus(tr("Exported %1 hardpoints to %2")
                                 .arg(m_context.hardpoints()->rowCount())
                                 .arg(QFileInfo(path).fileName()),
                             6000);
    }

    void removeHardpoints()
    {
        ProjectSession& session = m_context.session();
        const HardpointEdits pending = session.hardpoints().pendingEdits();
        const QString question =
            pending.isEmpty()
                ? tr("Remove the hardpoints from this project?\n\nThe workbook copy inside the "
                     "project folder is deleted. The file it was imported from is not touched.")
                : tr("Remove the hardpoints from this project?\n\n%1 change(s) have not been "
                     "written to a workbook and will be lost. The workbook copy inside the project "
                     "folder is deleted; the file it was imported from is not touched.")
                      .arg(pending.count());

        const QMessageBox::StandardButton answer =
            QMessageBox::question(m_context.window(), tr("Remove hardpoints"), question,
                                  QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
        if (answer != QMessageBox::Yes) return;

        session.hardpoints().remove();
        m_context.viewport()->clearHardpoints();
        // The template stays: it describes a kind of car, not this workbook, and
        // the next import should find it already there.
        session.resolveFromTable();
        // So do the wheel models, for the same reason. With no points to pin them
        // to there is nowhere to draw them, which is what this leaves behind.
        session.placeWheels();
        m_context.hardpointDock()->hide();
        // The workbook and the edits file are deleted, which no step can put back.
        session.restartHistory();
        m_context.markDirty();
    }

    bool hasPoints() const { return m_context.hardpoints()->rowCount() > 0; }

    AppContext& m_context;
};

} // namespace

void importHardpointFile(AppContext& context, const QString& path)
{
    HardpointDocument& document = context.session().hardpoints();
    const HardpointEdits pending = document.pendingEdits();
    if (!pending.isEmpty()) {
        const QMessageBox::StandardButton answer = QMessageBox::question(
            context.window(), tr("Replace the hardpoints?"),
            tr("This project is holding %1 hardpoint change(s) that are not in its workbook "
               "yet.\n\nImporting a different workbook discards them. Continue?")
                .arg(pending.count()),
            QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
        if (answer != QMessageBox::Yes) return;
    }

    SessionMessage problem;
    const std::optional<HardpointImport> imported = document.import(path, &problem);
    context.showProblem(problem);
    if (!imported) return; // whatever was loaded stays loaded

    context.setHardpointTable(document.baseline(), !context.viewport()->hasMesh());
    // An import is not an edit: the steps that led to the old points lead
    // nowhere in these.
    context.session().restartHistory();
    context.hardpointDock()->show();
    context.hardpointDock()->raise();

    const int count = context.hardpoints()->rowCount();
    context.showStatus(tr("Imported %1 hardpoints from %2 in %3 ms")
                           .arg(count)
                           .arg(QFileInfo(path).fileName())
                           .arg(imported->elapsedMs),
                       6000);

    if (!imported->warnings.isEmpty()) {
        QMessageBox::information(context.window(), tr("Imported with warnings"),
                                 tr("%1 hardpoints were imported.\n\n%2")
                                     .arg(count)
                                     .arg(imported->warnings.join(QStringLiteral("\n\n"))));
    }
}

bool adoptNewWorkbook(AppContext& context, const HardpointTable& table)
{
    SessionMessage problem;
    if (!context.session().hardpoints().adoptNewWorkbook(table, &problem)) {
        context.showProblem(problem);
        return false;
    }

    context.setHardpointTable(context.session().hardpoints().baseline(),
                              !context.viewport()->hasMesh());
    // The table begins here. Undoing past it would need the workbook that was
    // just made to be unmade, which is Remove Hardpoints, not a step.
    context.session().restartHistory();
    context.hardpointDock()->show();
    context.hardpointDock()->raise();
    context.markDirty();
    return true;
}

QString hardpointDialogDirectory(const Project& project)
{
    if (!project.lastHardpointDirectory().isEmpty()) return project.lastHardpointDirectory();
    if (!project.lastGeometryDirectory().isEmpty()) return project.lastGeometryDirectory();
    return QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
}

SUSPKIN_FEATURE(HardpointWorkbookFeature)

} // namespace suspkin
