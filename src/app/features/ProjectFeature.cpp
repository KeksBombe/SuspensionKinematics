#include "app/ProjectLauncher.h"
#include "app/framework/AppContext.h"
#include "app/framework/CommandRegistry.h"
#include "app/framework/Feature.h"
#include "app/framework/FeatureRegistry.h"
#include "project/Project.h"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QFileInfo>
#include <QKeySequence>
#include <QMainWindow>
#include <QUrl>

namespace suspkin {
namespace {

QString tr(const char* text) { return QCoreApplication::translate("ProjectFeature", text); }

/// The project itself, and the way out.
///
/// None of these are on the ribbon: they are what the File menu holds, because
/// the project and the way out are not a tab's worth of commands.
class ProjectFeature : public Feature {
public:
    explicit ProjectFeature(AppContext& context) : m_context(context) {}

    QString id() const override { return QStringLiteral("project"); }

    void registerCommands(CommandRegistry& commands) override
    {
        commands.add({ .id = QStringLiteral("project.new"),
                       .text = tr("&New Project..."),
                       .icon = Icon::FolderPlus,
                       .shortcut = QKeySequence::New,
                       .statusTip = tr("Start a new project in a folder of its own."),
                       .run = [this] { newProject(); } });

        commands.add({ .id = QStringLiteral("project.open"),
                       .text = tr("&Open Project..."),
                       .icon = Icon::FolderOpen,
                       .shortcut = QKeySequence(QStringLiteral("Ctrl+Shift+O")),
                       .statusTip = tr("Open another project. This one is saved first."),
                       .run = [this] { openProject(); } });

        commands.add({ .id = QStringLiteral("project.save"),
                       .text = tr("&Save Project"),
                       .icon = Icon::DeviceFloppy,
                       .shortcut = QKeySequence::Save,
                       .statusTip =
                           tr("The project saves itself as you work; this writes it out now."),
                       .run = [this] { saveNow(); } });

        commands.add({ .id = QStringLiteral("project.list"),
                       .text = tr("&Project List..."),
                       .icon = Icon::ListDetails,
                       .statusTip = tr("Go back to the list of projects."),
                       .run = [this] { showProjectList(); } });

        commands.add({ .id = QStringLiteral("project.reveal"),
                       .text = tr("Show Project &Folder"),
                       .icon = Icon::FolderSearch,
                       .statusTip = tr("Open the project's folder in the file manager."),
                       .run = [this] { revealProjectFolder(); } });

        commands.add({ .id = QStringLiteral("project.quit"),
                       .text = tr("&Quit"),
                       .icon = Icon::Logout,
                       .shortcut = QKeySequence::Quit,
                       .run = [this] { m_context.window()->close(); } });
    }

private:
    void newProject()
    {
        const QString path = ProjectLauncher::runNewProjectDialog(m_context.window());
        if (path.isEmpty()) return;
        m_context.saveProject();
        m_context.requestProject(path);
    }

    void openProject()
    {
        const QString path = ProjectLauncher::runOpenProjectDialog(m_context.window());
        if (path.isEmpty()) return;
        if (QFileInfo(path).absoluteFilePath()
            == QFileInfo(m_context.project().manifestPath()).absoluteFilePath()) {
            return; // already open
        }
        m_context.saveProject();
        m_context.requestProject(path);
    }

    void showProjectList()
    {
        m_context.saveProject();
        m_context.requestProjectList();
    }

    void revealProjectFolder()
    {
        QDesktopServices::openUrl(QUrl::fromLocalFile(m_context.project().rootPath()));
    }

    /// The project saves itself; this is the user asking for it now, so it says
    /// that it happened.
    void saveNow()
    {
        if (m_context.saveProject()) m_context.showStatus(tr("Project saved"), 3000);
    }

    AppContext& m_context;
};

} // namespace

SUSPKIN_FEATURE(ProjectFeature)

} // namespace suspkin
