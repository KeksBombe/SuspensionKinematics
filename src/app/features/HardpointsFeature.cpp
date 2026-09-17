#include "app/HardpointModel.h"
#include "app/framework/AppContext.h"
#include "app/framework/CommandRegistry.h"
#include "app/framework/Feature.h"
#include "app/framework/FeatureRegistry.h"
#include "app/framework/WindowActions.h"
#include "io/LinkageTemplate.h"
#include "project/Project.h"
#include "render/ViewportWidget.h"

#include <QCoreApplication>
#include <QKeySequence>

namespace suspkin {
namespace {

QString tr(const char* text) { return QCoreApplication::translate("HardpointsFeature", text); }

/// The workbook, and the points in it.
class HardpointsFeature : public Feature {
public:
    explicit HardpointsFeature(AppContext& context) : m_context(context) {}

    QString id() const override { return QStringLiteral("hardpoints"); }

    void registerCommands(CommandRegistry& commands) override
    {
        registerWorkbookCommands(commands);
        registerPointCommands(commands);
        registerShowCommands(commands);
    }

private:
    /// The project's own copy of the workbook: where the points come from, and
    /// where they go back to when the user asks.
    void registerWorkbookCommands(CommandRegistry& commands)
    {
        auto& actions = *m_context.windowActions();
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
            .run = [&actions] { actions.importHardpointsDialog(); },
        });

        commands.add({
            .id = QStringLiteral("hardpoints.overwriteWorkbook"),
            .text = tr("&Overwrite Workbook"),
            .icon = Icon::TableExport,
            .iconText = tr("Overwrite"),
            .shortcut = QKeySequence(QStringLiteral("Ctrl+Shift+S")),
            .statusTip = tr("Write the table into the project's own copy of the workbook."),
            .ribbon = { { page, group, RibbonButton::Small, nullptr, 20 } },
            .run = [&actions] { actions.overwriteWorkbook(); },
            .enabledWhen = [this] {
                return hasPoints() && m_context.workbookWritable()
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
            .run = [&actions] { actions.exportWorkbookAs(); },
            .enabledWhen = [this] { return hasPoints() && m_context.workbookWritable(); },
        });

        commands.add({
            .id = QStringLiteral("hardpoints.remove"),
            .text = tr("&Remove Hardpoints"),
            .icon = Icon::TableMinus,
            .statusTip = tr("Take the hardpoints out of the project. The workbook they came from "
                            "is not touched."),
            .ribbon = { { page, group, RibbonButton::Small, nullptr, 40 } },
            .run = [&actions] { actions.removeHardpoints(); },
            .enabledWhen = [this] { return hasPoints(); },
        });
    }

    /// Making points rather than importing them, and editing the ones there are.
    void registerPointCommands(CommandRegistry& commands)
    {
        auto& actions = *m_context.windowActions();
        const QString page = QStringLiteral("hardpoints");

        commands.add({
            .id = QStringLiteral("hardpoints.generate"),
            .text = tr("&Generate from Design..."),
            .icon = Icon::Wand,
            .iconText = tr("Generate\nfrom Design"),
            .shortcut = QKeySequence(QStringLiteral("Ctrl+G")),
            .statusTip = tr("Work out the wishbones, the upright and the steering from vehicle "
                            "targets: track, caster, roll centre, anti-dive and the rest."),
            .ribbon = { { page, tr("Create"), RibbonButton::Large, nullptr, 50 } },
            .run = [&actions] { actions.generateFromDesignDialog(); },
            .enabledWhen = [this] {
                // The generator names what it makes through the template's
                // roles, so it needs a template that loaded and named them.
                const LinkageTemplate& templ = m_context.linkageTemplate();
                return templ.canSimulate() && !templ.corners.empty();
            },
        });

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
            .run = [&actions] { actions.addPointDialog(); },
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
            .run = [&actions] { actions.mirrorHardpointsDialog(); },
            .enabledWhen = [this] { return hasPoints(); },
        });

        commands.add({
            .id = QStringLiteral("hardpoints.addPoint"),
            .text = tr("&Add Point..."),
            .icon = Icon::RowInsertBottom,
            .shortcut = QKeySequence(Qt::Key_Insert),
            .statusTip = tr("Add a point next to the selected one."),
            .ribbon = { { page, points, RibbonButton::Small, nullptr, 80 } },
            .run = [&actions] { actions.addPointDialog(); },
        });

        commands.add({
            .id = QStringLiteral("hardpoints.renamePoint"),
            .text = tr("Re&name Point..."),
            .icon = Icon::Forms,
            .statusTip = tr("Give the selected point a different name."),
            .ribbon = { { page, points, RibbonButton::Small, nullptr, 90 } },
            .run = [&actions] { actions.renamePointDialog(); },
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
            .run = [&actions] { actions.deleteSelectedPoints(); },
            .enabledWhen = [this] { return !m_context.viewport()->selectedHardpoints().empty(); },
        });
    }

    void registerShowCommands(CommandRegistry& commands)
    {
        commands.add({
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

    bool hasPoints() const { return m_context.hardpoints()->rowCount() > 0; }

    AppContext& m_context;
};

} // namespace

SUSPKIN_FEATURE(HardpointsFeature)

} // namespace suspkin
