#include "render/GpuPoints.h"

namespace suspkin {

void GpuPoints::upload(QOpenGLFunctions_3_3_Core& gl, const std::vector<QVector3D>& positions)
{
    destroy(gl);
    if (positions.empty()) return;

    std::vector<float> data;
    data.reserve(positions.size() * 3);
    for (const QVector3D& position : positions)
        data.insert(data.end(), { position.x(), position.y(), position.z() });

    gl.glGenVertexArrays(1, &m_vao);
    gl.glBindVertexArray(m_vao);
    gl.glGenBuffers(1, &m_vbo);
    gl.glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    gl.glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(data.size() * sizeof(float)),
                    data.data(), GL_STATIC_DRAW);
    gl.glEnableVertexAttribArray(0);
    gl.glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
    gl.glBindVertexArray(0);
    gl.glBindBuffer(GL_ARRAY_BUFFER, 0);

    m_count = static_cast<int>(positions.size());
}

void GpuPoints::destroy(QOpenGLFunctions_3_3_Core& gl)
{
    if (m_vao) { gl.glDeleteVertexArrays(1, &m_vao); m_vao = 0; }
    if (m_vbo) { gl.glDeleteBuffers(1, &m_vbo); m_vbo = 0; }
    m_count = 0;
}

void GpuPoints::drawAll(QOpenGLFunctions_3_3_Core& gl) const
{
    if (!isValid()) return;
    gl.glBindVertexArray(m_vao);
    gl.glDrawArrays(GL_POINTS, 0, m_count);
    gl.glBindVertexArray(0);
}

void GpuPoints::drawOne(QOpenGLFunctions_3_3_Core& gl, int index) const
{
    if (!isValid() || index < 0 || index >= m_count) return;
    gl.glBindVertexArray(m_vao);
    gl.glDrawArrays(GL_POINTS, index, 1);
    gl.glBindVertexArray(0);
}

} // namespace suspkin
