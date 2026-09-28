#include "app/HardpointModel.h"
#include "app/StaticAnglesDialog.h"
#include "app/SteeringDialog.h"
#include "app/framework/AppContext.h"
#include "app/framework/CommandRegistry.h"
#include "app/framework/Feature.h"
#include "app/framework/FeatureRegistry.h"
#include "app/session/ProjectSession.h"
#include "io/LinkageTemplate.h"
#include "model/Simulation.h"
#include "project/Project.h"

#include <QCoreApplication>
#include <QMainWindow>

namespace suspkin {
namespace {

QString tr(const char* text) { return QCoreApplication::translate("MechanismFeature", text); }

/// What the car is made of, as far as the solver is concerned: which axle has a
/// steering rack, and the static camber and toe each axle is set to.
///
/// Its buttons are on the Linkage tab, after the parts: RibbonSlot::order puts
/// them there, whichever file they come from.
class MechanismFeature : public Feature {
public:
    explicit MechanismFeature(AppContext& context) : m_context(context) {}

    QString id() const override { return QStringLiteral("mechanism"); }

    void registerCommands(CommandRegistry& commands) override
    {
        const QString page = QStringLiteral("linkage");

        commands.add({
            .id = QStringLiteral("linkage.steering"),
            .text = tr("Steering &Rack..."),
            .icon = Icon::SteeringWheel,
            .iconText = tr("Steering\nRack"),
            .statusTip = tr("Say where the steering rack is attached: which axle has one, and "
                            "which points it moves. An axle without a rack is not offered a "
                            "steer sweep."),
            .ribbon = { { page, tr("Steering"), RibbonButton::Large, nullptr, 40 } },
            .run = [this] { steeringDialog(); },
            .enabledWhen = [this] {
                // A template with no corners has no axle to ask about, and one
                // that failed to load has nothing to write into.
                const LinkageTemplate& templ = m_context.session().linkage().linkageTemplate();
                return !templ.isEmpty() && !templ.corners.empty();
            },
        });

        commands.add({
            .id = QStringLiteral("linkage.staticAngles"),
            .text = tr("Static &Camber and Toe..."),
            .icon = Icon::Angle,
            .iconText = tr("Camber\nand Toe"),
            .statusTip = tr("Set each axle's static camber and toe as numbers, the way Lotus's "
                            "Set Static Angles does. The wheel axis and the contact patch are "
                            "computed from them."),
            .ribbon = { { page, tr("Alignment"), RibbonButton::Large, nullptr, 50 } },
            .run = [this] { staticAnglesDialog(); },
            // Angles are set on a wheel, so there has to be an axle that solves.
            .enabledWhen = [this] {
                return !m_context.session().simulation().simulation().isEmpty();
            },
        });
    }

private:
    /// Let the user say which axle the rack drives, and write it into the
    /// project's own template.
    void steeringDialog()
    {
        LinkageDocument& linkage = m_context.session().linkage();
        const LinkageTemplate& templ = linkage.linkageTemplate();
        if (templ.isEmpty() || templ.corners.empty()) return;

        SteeringDialog dialog(templ, m_context.project().mirror(), m_context.hardpoints()->table(),
                              m_context.window());
        if (dialog.exec() != QDialog::Accepted) return;

        const std::vector<CornerSpec> corners = dialog.corners();
        bool changed = corners.size() != templ.corners.size();
        for (std::size_t i = 0; !changed && i < corners.size(); ++i) {
            changed = corners[i].steeringRack != templ.corners[i].steeringRack
                      || corners[i].steeringStated != templ.corners[i].steeringStated;
        }
        if (!changed) return;

        SessionMessage problem;
        const bool written = linkage.patch(
            [&corners](const QByteArray& bytes, QString* error) {
                return setTemplateSteering(bytes, corners, error);
            },
            tr("The steering could not be saved."), &problem);
        m_context.showProblem(problem);
        if (written) m_context.showStatus(tr("Steering written to the linkage template."), 5000);
    }

    /// Let the user state each axle's static camber and toe -- or hand an axle
    /// back to its hardpoints -- and solve again with them.
    void staticAnglesDialog()
    {
        const std::vector<StaticAnglesAxle> rows = staticAnglesAxles();
        if (rows.empty()) return;

        StaticAnglesDialog dialog(rows, m_context.window());
        if (dialog.exec() != QDialog::Accepted) return;

        // Every axle the dialog showed is replaced; an axle it could not show --
        // one that does not solve today -- keeps whatever the project said about
        // it, rather than losing it to a table that is half way through an edit.
        Project& project = m_context.project();
        QHash<QString, StaticAlignment> alignment = project.alignment();
        for (const StaticAnglesAxle& row : rows) alignment.remove(row.token);
        const QHash<QString, StaticAlignment> chosen = dialog.alignment();
        for (auto it = chosen.begin(); it != chosen.end(); ++it)
            alignment.insert(it.key(), it.value());
        if (alignment == project.alignment()) return;

        project.setAlignment(alignment);
        m_context.session().resolveMechanism();
        m_context.markDirty();
        // A step of its own: Generate from Design writes these too, so they are in
        // every state, and an angle changed without a step would be put back by
        // the next undo of anything.
        m_context.session().recordEdit(tr("Set static camber and toe"));
        m_context.showStatus(tr("Static camber and toe saved with the project."), 5000);
    }

    /// Every axle that solves, as the static angles dialog shows it: what the
    /// hardpoints say on their own, and what the project states.
    std::vector<StaticAnglesAxle> staticAnglesAxles() const
    {
        const LinkageTemplate& linkage = m_context.session().linkage().linkageTemplate();
        const MechanismTemplate& mechanism = linkage.mechanism;
        const HardpointTable& table = m_context.hardpoints()->table();
        if (mechanism.isEmpty() || table.isEmpty()) return {};

        std::vector<CornerSpec> axles = linkage.corners;
        if (axles.empty()) axles.push_back(CornerSpec{});
        const bool steeringDeclared = linkage.steeringDeclared();
        const Project& project = m_context.project();

        std::vector<StaticAnglesAxle> rows;
        for (const CornerSpec& corner : axles) {
            // Built without the project's angles, which is the only way to find
            // out what the hardpoints would say on their own -- the numbers an
            // axle goes back to, and the ones a newly ticked axle starts from.
            const AxleSolver bare =
                AxleSolver::build(mechanism, corner, table, project.mirror(), steeringDeclared);
            const std::optional<CornerSolver>& near = bare.left() ? bare.left() : bare.right();
            if (!near) continue;

            StaticAnglesAxle row;
            row.token = corner.token;
            row.label = bare.label().isEmpty() ? tr("Suspension") : bare.label();
            row.fromHardpoints =
                StaticAlignment{ near->designPose().camber, near->designPose().toe };
            row.source = near->wheelAttitude();
            row.sourcePoint = row.source == WheelAttitude::WheelAxis
                                  ? near->mechanism().wheelAxis
                                  : near->mechanism().contactPatch;
            row.stated = project.alignmentFor(corner.token);
            row.wheelCenter = near->designPose().wheelCenter;
            row.side = near->side();
            // The computed patch sits on the ground whatever the angles, so its
            // height is the ground's.
            row.groundZ = near->designPose().contactPatch.z;
            rows.push_back(row);
        }
        return rows;
    }

    AppContext& m_context;
};

} // namespace

SUSPKIN_FEATURE(MechanismFeature)

} // namespace suspkin
