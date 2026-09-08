#include "render/GpuMesh.h"

#include <vector>

namespace suspkin {

void GpuMesh::upload(QOpenGLFunctions_3_3_Core& gl, const TriMesh& mesh, const EdgeSet& edges)
{
    destroy(gl);
    if (mesh.isEmpty()) return;

    // --- Solid stream ------------------------------------------------------
    // Non-indexed: each triangle contributes three vertices with their own
    // normal. That is what lets one buffer serve both shading models -- flat
    // facets for an STL, and the exact analytic normals of a tessellated B-Rep,
    // which no amount of averaging welded vertices could reconstruct.
    const std::size_t triCount = mesh.triangleCount();
    const bool smooth = mesh.hasCornerNormals();
    std::vector<float> solidVerts;
    solidVerts.reserve(triCount * 3 * 6);
    for (std::size_t t = 0; t < triCount; ++t) {
        for (int k = 0; k < 3; ++k) {
            const QVector3D& p = mesh.positions[mesh.indices[3 * t + k]];
            const QVector3D& n = smooth ? mesh.cornerNormals[3 * t + k] : mesh.faceNormals[t];
            solidVerts.insert(solidVerts.end(),
                              { p.x(), p.y(), p.z(), n.x(), n.y(), n.z() });
        }
    }
    m_solidVertexCount = static_cast<GLsizei>(triCount * 3);

    constexpr GLsizei kSolidStride = 6 * sizeof(float);
    gl.glGenVertexArrays(1, &m_solidVao);
    gl.glBindVertexArray(m_solidVao);
    gl.glGenBuffers(1, &m_solidVbo);
    gl.glBindBuffer(GL_ARRAY_BUFFER, m_solidVbo);
    gl.glBufferData(GL_ARRAY_BUFFER,
                    static_cast<GLsizeiptr>(solidVerts.size() * sizeof(float)),
                    solidVerts.data(), GL_STATIC_DRAW);
    gl.glEnableVertexAttribArray(0);
    gl.glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, kSolidStride, nullptr);
    gl.glEnableVertexAttribArray(1);
    gl.glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, kSolidStride,
                             reinterpret_cast<void*>(3 * sizeof(float)));
    gl.glBindVertexArray(0);

    // --- Line stream -------------------------------------------------------
    std::vector<float> linePositions;
    linePositions.reserve(mesh.positions.size() * 3);
    for (const QVector3D& p : mesh.positions)
        linePositions.insert(linePositions.end(), { p.x(), p.y(), p.z() });

    gl.glGenBuffers(1, &m_lineVbo);
    gl.glBindBuffer(GL_ARRAY_BUFFER, m_lineVbo);
    gl.glBufferData(GL_ARRAY_BUFFER,
                    static_cast<GLsizeiptr>(linePositions.size() * sizeof(float)),
                    linePositions.data(), GL_STATIC_DRAW);

    m_lineIndexCount = static_cast<GLsizei>(edges.all.size());
    if (m_lineIndexCount > 0) {
        gl.glGenVertexArrays(1, &m_lineVao);
        gl.glBindVertexArray(m_lineVao);
        gl.glBindBuffer(GL_ARRAY_BUFFER, m_lineVbo);
        gl.glEnableVertexAttribArray(0);
        gl.glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
        gl.glGenBuffers(1, &m_lineEbo);
        gl.glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_lineEbo);
        gl.glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                        static_cast<GLsizeiptr>(edges.all.size() * sizeof(std::uint32_t)),
                        edges.all.data(), GL_STATIC_DRAW);
        gl.glBindVertexArray(0);
    }

    gl.glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void GpuMesh::destroy(QOpenGLFunctions_3_3_Core& gl)
{
    const auto delVao = [&gl](GLuint& v) { if (v) { gl.glDeleteVertexArrays(1, &v); v = 0; } };
    const auto delBuf = [&gl](GLuint& b) { if (b) { gl.glDeleteBuffers(1, &b); b = 0; } };

    delVao(m_solidVao);
    delVao(m_lineVao);
    delBuf(m_solidVbo);
    delBuf(m_lineVbo);
    delBuf(m_lineEbo);

    m_solidVertexCount = 0;
    m_lineIndexCount = 0;
}

void GpuMesh::drawSolid(QOpenGLFunctions_3_3_Core& gl) const
{
    if (!m_solidVao || m_solidVertexCount == 0) return;
    gl.glBindVertexArray(m_solidVao);
    gl.glDrawArrays(GL_TRIANGLES, 0, m_solidVertexCount);
    gl.glBindVertexArray(0);
}

void GpuMesh::drawLines(QOpenGLFunctions_3_3_Core& gl) const
{
    if (!m_lineVao || m_lineIndexCount == 0) return;
    gl.glBindVertexArray(m_lineVao);
    gl.glDrawElements(GL_LINES, m_lineIndexCount, GL_UNSIGNED_INT, nullptr);
    gl.glBindVertexArray(0);
}

} // namespace suspkin
