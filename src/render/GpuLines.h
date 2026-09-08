#pragma once

#include <QOpenGLFunctions_3_3_Core>
#include <QVector3D>

#include <vector>

namespace suspkin {

/// GPU-side line segments for the linkage, uploaded as one buffer that is drawn
/// in ranges so each kind of part can have its own colour without a per-vertex
/// attribute or a buffer each.
///
/// Like GpuMesh and GpuPoints this is only a cache of CPU-side data:
/// initializeGL() can run again at any time, so everything here has to be
/// rebuildable from scratch.
class GpuLines {
public:
    /// @p vertices is a flat list of segment endpoints: two per segment.
    void upload(QOpenGLFunctions_3_3_Core& gl, const std::vector<QVector3D>& vertices);
    void destroy(QOpenGLFunctions_3_3_Core& gl);

    /// Draw @p count vertices starting at @p first, both in vertices.
    void draw(QOpenGLFunctions_3_3_Core& gl, int first, int count) const;

    bool isValid() const { return m_vao != 0 && m_count > 0; }
    int count() const { return m_count; }

private:
    GLuint m_vao = 0;
    GLuint m_vbo = 0;
    int m_count = 0;
};

} // namespace suspkin
