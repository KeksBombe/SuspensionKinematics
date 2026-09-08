#include "render/GpuLines.h"

namespace suspkin {

void GpuLines::upload(QOpenGLFunctions_3_3_Core& gl, const std::vector<QVector3D>& vertices)
{
    destroy(gl);
    if (vertices.empty()) return;

    std::vector<float> data;
    data.reserve(vertices.size() * 3);
    for (const QVector3D& vertex : vertices)
        data.insert(data.end(), { vertex.x(), vertex.y(), vertex.z() });

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

    m_count = static_cast<int>(vertices.size());
}

void GpuLines::destroy(QOpenGLFunctions_3_3_Core& gl)
{
    if (m_vao) { gl.glDeleteVertexArrays(1, &m_vao); m_vao = 0; }
    if (m_vbo) { gl.glDeleteBuffers(1, &m_vbo); m_vbo = 0; }
    m_count = 0;
}

void GpuLines::draw(QOpenGLFunctions_3_3_Core& gl, int first, int count) const
{
    if (!isValid() || count <= 0 || first < 0 || first + count > m_count) return;
    gl.glBindVertexArray(m_vao);
    gl.glDrawArrays(GL_LINES, first, count);
    gl.glBindVertexArray(0);
}

} // namespace suspkin
