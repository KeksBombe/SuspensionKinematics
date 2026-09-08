#pragma once

#include "io/MeshLoadResult.h"

#include <QString>

#include <cstdint>

namespace suspkin {

/// Load an ASCII or binary STL. The format is detected from the file size, not
/// from a leading "solid" token -- see the implementation for why.
///
/// Deliberately free of any Qt widget or OpenGL dependency, so it is unit
/// testable and can later be moved onto a worker thread unchanged.
MeshLoadResult readStl(const QString& path);

/// Decide whether @p data is a binary STL, and if so report its triangle count.
/// Exposed for testing because the arithmetic here has a sharp edge.
bool isBinaryStl(const char* data, qint64 size, std::uint32_t* triCountOut = nullptr);

} // namespace suspkin
