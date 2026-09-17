#include "app/framework/AppContext.h"
#include "app/framework/CommandRegistry.h"
#include "app/framework/Feature.h"
#include "app/framework/FeatureRegistry.h"
#include "app/framework/WindowActions.h"

#include <QCoreApplication>
#include <QKeySequence>
#include <QWidget>

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
        auto& actions = *m_context.windowActions();

        commands.add({ .id = QStringLiteral("project.new"),
                       .text = tr("&New Project..."),
                       .icon = Icon::FolderPlus,
                       .shortcut = QKeySequence::New,
                       .statusTip = tr("Start a new project in a folder of its own."),
                       .run = [&actions] { actions.newProject(); } });

        commands.add({ .id = QStringLiteral("project.open"),
                       .text = tr("&Open Project..."),
                       .icon = Icon::FolderOpen,
                       .shortcut = QKeySequence(QStringLiteral("Ctrl+Shift+O")),
                       .statusTip = tr("Open another project. This one is saved first."),
                       .run = [&actions] { actions.openProject(); } });

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
                       .run = [&actions] { actions.showProjectList(); } });

        commands.add({ .id = QStringLiteral("project.reveal"),
                       .text = tr("Show Project &Folder"),
                       .icon = Icon::FolderSearch,
                       .statusTip = tr("Open the project's folder in the file manager."),
                       .run = [&actions] { actions.revealProjectFolder(); } });

        commands.add({ .id = QStringLiteral("project.quit"),
                       .text = tr("&Quit"),
                       .icon = Icon::Logout,
                       .shortcut = QKeySequence::Quit,
                       .run = [this] { m_context.window()->close(); } });
    }

private:
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
