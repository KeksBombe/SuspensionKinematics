#pragma once

#include "geom/TriMesh.h"

#include <QString>

#include <optional>

namespace suspkin {

/// Outcome of loading any supported geometry file. Errors are returned rather
/// than thrown so the caller can put the message straight in front of the user.
struct MeshLoadResult {
    std::optional<TriMesh> mesh;   ///< empty on failure
    QString error;                 ///< human-readable; empty on success
    QString formatName;            ///< what the loader decided it was reading
    int skippedDegenerate = 0;     ///< zero-area triangles dropped
    bool wasBinary = false;        ///< STL only: which of the two parsers ran
    qint64 elapsedMs = 0;

    bool ok() const { return mesh.has_value(); }
};

} // namespace suspkin
