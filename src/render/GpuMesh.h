#pragma once

#include "geom/MeshTopology.h"
#include "geom/TriMesh.h"

#include <QOpenGLFunctions_3_3_Core>

namespace suspkin {

/// GPU-side buffers for one mesh: a non-indexed triangle stream for the solid
/// pass, plus a welded-position stream indexed into triangle edges for the
/// tessellation overlay.
///
/// This is a cache, never the source of truth. The CPU-side TriMesh outlives it,
/// because initializeGL() can run again at any time and everything here has to be
/// rebuildable from scratch.
class GpuMesh {
public:
    void upload(QOpenGLFunctions_3_3_Core& gl, const TriMesh& mesh, const EdgeSet& edges);
    void destroy(QOpenGLFunctions_3_3_Core& gl);

    void drawSolid(QOpenGLFunctions_3_3_Core& gl) const;
    void drawLines(QOpenGLFunctions_3_3_Core& gl) const;

    bool isValid() const { return m_solidVao != 0; }

private:
    GLuint m_solidVao = 0;
    GLuint m_solidVbo = 0;

    GLuint m_lineVbo = 0;   ///< welded positions
    GLuint m_lineVao = 0;
    GLuint m_lineEbo = 0;

    GLsizei m_solidVertexCount = 0;
    GLsizei m_lineIndexCount = 0;
};

} // namespace suspkin
