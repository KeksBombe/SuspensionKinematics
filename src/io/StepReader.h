#pragma once

#include "io/MeshLoadResult.h"

#include <QString>

namespace suspkin {

/// Load a STEP (ISO 10303) file and tessellate it into a triangle mesh.
///
/// STEP is a boundary representation -- trimmed NURBS surfaces, not triangles --
/// so this needs a real geometry kernel. Open CASCADE reads the file and meshes
/// the resulting shape; the tessellation tolerance is relative to the model's own
/// size, so a 10 mm bracket and a 3 m chassis both come out sensibly.
///
/// Only compiled when the build found Open CASCADE.
MeshLoadResult readStep(const QString& path);

} // namespace suspkin
