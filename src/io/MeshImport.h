#pragma once

#include "io/MeshLoadResult.h"

#include <QString>

namespace suspkin {

/// Load any geometry file this build supports, dispatching on the extension.
MeshLoadResult importMeshFile(const QString& path);

/// True when this build was compiled with STEP support (Open CASCADE present).
bool stepImportSupported();

/// Name filter for the file dialog, reflecting what this build can actually open.
QString importFileFilter();

} // namespace suspkin
