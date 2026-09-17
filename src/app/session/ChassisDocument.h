#pragma once

#include "app/session/SessionMessage.h"
#include "geom/MeshTopology.h"
#include "geom/TriMesh.h"

#include <QString>

#include <memory>
#include <optional>

namespace suspkin {

class MeshQuery;
class ProjectSession;

/// A chassis read from disk, ready to be handed to the viewport.
struct ChassisModel {
    TriMesh mesh;
    EdgeSet edges;
};

/// The car the suspension is bolted to: the project's copy of the chassis, what
/// the status line says about it, and the query the generator casts rays at.
///
/// The mesh itself is the viewport's once it is drawn -- a million-triangle
/// chassis is not worth holding twice -- so the query is built from the mesh the
/// caller passes in.
class ChassisDocument {
public:
    explicit ChassisDocument(ProjectSession& session);
    ~ChassisDocument();

    /// Read the project's own copy. Nothing, with @p problem empty, when the
    /// project has none; nothing, with @p problem said, when it cannot be read.
    /// A copy that has gone is forgotten, so the project stops claiming it.
    std::optional<ChassisModel> open(SessionMessage* problem);

    /// Read @p path and copy it into the project, which from then on loads the
    /// copy. A file that cannot be read is not copied.
    std::optional<ChassisModel> import(const QString& path, SessionMessage* problem);

    /// Delete the project's copy and forget it. The file it was imported from
    /// is not touched.
    void remove();

    /// What the status line says about the chassis: its file, its size and its
    /// format. Empty when there is none.
    const QString& summary() const { return m_summary; }

    /// @p mesh as something rays can be cast at, built the first time it is
    /// asked for and dropped whenever the geometry changes.
    const MeshQuery* query(const TriMesh& mesh);

private:
    /// The status line's words for @p model, read from @p path in @p format.
    QString describe(const QString& path, const ChassisModel& model,
                     const QString& format) const;

    ProjectSession& m_session;
    QString m_summary;
    /// Built on first use -- a million-triangle chassis takes a moment -- and
    /// reset whenever the geometry is replaced or removed.
    std::unique_ptr<MeshQuery> m_query;
};

} // namespace suspkin
