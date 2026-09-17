#include "app/session/ChassisDocument.h"

#include "app/session/ProjectSession.h"
#include "geom/MeshQuery.h"
#include "io/MeshImport.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QCursor>
#include <QGuiApplication>
#include <QLocale>

namespace suspkin {
namespace {

QString tr(const char* text) { return QCoreApplication::translate("ChassisDocument", text); }

/// Where imported geometry is copied to inside a project.
const char kGeometrySubdirectory[] = "geometry";

/// Read @p path with the wait cursor up.
MeshLoadResult readMesh(const QString& path)
{
    QGuiApplication::setOverrideCursor(Qt::WaitCursor);
    MeshLoadResult result = importMeshFile(path);
    QGuiApplication::restoreOverrideCursor();
    return result;
}

} // namespace

ChassisDocument::ChassisDocument(ProjectSession& session) : m_session(session) {}

ChassisDocument::~ChassisDocument() = default;

std::optional<ChassisModel> ChassisDocument::open(SessionMessage* problem)
{
    Project& project = m_session.project();
    const AssetRef& asset = project.geometry();
    if (asset.isEmpty()) return std::nullopt;

    const QString path = project.absolutePath(asset.relativePath);
    if (!QFileInfo::exists(path)) {
        // The project says it has geometry and the copy is gone: say so rather
        // than opening a window that quietly shows nothing.
        *problem = { tr("Chassis missing"),
                     tr("This project's chassis file is not where the project says it "
                        "is:\n\n%1\n\nImport it again to restore it.")
                         .arg(QDir::toNativeSeparators(path)) };
        project.clearGeometry();
        m_session.markDirty();
        return std::nullopt;
    }

    MeshLoadResult result = readMesh(path);
    if (!result.ok()) {
        *problem = { tr("Cannot open the project's chassis"),
                     tr("Failed to load:\n%1\n\n%2")
                         .arg(QDir::toNativeSeparators(path), result.error) };
        return std::nullopt;
    }

    ChassisModel model{ std::move(*result.mesh), {} };
    model.edges = buildEdges(model.mesh);
    m_summary = describe(path, model, result.formatName);
    m_query.reset();
    return model;
}

std::optional<ChassisModel> ChassisDocument::import(const QString& path, SessionMessage* problem)
{
    // Read it before copying it in: a file that cannot be loaded has no business
    // being written into the project.
    MeshLoadResult result = readMesh(path);
    if (!result.ok()) {
        *problem = { tr("Cannot import file"),
                     tr("Failed to load:\n%1\n\n%2")
                         .arg(QDir::toNativeSeparators(path), result.error) };
        return std::nullopt;
    }

    Project& project = m_session.project();
    QString error;
    const std::optional<AssetRef> asset =
        project.importAsset(path, QLatin1String(kGeometrySubdirectory), &error);
    if (!asset) {
        *problem = { tr("Cannot copy into the project"),
                     tr("The chassis loaded, but could not be copied into the "
                        "project:\n\n%1")
                         .arg(error) };
        return std::nullopt;
    }

    ChassisModel model{ std::move(*result.mesh), {} };
    model.edges = buildEdges(model.mesh);
    const QString summary =
        tr("%1, %2 ms").arg(describe(path, model, result.formatName)).arg(result.elapsedMs);
    m_summary = result.skippedDegenerate > 0
                    ? tr("%1  [%2 degenerate skipped]").arg(summary).arg(result.skippedDegenerate)
                    : summary;
    m_query.reset(); // it was a query of the geometry that has just gone

    project.setGeometry(*asset);
    project.setLastGeometryDirectory(QFileInfo(path).absolutePath());
    m_session.markDirty();
    return model;
}

void ChassisDocument::remove()
{
    Project& project = m_session.project();
    const AssetRef asset = project.geometry();
    if (!asset.isEmpty()) QFile::remove(project.absolutePath(asset.relativePath));

    m_query.reset();
    m_summary.clear();
    project.clearGeometry();
    m_session.markDirty();
}

const MeshQuery* ChassisDocument::query(const TriMesh& mesh)
{
    if (mesh.isEmpty()) return nullptr;
    if (!m_query) {
        QGuiApplication::setOverrideCursor(Qt::WaitCursor);
        m_query = std::make_unique<MeshQuery>(mesh);
        QGuiApplication::restoreOverrideCursor();
    }
    return m_query.get();
}

QString ChassisDocument::describe(const QString& path, const ChassisModel& model,
                                  const QString& format) const
{
    const QLocale locale;
    return tr("%1  -  %2 triangles, %3 vertices, %4 edges  -  %5")
        .arg(QFileInfo(path).fileName(), locale.toString(qulonglong(model.mesh.triangleCount())),
             locale.toString(qulonglong(model.mesh.vertexCount())),
             locale.toString(qulonglong(model.edges.allCount())), format);
}

} // namespace suspkin
