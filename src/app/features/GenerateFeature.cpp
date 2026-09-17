#include "app/GenerateDialog.h"
#include "app/HardpointModel.h"
#include "app/features/SharedCommands.h"
#include "app/framework/AppContext.h"
#include "app/framework/CommandRegistry.h"
#include "app/framework/Feature.h"
#include "app/framework/FeatureRegistry.h"
#include "app/session/ProjectSession.h"
#include "io/LinkageTemplate.h"
#include "model/HardpointGenerator.h"
#include "project/Project.h"
#include "render/ViewportWidget.h"

#include <QCoreApplication>
#include <QKeySequence>
#include <QMainWindow>

#include <utility>

namespace suspkin {
namespace {

QString tr(const char* text) { return QCoreApplication::translate("GenerateFeature", text); }

/// Generate from Design: vehicle targets in, a corner's hardpoints out.
///
/// The targets are project state whether or not anything is generated from
/// them; the points go in only when the user says so, and as one step.
class GenerateFeature : public Feature {
public:
    explicit GenerateFeature(AppContext& context) : m_context(context) {}

    QString id() const override { return QStringLiteral("generate"); }

    void registerCommands(CommandRegistry& commands) override
    {
        commands.add({
            .id = QStringLiteral("hardpoints.generate"),
            .text = tr("&Generate from Design..."),
            .icon = Icon::Wand,
            .iconText = tr("Generate\nfrom Design"),
            .shortcut = QKeySequence(QStringLiteral("Ctrl+G")),
            .statusTip = tr("Work out the wishbones, the upright and the steering from vehicle "
                            "targets: track, caster, roll centre, anti-dive and the rest."),
            .ribbon = { { QStringLiteral("hardpoints"), tr("Create"), RibbonButton::Large, nullptr, 50 } },
            .run = [this] { generateFromDesignDialog(); },
            .enabledWhen = [this] {
                // The generator names what it makes through the template's
                // roles, so it needs a template that loaded and named them.
                const LinkageTemplate& templ = m_context.session().linkage().linkageTemplate();
                return templ.canSimulate() && !templ.corners.empty();
            },
        });

    }

private:
    /// The targets, the preview, and -- if the user says so -- the points.
    void generateFromDesignDialog()
    {
        ProjectSession& session = m_context.session();
        Project& project = m_context.project();
        const LinkageTemplate& templ = session.linkage().linkageTemplate();
        if (!templ.canSimulate() || templ.corners.empty()) return;

        const DesignParameters seed = project.design().value_or(initialDesign());
        GenerateDialog dialog(
            seed, templ, project.mirror(), m_context.hardpoints()->table(),
            session.hardpoints().baseline(), m_context.viewport()->hasMesh(),
            [this] { return m_context.session().chassis().query(m_context.viewport()->mesh()); },
            m_context.window());
        const int answer = dialog.exec();

        // The targets are the user's work whether or not anything was generated
        // from them: reopening the dialog starts where they left it.
        if (!project.design() || *project.design() != dialog.parameters()) {
            project.setDesign(dialog.parameters());
            m_context.markDirty();
        }
        if (answer != QDialog::Accepted) return;

        const DesignPlan plan = dialog.plan();
        if (!plan.ok()) return;

        stateGeneratedAngles(dialog.parameters());

        // The points. A project with a workbook takes them as edits against it,
        // like any other change; one without gets a workbook made for them.
        if (project.hardpoints().isEmpty()) {
            if (!adoptNewWorkbook(m_context, plan.table)) return;
        } else {
            m_context.setHardpointTable(plan.table, false);
            session.hardpoints().captureMirrorProvenance();
        }

        if (plan.steeringChanged) writeSteering(plan.steering);

        m_context.markDirty();
        // The points and the angles, one step. The steering written into the
        // template is not in it: the template is not an edit of the points. In a
        // project that had no workbook this records nothing -- the table began
        // with these points.
        session.recordEdit(tr("Generate from design"));
        showOutcome(plan);
    }

    /// An axle whose static angles the project states would have them win over
    /// the wheel axis just generated, and the scrub, trail and roll centre built
    /// off that camber would then be missed. So a generated axle that has stated
    /// angles gets the ones it was generated with; one that has none keeps
    /// reading them off the new points. Before the points go in, so the solve
    /// they trigger already sees it.
    void stateGeneratedAngles(const DesignParameters& generated)
    {
        Project& project = m_context.project();
        QHash<QString, StaticAlignment> alignment = project.alignment();
        for (const AxleDesign* axle : { &generated.front, &generated.rear }) {
            if (axle->generate && alignment.contains(axle->corner))
                alignment.insert(axle->corner, StaticAlignment{ axle->camber, axle->toe });
        }
        project.setAlignment(alignment);
    }

    /// Which axle has a rack, into the template, patched like any other edit of
    /// it. Read back afterwards, which resolves the parts and the solve against
    /// the new points as well.
    void writeSteering(const std::vector<CornerSpec>& steering)
    {
        SessionMessage problem;
        m_context.session().linkage().patch(
            [&steering](const QByteArray& bytes, QString* error) {
                return setTemplateSteering(bytes, steering, error);
            },
            tr("The points were generated, but the steering could not be written into the "
               "linkage template."),
            &problem);
        m_context.showProblem(problem);
    }

    /// The advice goes on the status bar as well as in the dialog, and is not
    /// applied: whether to move a chassis pivot is the designer's call.
    void showOutcome(const DesignPlan& plan)
    {
        const QString done = tr("Generated %1 point(s): %2 new, %3 moved.")
                                 .arg(plan.changes.size())
                                 .arg(plan.count(DesignChange::Kind::Added))
                                 .arg(plan.count(DesignChange::Kind::Moved));
        m_context.showStatus(plan.advice.isEmpty()
                                 ? done
                                 : done + QLatin1Char(' ') + plan.advice.join(QLatin1Char(' ')),
                             plan.advice.isEmpty() ? 8000 : 30000);
    }

    /// The targets a project that has never opened the generator starts from:
    /// the 2025 car, pointed at this project's own corners and side.
    DesignParameters initialDesign() const
    {
        DesignParameters design;
        const LinkageTemplate& templ = m_context.session().linkage().linkageTemplate();
        const std::vector<CornerSpec>& corners = templ.corners;

        // This project's own corners: the first is taken to be the front and the
        // second the rear, which is how every template written so far lists them.
        const bool declared = templ.steeringDeclared();
        for (const auto& [position, index] :
             { std::pair{ AxlePosition::Front, 0 }, std::pair{ AxlePosition::Rear, 1 } }) {
            AxleDesign& axle = design.axle(position);
            if (index >= static_cast<int>(corners.size())) {
                axle.generate = false;
                continue;
            }
            const CornerSpec& corner = corners[static_cast<std::size_t>(index)];
            axle.corner = corner.token;
            // Whatever the template says about this axle's rack now is the start.
            axle.steered = declared ? !corner.steeringRack.isEmpty() : true;
        }

        // The side the template's names are already on, when the table says.
        if (!corners.empty()) {
            const MechanismTemplate names = instantiateMechanism(
                templ.mechanism, corners.front().token, false, m_context.project().mirror());
            if (const Hardpoint* centre =
                    m_context.session().hardpoints().model().table().find(names.wheelCenter))
                design.side = centre->y() < 0.0 ? DesignSide::Right : DesignSide::Left;
        }
        return design;
    }

    AppContext& m_context;
};

} // namespace

SUSPKIN_FEATURE(GenerateFeature)

} // namespace suspkin
