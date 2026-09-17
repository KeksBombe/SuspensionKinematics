#include "app/session/HardpointDocument.h"

#include "app/session/ProjectSession.h"
#include "io/LinkageTemplate.h"
#include "model/HardpointConfig.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QCursor>
#include <QGuiApplication>

namespace suspkin {
namespace {

QString tr(const char* text) { return QCoreApplication::translate("HardpointDocument", text); }

/// Where an imported workbook is copied to inside a project.
const char kHardpointSubdirectory[] = "hardpoints";
/// Hardpoint edits live beside the workbook they modify, so a project folder
/// reads as what it is without a manifest to explain it.
const char kEditsRelativePath[] = "hardpoints/edits.json";

/// Read @p path with the wait cursor up.
HardpointLoadResult readWorkbook(const QString& path)
{
    QGuiApplication::setOverrideCursor(Qt::WaitCursor);
    HardpointLoadResult result = readHardpointsXlsx(path);
    QGuiApplication::restoreOverrideCursor();
    return result;
}

} // namespace

HardpointDocument::HardpointDocument(ProjectSession& session) : m_session(session) {}

QString HardpointDocument::workbookPath() const
{
    const Project& project = m_session.project();
    return project.absolutePath(project.hardpoints().workbook.relativePath);
}

QString HardpointDocument::editsPath() const
{
    return m_session.project().absolutePath(QLatin1String(kEditsRelativePath));
}

HardpointEdits HardpointDocument::pendingEdits() const
{
    if (m_session.project().hardpoints().isEmpty()) return {};
    return diffHardpoints(m_baseline, m_model.table());
}

bool HardpointDocument::writePendingEdits(QString* error) const
{
    if (m_session.project().hardpoints().isEmpty()) return true;
    return writeHardpointEdits(editsPath(), pendingEdits(), error);
}

bool HardpointDocument::clearEditsFile(QString* error) const
{
    return writeHardpointEdits(editsPath(), HardpointEdits{}, error);
}

void HardpointDocument::adoptBaseline(HardpointLoadResult& result)
{
    m_baseline = *result.table;
    m_source = std::move(result.source);
}

// ---------------------------------------------------------------------------
// Reading and writing the workbook
// ---------------------------------------------------------------------------

std::optional<HardpointTable> HardpointDocument::open(SessionMessage* problem)
{
    Project& project = m_session.project();
    if (project.hardpoints().isEmpty()) return std::nullopt;

    const QString path = workbookPath();
    if (!QFileInfo::exists(path)) {
        *problem = { tr("Workbook missing"),
                     tr("This project's hardpoint workbook is not where the project says "
                        "it is:\n\n%1\n\nImport it again to restore it.")
                         .arg(QDir::toNativeSeparators(path)) };
        project.clearHardpoints();
        m_session.markDirty();
        return std::nullopt;
    }

    HardpointLoadResult result = readHardpointsXlsx(path);
    if (!result.ok()) {
        *problem = { tr("Cannot read the project's hardpoints"),
                     tr("Failed to read:\n%1\n\n%2")
                         .arg(QDir::toNativeSeparators(path), result.error) };
        return std::nullopt;
    }
    adoptBaseline(result);
    applyMirrorProvenance(m_baseline);

    // The workbook is the baseline; what the user has actually been working on
    // is that plus whatever the edits file holds.
    QString error;
    const std::optional<HardpointEdits> edits = readHardpointEdits(editsPath(), &error);
    if (!edits) {
        *problem = { tr("Cannot read the hardpoint edits"),
                     tr("The workbook was loaded, but the edits saved alongside it could "
                        "not be:\n\n%1")
                         .arg(error) };
        return m_baseline;
    }

    HardpointTable table = applyHardpointEdits(m_baseline, *edits);
    applyMirrorProvenance(table);
    return table;
}

std::optional<HardpointImport> HardpointDocument::import(const QString& path,
                                                         SessionMessage* problem)
{
    HardpointLoadResult result = readWorkbook(path);
    if (!result.ok()) {
        *problem = { tr("Cannot import hardpoints"),
                     tr("Failed to read:\n%1\n\n%2")
                         .arg(QDir::toNativeSeparators(path), result.error) };
        return std::nullopt; // whatever was loaded stays loaded
    }

    Project& project = m_session.project();
    QString error;
    const std::optional<AssetRef> asset =
        project.importAsset(path, QLatin1String(kHardpointSubdirectory), &error);
    if (!asset) {
        *problem = { tr("Cannot copy into the project"),
                     tr("The workbook was read, but could not be copied into the "
                        "project:\n\n%1")
                         .arg(error) };
        return std::nullopt;
    }

    // A new workbook is a new set of points, so nothing about the old one --
    // including which of them were mirrors -- carries over.
    HardpointRef reference;
    reference.workbook = *asset;
    reference.sheetName = result.source.sheetName;
    project.setHardpoints(reference);
    project.setLastHardpointDirectory(QFileInfo(path).absolutePath());

    const HardpointImport imported{ result.warnings, result.elapsedMs };
    adoptBaseline(result);
    // A fresh import is its own baseline, so nothing is pending against it. A
    // stale edits file left behind here would be applied to the new workbook on
    // the next open, which is why the failure is worth reporting.
    if (!clearEditsFile(&error)) {
        *problem = { tr("Cannot clear the old edits"),
                     tr("The workbook was imported, but the previous edits file could "
                        "not be removed:\n\n%1")
                         .arg(error) };
    }
    m_session.markDirty();
    return imported;
}

bool HardpointDocument::writeTo(const QString& path, SessionMessage* problem) const
{
    QGuiApplication::setOverrideCursor(Qt::WaitCursor);
    const QString error = writeHardpointsXlsx(path, m_model.table(), m_source);
    QGuiApplication::restoreOverrideCursor();

    if (!error.isEmpty()) {
        *problem = { tr("Cannot write the workbook"),
                     tr("Failed to write:\n%1\n\n%2").arg(QDir::toNativeSeparators(path), error) };
        return false;
    }
    return true;
}

bool HardpointDocument::overwriteWorkbook(SessionMessage* problem)
{
    const QString path = workbookPath();
    if (!writeTo(path, problem)) return false;

    // Re-read, so the workbook the project holds is the baseline again and the
    // cell map covers the rows that were just appended. Without this a second
    // overwrite would append the mirrored points a second time.
    HardpointLoadResult reloaded = readHardpointsXlsx(path);
    if (!reloaded.ok()) {
        // The write succeeded, so the user's work is on disk; what failed is
        // adopting it as the new baseline. Leaving the edits pending is the safe
        // half of that, and saying so is better than a silent inconsistency.
        *problem = { tr("Workbook written, but not re-read"),
                     tr("%1 was written, but reading it back failed:\n\n%2\n\nThe "
                        "changes are still listed as pending. Reopen the project before "
                        "writing it again.")
                         .arg(QFileInfo(path).fileName(), reloaded.error) };
        return false;
    }

    adoptBaseline(reloaded);
    Project& project = m_session.project();
    HardpointRef reference = project.hardpoints();
    reference.sheetName = m_source.sheetName;
    project.setHardpoints(reference);
    // The mirrored points are ordinary rows in the workbook now; only the
    // project remembers that is what they are.
    applyMirrorProvenance(m_baseline);
    return true;
}

bool HardpointDocument::adoptNewWorkbook(const HardpointTable& table, SessionMessage* problem)
{
    Project& project = m_session.project();
    QString error;
    const std::optional<AssetRef> asset = project.createHardpointWorkbook(table, &error);
    if (!asset) {
        *problem = { tr("Cannot make a workbook"),
                     tr("The points could not be written into a workbook inside the "
                        "project:\n\n%1")
                         .arg(error) };
        return false;
    }

    // Written, then read -- never the other way round. What comes back is the
    // baseline, and the cell map that lets the next overwrite patch these rows
    // rather than append them again.
    const QString path = project.absolutePath(asset->relativePath);
    HardpointLoadResult result = readHardpointsXlsx(path);
    if (!result.ok()) {
        *problem = { tr("Cannot read the new workbook"),
                     tr("The workbook was written, but reading it back failed:\n\n%1")
                         .arg(result.error) };
        QFile::remove(path);
        return false;
    }

    HardpointRef reference;
    reference.workbook = *asset;
    reference.sheetName = result.source.sheetName;
    // The workbook cannot say which of its points are mirrors; the project
    // remembers, so a later mirror pass does not mirror them again.
    for (const Hardpoint& point : table.points)
        if (point.isMirrored()) reference.mirrored.insert(point.name, point.mirrorOf);
    project.setHardpoints(reference);

    adoptBaseline(result);
    // Nothing is pending against a workbook that was just written, and an edits
    // file left from an earlier workbook would be applied to this one.
    clearEditsFile(&error);
    applyMirrorProvenance(m_baseline);
    return true;
}

void HardpointDocument::remove()
{
    const QString workbook = workbookPath();
    if (!workbook.isEmpty()) QFile::remove(workbook);
    QFile::remove(editsPath());

    m_model.clear();
    m_source = XlsxHardpointSource{};
    m_baseline = HardpointTable{};
    m_session.project().clearHardpoints();
}

// ---------------------------------------------------------------------------
// What the workbook cannot hold
// ---------------------------------------------------------------------------

void HardpointDocument::applyMirrorProvenance(HardpointTable& table) const
{
    const QHash<QString, QString>& mirrored = m_session.project().hardpoints().mirrored;
    for (Hardpoint& point : table.points)
        point.mirrorOf = mirrored.value(point.name);
}

void HardpointDocument::captureMirrorProvenance()
{
    Project& project = m_session.project();
    HardpointRef reference = project.hardpoints();
    reference.mirrored.clear();
    for (const Hardpoint& point : m_model.table().points) {
        if (point.isMirrored()) reference.mirrored.insert(point.name, point.mirrorOf);
    }
    project.setHardpoints(reference);
    // The baseline has to agree, or every mirrored point would read as edited.
    applyMirrorProvenance(m_baseline);
}

int HardpointDocument::refreshConfig(const LinkageTemplate& templ)
{
    // What the Part columns may name comes from the template: a project that
    // describes a different car offers that car's bodies.
    m_model.setBodyCatalog(bodyCatalog(templ));

    const Project& project = m_session.project();
    HardpointConfigMap config = project.hardpoints().config;
    const int filled =
        fillMissingConfig(config, inferHardpointConfig(m_model.table(), templ, project.mirror()));
    m_model.setConfig(config);
    // What was inferred is the user's from the moment it is in front of them --
    // they are the ones who will correct it -- so it is saved like any other
    // edit rather than worked out again on every open.
    if (filled > 0) captureConfig();
    return filled;
}

void HardpointDocument::captureConfig()
{
    Project& project = m_session.project();
    HardpointRef reference = project.hardpoints();
    if (reference.config == m_model.config()) return;
    reference.config = m_model.config();
    project.setHardpoints(reference);
}

} // namespace suspkin
