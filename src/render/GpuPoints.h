#pragma once

#include <QOpenGLFunctions_3_3_Core>
#include <QVector3D>

#include <vector>

namespace suspkin {

/// GPU-side positions for the hardpoint markers, drawn as point sprites.
///
/// Like GpuMesh this is only a cache of CPU-side data: initializeGL() can run
/// again at any time, so everything here has to be rebuildable from scratch.
class GpuPoints {
public:
    void upload(QOpenGLFunctions_3_3_Core& gl, const std::vector<QVector3D>& positions);
    void destroy(QOpenGLFunctions_3_3_Core& gl);

    void drawAll(QOpenGLFunctions_3_3_Core& gl) const;
    /// One marker on its own, so the selected point can be drawn in its own
    /// colour without a second buffer or a per-point attribute.
    void drawOne(QOpenGLFunctions_3_3_Core& gl, int index) const;

    bool isValid() const { return m_vao != 0 && m_count > 0; }
    int count() const { return m_count; }

private:
    GLuint m_vao = 0;
    GLuint m_vbo = 0;
    int m_count = 0;
};

} // namespace suspkin
