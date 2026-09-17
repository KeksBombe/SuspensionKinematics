#include "app/session/LinkageDocument.h"

#include "app/session/ProjectSession.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>

#include <algorithm>

namespace suspkin {
namespace {

QString tr(const char* text) { return QCoreApplication::translate("LinkageDocument", text); }

/// Where an imported template is copied to, next to everything else the project
/// owns a copy of.
const char kLinkageSubdirectory[] = "linkage";

} // namespace

LinkageDocument::LinkageDocument(ProjectSession& session) : m_session(session) {}

bool LinkageDocument::installBuiltin(SessionMessage* problem)
{
    const QByteArray bytes = builtinLinkageTemplateBytes();
    if (bytes.isEmpty()) return false;

    Project& project = m_session.project();
    const QString relative = linkageTemplateRelativePath();
    QString error;
    if (!project.writeFile(relative, bytes, &error)) {
        *problem = { tr("Cannot write the linkage template"),
                     tr("The parts between the hardpoints could not be set up:\n\n%1").arg(error) };
        return false;
    }

    AssetRef asset;
    asset.relativePath = relative;
    // No importedFrom: it did not come from anywhere on this machine.
    asset.importedAt = QDateTime::currentDateTimeUtc();
    project.setLinkageTemplate(asset);
    m_session.markDirty();
    return true;
}

bool LinkageDocument::load(SessionMessage* problem)
{
    Project& project = m_session.project();
    QString path = project.absolutePath(project.linkageTemplate().relativePath);

    // A template dropped into the project by hand, without the manifest being
    // edited to match, is adopted rather than overwritten: it is a file the user
    // put there on purpose, and the manifest is ours to fix, not theirs.
    if (path.isEmpty() || !QFileInfo::exists(path)) {
        const QString conventional = project.absolutePath(linkageTemplateRelativePath());
        if (QFileInfo::exists(conventional)) {
            AssetRef asset;
            asset.relativePath = linkageTemplateRelativePath();
            project.setLinkageTemplate(asset);
            path = conventional;
        }
    }

    // A project made before templates existed, or one whose copy was deleted,
    // gets the built-in one written into it. That is the whole of "the template
    // is saved in the project": from here on it is an ordinary project file the
    // user can open and edit.
    if (path.isEmpty() || !QFileInfo::exists(path)) {
        if (!installBuiltin(problem)) return false;
        path = project.absolutePath(project.linkageTemplate().relativePath);
    }

    const LinkageTemplateLoadResult result = readLinkageTemplateFile(path);
    if (!result.ok()) {
        // Deliberately not repaired by overwriting: the file is the user's, and
        // silently replacing an edit they made would be worse than not drawing.
        *problem = { tr("Cannot read the linkage template"),
                     tr("The parts between the hardpoints cannot be drawn:\n\n%1\n\n"
                        "Fix the file, or use Linkage > Reset to Built-in Template.")
                         .arg(result.error) };
        m_template = LinkageTemplate{};
        m_session.resolveFromTable();
        return false;
    }

    m_template = *result.templ;
    adoptTemplateSteering();
    m_session.resolveFromTable();
    return true;
}

bool LinkageDocument::patch(const Patch& patch, const QString& failure, SessionMessage* problem)
{
    QString error;
    // Patched rather than rewritten, the same way a workbook is: this file is
    // the user's, and anything in it this version does not model -- a note, a
    // part, a key from a later release -- has to come out the other side.
    if (!patchFile(m_session.project().linkageTemplate().relativePath, patch, &error)) {
        *problem = { tr("Cannot write the linkage template"),
                     QStringLiteral("%1\n\n%2").arg(
                         failure, error.isEmpty() ? tr("The template could not be read.") : error) };
        return false;
    }

    // Read back rather than patched in memory, so what the solver sees is what
    // the file says -- the same re-read Overwrite Workbook does, and for the
    // same reason.
    load(problem);
    m_session.markDirty();
    return true;
}

bool LinkageDocument::patchFile(const QString& relative, const Patch& patch, QString* error) const
{
    const Project& project = m_session.project();
    QByteArray patched;
    if (const std::optional<QByteArray> bytes = project.readFile(relative, error))
        patched = patch(*bytes, error);
    return !patched.isEmpty() && project.writeFile(relative, patched, error);
}

std::optional<QStringList> LinkageDocument::import(const QString& path, SessionMessage* problem)
{
    // Read before copying: a file that is not a template should not land in the
    // project and replace the one that works.
    const LinkageTemplateLoadResult result = readLinkageTemplateFile(path);
    if (!result.ok()) {
        *problem = { tr("Cannot import the linkage template"),
                     tr("Failed to read:\n%1\n\n%2")
                         .arg(QDir::toNativeSeparators(path), result.error) };
        return std::nullopt;
    }

    Project& project = m_session.project();
    QString error;
    const std::optional<AssetRef> asset =
        project.importAsset(path, QLatin1String(kLinkageSubdirectory), &error);
    if (!asset) {
        *problem = { tr("Cannot copy into the project"),
                     tr("The template was read, but could not be copied into the "
                        "project:\n\n%1")
                         .arg(error) };
        return std::nullopt;
    }

    project.setLinkageTemplate(*asset);
    m_template = *result.templ;
    m_session.resolveFromTable();
    m_session.markDirty();
    return result.warnings;
}

void LinkageDocument::resolve(const HardpointTable& table)
{
    m_parts = buildLinkage(m_template, table, m_session.project().mirror());
}

void LinkageDocument::adoptTemplateSteering()
{
    m_steeringNote.clear();
    if (m_template.isEmpty() || m_template.steeringDeclared()) return;

    // A template written before steering was a role says nothing about it, and
    // every axle then steers -- which is how a rear toe link ends up being
    // dragged sideways by a rack the car has not got.
    //
    // If the file is recognisably the built-in template, the answer is known and
    // is written in. If it is somebody's own, nothing is touched: the same rule
    // the reader already follows for a template it cannot parse.
    const QString unstated = tr("This project's template does not say where the steering rack is, "
                                "so every axle can be steered. Linkage > Steering Rack says which "
                                "axle has one.");
    const std::optional<std::vector<CornerSpec>> corners = builtinSteering();
    if (!corners) {
        m_steeringNote = unstated;
        return;
    }

    QString error;
    const Patch write = [&corners](const QByteArray& bytes, QString* patchError) {
        return setTemplateSteering(bytes, *corners, patchError);
    };
    if (!patchFile(m_session.project().linkageTemplate().relativePath, write, &error)) {
        // Not worth a dialog: the project still works, it just still says
        // nothing about steering.
        m_steeringNote = unstated;
        return;
    }

    m_template.corners = *corners;
    QStringList steered;
    for (const CornerSpec& corner : *corners) {
        if (!corner.steeringRack.isEmpty())
            steered << (corner.label.isEmpty() ? corner.token : corner.label);
    }
    m_steeringNote = tr("This project's template did not say where the steering rack is, so the "
                        "built-in answer was written into it: %1. Linkage > Steering Rack "
                        "changes it.")
                         .arg(steered.isEmpty() ? tr("none") : steered.join(QStringLiteral(", ")));
    m_session.markDirty();
}

std::optional<std::vector<CornerSpec>> LinkageDocument::builtinSteering() const
{
    const LinkageTemplate builtin = builtinLinkageTemplate();
    const bool sameMechanism = !builtin.corners.empty() && !builtin.mechanism.tieRodInboard.isEmpty()
                               && m_template.mechanism.tieRodInboard
                                      == builtin.mechanism.tieRodInboard
                               && m_template.corners.size() == builtin.corners.size();
    if (!sameMechanism) return std::nullopt;

    std::vector<CornerSpec> corners = m_template.corners;
    for (CornerSpec& corner : corners) {
        const auto match = std::find_if(
            builtin.corners.begin(), builtin.corners.end(),
            [&corner](const CornerSpec& known) { return known.token == corner.token; });
        if (match == builtin.corners.end()) return std::nullopt;
        corner.steeringRack = match->steeringRack;
    }
    return corners;
}

} // namespace suspkin
