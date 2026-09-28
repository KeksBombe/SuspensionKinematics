#include "app/session/WheelsDocument.h"

#include "app/session/ProjectSession.h"
#include "io/MeshImport.h"
#include "model/Simulation.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QCursor>
#include <QGuiApplication>
#include <QQuaternion>
#include <QStringList>

namespace suspkin {
namespace {

QString tr(const char* text) { return QCoreApplication::translate("WheelsDocument", text); }

/// The tyre and rim models, which together are a wheel. They share a directory,
/// so they are copied in under fixed names rather than their own: two files that
/// happen to be called the same would otherwise be one file.
const char kWheelSubdirectory[] = "wheels";
const char kTyreStem[] = "tyre";
const char kRimStem[] = "rim";

} // namespace

WheelsDocument::WheelsDocument(ProjectSession& session) : m_session(session) {}

std::optional<WheelModels> WheelsDocument::open(SessionMessage* problem)
{
    Project& project = m_session.project();
    WheelsRef wheels = project.wheels();
    if (wheels.isEmpty()) {
        m_placements.clear();
        return std::nullopt;
    }

    // The two models are read the same way, so they are read in a loop rather
    // than twice by hand.
    WheelModels models;
    struct Slot {
        AssetRef* asset;
        TriMesh* mesh;
        EdgeSet* edges;
    };
    const Slot readers[2] = { { &wheels.tyre, &models.tyre, &models.tyreEdges },
                            { &wheels.rim, &models.rim, &models.rimEdges } };

    QStringList problems;
    bool referencesChanged = false;

    QGuiApplication::setOverrideCursor(Qt::WaitCursor);
    for (const Slot& slot : readers) {
        if (slot.asset->isEmpty()) continue;

        const QString path = project.absolutePath(slot.asset->relativePath);
        if (!QFileInfo::exists(path)) {
            // The project claims a model whose copy has gone. Say so, and stop
            // claiming it, rather than opening a window that quietly shows less.
            problems << tr("%1 is not where the project says it is.")
                            .arg(QDir::toNativeSeparators(path));
            *slot.asset = AssetRef{};
            referencesChanged = true;
            continue;
        }

        MeshLoadResult result = importMeshFile(path);
        if (!result.ok()) {
            // The file is there and unreadable, which is a different problem:
            // the reference stays, so a fixed file comes back on the next open.
            problems << tr("%1: %2").arg(QFileInfo(path).fileName(), result.error);
            continue;
        }
        *slot.edges = buildEdges(*result.mesh);
        *slot.mesh = std::move(*result.mesh);
    }
    QGuiApplication::restoreOverrideCursor();

    if (referencesChanged) {
        project.setWheels(wheels);
        m_session.markDirty();
    }
    if (!problems.isEmpty()) {
        *problem = { tr("Cannot open the project's wheels"),
                     tr("The wheels could not be drawn as this project describes "
                        "them:\n\n%1\n\nUse Geometry > Add Wheels to set them up "
                        "again.")
                         .arg(problems.join(QStringLiteral("\n"))) };
    }
    return models;
}

bool WheelsDocument::apply(const WheelSpec& spec, const QString& tyrePath, const QString& rimPath,
                           SessionMessage* failure, QList<SessionMessage>* problems)
{
    Project& project = m_session.project();
    WheelsRef wheels = project.wheels();
    wheels.spec = spec;

    struct Slot {
        AssetRef* asset;
        QString chosen;   ///< empty for "no model here"
        const char* stem; ///< what the copy inside the project is called
        bool replace = false;
    };
    Slot models[2] = { { &wheels.tyre, tyrePath, kTyreStem, false },
                      { &wheels.rim, rimPath, kRimStem, false } };

    // Read whatever is new before anything is copied or deleted: a file that
    // cannot be loaded has no business being written into the project, and one
    // bad model should not half-apply the other.
    QGuiApplication::setOverrideCursor(Qt::WaitCursor);
    QString failedPath;
    QString error;
    for (Slot& slot : models) {
        if (slot.chosen.isEmpty()) continue;
        // A path handed back unchanged is the project's own copy: keep it, and
        // keep the note of where it was originally imported from.
        const QString current = project.absolutePath(slot.asset->relativePath);
        if (!current.isEmpty()
            && QFileInfo(slot.chosen).absoluteFilePath() == QFileInfo(current).absoluteFilePath()) {
            continue;
        }
        const MeshLoadResult result = importMeshFile(slot.chosen);
        if (!result.ok()) {
            failedPath = slot.chosen;
            error = result.error;
            break;
        }
        slot.replace = true;
    }
    QGuiApplication::restoreOverrideCursor();

    if (!error.isEmpty()) {
        *failure = { tr("Cannot import the tyre or rim model"),
                     tr("Failed to load:\n%1\n\n%2\n\nNothing was changed.")
                         .arg(QDir::toNativeSeparators(failedPath), error) };
        return false;
    }

    QString importedFrom;
    for (Slot& slot : models) {
        if (slot.chosen.isEmpty()) {
            // Cleared in the dialog: the copy inside the project goes with it.
            if (!slot.asset->isEmpty()) QFile::remove(project.absolutePath(slot.asset->relativePath));
            *slot.asset = AssetRef{};
            continue;
        }
        if (!slot.replace) continue;

        const QString suffix = QFileInfo(slot.chosen).suffix();
        const QString target = suffix.isEmpty()
                                   ? QString::fromLatin1(slot.stem)
                                   : QStringLiteral("%1.%2").arg(QLatin1String(slot.stem), suffix);
        QString copyError;
        const std::optional<AssetRef> asset = project.importAssetAs(
            slot.chosen, QLatin1String(kWheelSubdirectory), target, &copyError);
        if (!asset) {
            // The model read, so this is a disk or permission problem. Whatever
            // else worked is kept rather than rolled back.
            problems->append({ tr("Cannot copy into the project"),
                               tr("The model was read, but could not be copied into the "
                                  "project:\n\n%1")
                                   .arg(copyError) });
            continue;
        }
        // A model in another format supersedes the previous copy, which would
        // otherwise sit in the project forever under its own extension.
        if (!slot.asset->isEmpty() && slot.asset->relativePath != asset->relativePath)
            QFile::remove(project.absolutePath(slot.asset->relativePath));
        *slot.asset = *asset;
        importedFrom = slot.chosen;
    }

    project.setWheels(wheels);
    // Only when something actually came in from outside: keeping a model the
    // project already had says nothing about where the user keeps their CAD.
    if (!importedFrom.isEmpty())
        project.setLastGeometryDirectory(QFileInfo(importedFrom).absolutePath());
    m_session.markDirty();
    return true;
}

void WheelsDocument::remove()
{
    Project& project = m_session.project();
    const WheelsRef wheels = project.wheels();
    for (const AssetRef* asset : { &wheels.tyre, &wheels.rim }) {
        if (!asset->isEmpty()) QFile::remove(project.absolutePath(asset->relativePath));
    }

    project.clearWheels();
    m_placements.clear();
    m_session.markDirty();
}

void WheelsDocument::place(const HardpointTable& table, const WheelRotations& designAttitudes,
                           const SimulationPose& pose)
{
    const WheelsRef& wheels = m_session.project().wheels();
    m_placements = wheels.isEmpty() ? std::vector<WheelPlacement>{}
                                    : resolveWheels(wheels.spec, pose.layOver(table));
    // The models are drawn upright and square to the car, so each is first
    // turned to its corner's static camber and toe.
    orientWheels(m_placements, designAttitudes);
    // A wheel is bolted to its upright, so it goes where the upright goes and
    // turns the way the upright turns. Without this the models slide about the
    // car on steering lock without ever pointing anywhere.
    orientWheels(m_placements, pose.wheelRotations());
    // And a rolled body leans all four with it. The upright's turn is measured
    // in the body, so it comes first and the body's after it. Their centres are
    // already where the body put them: they came out of the pose.
    if (pose.bodyMotion) {
        const QQuaternion body = pose.bodyMotion->toQuaternion();
        for (WheelPlacement& placement : m_placements)
            placement.rotation = body * placement.rotation;
    }
}

void WheelsDocument::followRenames(const QHash<QString, QString>& renamed)
{
    if (renamed.isEmpty()) return;

    Project& project = m_session.project();
    WheelsRef wheels = project.wheels();
    bool moved = false;
    for (const WheelCorner corner : kWheelCorners) {
        const auto to = renamed.constFind(wheels.spec.point(corner));
        if (to == renamed.constEnd()) continue;
        wheels.spec.setPoint(corner, *to);
        moved = true;
    }
    if (moved) project.setWheels(wheels);
}

} // namespace suspkin
