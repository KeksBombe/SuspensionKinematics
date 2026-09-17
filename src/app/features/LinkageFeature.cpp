#include "app/HardpointModel.h"
#include "app/PartDialogs.h"
#include "app/framework/AppContext.h"
#include "app/framework/CommandRegistry.h"
#include "app/framework/Feature.h"
#include "app/framework/FeatureRegistry.h"
#include "app/session/ProjectSession.h"
#include "io/LinkageTemplate.h"
#include "model/HardpointConfig.h"
#include "model/Linkage.h"
#include "project/Project.h"
#include "render/ViewportWidget.h"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QFileDialog>
#include <QFileInfo>
#include <QKeySequence>
#include <QMainWindow>
#include <QMessageBox>
#include <QUrl>

namespace suspkin {
namespace {

QString tr(const char* text) { return QCoreApplication::translate("LinkageFeature", text); }

/// What is drawn between the hardpoints: the template, and the parts it draws.
class LinkageFeature : public Feature {
public:
    explicit LinkageFeature(AppContext& context) : m_context(context) {}

    QString id() const override { return QStringLiteral("linkage"); }

    void registerCommands(CommandRegistry& commands) override
    {
        registerPartCommands(commands);
        registerTemplateCommands(commands);
    }

    void collectViewState(ViewState& view) const override
    {
        view.linksVisible = m_context.viewport()->linkageVisible();
    }

    void applyViewState(const ViewState& view) override
    {
        m_showParts->setChecked(view.linksVisible);
        m_context.viewport()->setLinkageVisible(view.linksVisible);
    }

private:
    /// A part drawn through the selected points, written into the template.
    void newPartFromSelection()
    {
        const QList<int> rows = m_context.viewport()->selectedHardpoints();
        LinkageDocument& linkage = m_context.session().linkage();
        if (rows.size() < 2 || linkage.linkageTemplate().isEmpty()) return;

        // In the order they were picked: that is the order the chain is drawn in.
        const HardpointTable& table = m_context.hardpoints()->table();
        QStringList names;
        for (const int row : rows) names << table.points[static_cast<std::size_t>(row)].name;

        NewPartDialog dialog(linkage.linkageTemplate(), names, m_context.window());
        if (dialog.exec() != QDialog::Accepted) return;
        const PartTemplate part = dialog.part();

        SessionMessage problem;
        const bool written = linkage.patch(
            [&part](const QByteArray& bytes, QString* error) {
                return addTemplatePart(bytes, part, error);
            },
            tr("The part could not be added."), &problem);
        m_context.showProblem(problem);
        if (written) m_context.showStatus(tr("Added the part \"%1\"").arg(part.label), 5000);
    }

    void editPartsDialog()
    {
        LinkageDocument& linkage = m_context.session().linkage();
        if (linkage.linkageTemplate().isEmpty()) return;
        const LinkageTemplate before = linkage.linkageTemplate();

        EditPartsDialog dialog(before, m_context.window());
        if (dialog.exec() != QDialog::Accepted) return;
        const QHash<QString, QString> relabelled = dialog.relabelled();
        const QStringList removed = dialog.removed();
        if (relabelled.isEmpty() && removed.isEmpty()) return;

        SessionMessage problem;
        const bool written = linkage.patch(
            [&](const QByteArray& bytes, QString* error) {
                QByteArray out = bytes;
                for (const QString& id : removed) {
                    out = removeTemplatePart(out, id, error);
                    if (out.isEmpty()) return out;
                }
                for (auto it = relabelled.constBegin(); it != relabelled.constEnd(); ++it) {
                    out = setTemplatePartLabel(out, it.key(), it.value(), error);
                    if (out.isEmpty()) return out;
                }
                return out;
            },
            tr("The parts could not be changed."), &problem);
        m_context.showProblem(problem);
        if (!written) return;

        moveRelabelledBodies(before, relabelled);
        m_context.showStatus(tr("Parts updated in the linkage template"), 5000);
    }

    /// A part's label is also the name of the body the configuration table's
    /// Part columns offer. A relabelled part takes the rows that named it along,
    /// rather than leaving them all pointing at a body that is no longer there.
    void moveRelabelledBodies(const LinkageTemplate& before,
                              const QHash<QString, QString>& relabelled)
    {
        ProjectSession& session = m_context.session();
        HardpointConfigMap config = m_context.hardpoints()->config();
        const BodyCatalog catalog = bodyCatalog(session.linkage().linkageTemplate());
        int moved = 0;
        for (auto it = relabelled.constBegin(); it != relabelled.constEnd(); ++it) {
            for (const PartTemplate& part : before.parts) {
                if (part.id != it.key()) continue;
                PartTemplate renamed = part;
                renamed.label = it.value();
                const QString from = partBodyName(part);
                const QString to = partBodyName(renamed);
                if (from == to || catalog.contains(from)) continue;
                moved += renameBody(config, from, to);
                // True of every step, which all describe this part: an undo must
                // not put back rows naming a body the template no longer has.
                session.history().renameBody(from, to);
            }
        }
        if (moved > 0) {
            m_context.hardpoints()->setConfig(config);
            session.hardpoints().captureConfig();
        }
    }

    void importLinkageTemplateDialog()
    {
        const QString path = QFileDialog::getOpenFileName(
            m_context.window(), tr("Import Linkage Template"), m_context.project().rootPath(),
            linkageTemplateFileFilter());
        if (path.isEmpty()) return;

        LinkageDocument& linkage = m_context.session().linkage();
        SessionMessage problem;
        const std::optional<QStringList> warnings = linkage.import(path, &problem);
        if (!warnings) {
            m_context.showProblem(problem);
            return;
        }

        m_context.showStatus(tr("%1 part(s) from %2")
                                 .arg(linkage.parts().parts.size())
                                 .arg(QFileInfo(path).fileName()),
                             6000);

        const QStringList notes = *warnings + linkage.parts().warnings;
        if (!notes.isEmpty()) {
            QMessageBox::information(m_context.window(), tr("Imported with warnings"),
                                     notes.join(QStringLiteral("\n")));
        }
    }

    void resetLinkageTemplate()
    {
        const QMessageBox::StandardButton answer = QMessageBox::question(
            m_context.window(), tr("Reset the linkage template"),
            tr("Replace this project's linkage template with the one the application ships?\n\n"
               "Any changes made to the project's copy are lost."),
            QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
        if (answer != QMessageBox::Yes) return;

        LinkageDocument& linkage = m_context.session().linkage();
        SessionMessage problem;
        if (!linkage.installBuiltin(&problem) || !linkage.load(&problem)) {
            m_context.showProblem(problem);
            return;
        }
        m_context.showStatus(
            tr("Linkage template reset - %1 part(s)").arg(linkage.parts().parts.size()), 6000);
    }

    /// Open the project's template in whatever edits JSON on this machine.
    void revealTemplateFile()
    {
        const Project& project = m_context.project();
        const QString path = project.absolutePath(project.linkageTemplate().relativePath);
        if (path.isEmpty() || !QFileInfo::exists(path)) return;
        QDesktopServices::openUrl(QUrl::fromLocalFile(path));
    }

    void registerPartCommands(CommandRegistry& commands)
    {
        const QString page = QStringLiteral("linkage");

        m_showParts = commands.add({
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
            .enabledWhen = [this] { return !m_context.session().linkage().parts().isEmpty(); },
        });

        commands.add({
            .id = QStringLiteral("linkage.newPart"),
            .text = tr("&New Part from Selection..."),
            .icon = Icon::Line,
            .iconText = tr("New Part"),
            .statusTip =
                tr("Draw a part through the selected points, in the order they were picked."),
            .ribbon = { { page, tr("Parts"), RibbonButton::Small, nullptr, 20 } },
            .run = [this] { newPartFromSelection(); },
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
            .run = [this] { editPartsDialog(); },
            .enabledWhen = [this] { return templateLoaded(); },
        });
    }

    void registerTemplateCommands(CommandRegistry& commands)
    {
        const QString page = QStringLiteral("linkage");
        const QString group = tr("Template");

        commands.add({
            .id = QStringLiteral("linkage.importTemplate"),
            .text = tr("&Import Template..."),
            .icon = Icon::Template,
            .statusTip =
                tr("Replace the rule that says which hardpoints are joined by which part."),
            .ribbon = { { page, group, RibbonButton::Small, nullptr, 60 } },
            .run = [this] { importLinkageTemplateDialog(); },
        });

        commands.add({
            .id = QStringLiteral("linkage.resetTemplate"),
            .text = tr("&Reset to Built-in Template"),
            .icon = Icon::Restore,
            .iconText = tr("Reset Template"),
            .statusTip = tr("Replace the project's template with the one the application ships."),
            .ribbon = { { page, group, RibbonButton::Small, nullptr, 70 } },
            .run = [this] { resetLinkageTemplate(); },
        });

        commands.add({
            .id = QStringLiteral("linkage.revealTemplate"),
            .text = tr("Show &Template File"),
            .icon = Icon::Braces,
            .statusTip =
                tr("Open the project's template in whatever edits JSON on this machine."),
            .ribbon = { { page, group, RibbonButton::Small, nullptr, 80 } },
            .run = [this] { revealTemplateFile(); },
        });
    }

    bool templateLoaded() const
    {
        return !m_context.session().linkage().linkageTemplate().isEmpty();
    }

    AppContext& m_context;
    QAction* m_showParts = nullptr;
};

} // namespace

SUSPKIN_FEATURE(LinkageFeature)

} // namespace suspkin
