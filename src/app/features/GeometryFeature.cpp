#include "app/features/SharedCommands.h"
#include "app/framework/AppContext.h"
#include "app/framework/CommandRegistry.h"
#include "app/framework/Feature.h"
#include "app/framework/FeatureRegistry.h"
#include "app/session/ProjectSession.h"
#include "io/MeshImport.h"
#include "project/Project.h"
#include "render/ViewportWidget.h"

#include <QCoreApplication>
#include <QFileDialog>
#include <QFileInfo>
#include <QKeySequence>
#include <QMainWindow>
#include <QMessageBox>
#include <QStandardPaths>

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
            .run = [this] { importChassisDialog(); },
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
            .run = [this] { removeChassis(); },
            .enabledWhen = [this] { return !m_context.project().geometry().isEmpty(); },
        });
    }

private:
    void importChassisDialog()
    {
        const QString path = QFileDialog::getOpenFileName(
            m_context.window(), tr("Import chassis"), geometryDialogDirectory(m_context.project()),
            importFileFilter());
        if (!path.isEmpty()) importChassisFile(m_context, path);
    }

    void removeChassis()
    {
        const AssetRef asset = m_context.project().geometry();
        if (!asset.isEmpty()) {
            const QMessageBox::StandardButton answer = QMessageBox::question(
                m_context.window(), tr("Remove chassis"),
                tr("Remove %1 from this project?\n\nThe copy inside the project folder is "
                   "deleted. The file it was imported from is not touched.")
                    .arg(QFileInfo(asset.relativePath).fileName()),
                QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
            if (answer != QMessageBox::Yes) return;
        }

        m_context.session().chassis().remove();
        m_context.viewport()->clearMesh();
        m_context.refreshCommands();
    }

    AppContext& m_context;
};

} // namespace

void importChassisFile(AppContext& context, const QString& path)
{
    SessionMessage problem;
    std::optional<ChassisModel> model = context.session().chassis().import(path, &problem);
    if (!model) {
        context.showProblem(problem); // whatever was on screen stays there
        return;
    }

    context.viewport()->setMesh(std::move(model->mesh), model->edges);
    context.viewport()->fitToView();
    context.refreshCommands();
}

QString geometryDialogDirectory(const Project& project)
{
    if (!project.lastGeometryDirectory().isEmpty()) return project.lastGeometryDirectory();
    return QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
}

SUSPKIN_FEATURE(GeometryFeature)

} // namespace suspkin
