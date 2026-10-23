#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <vr/Geometry.h>

namespace vr{
// Releases the OpenGL buffers owned by this mesh.
Geometry::~Geometry(){
    if (m_ebo){
        glDeleteBuffers(1, &m_ebo);
    }
    if (m_vbo){
        glDeleteBuffers(1, &m_vbo);
    }
    if (m_vao){
        glDeleteVertexArrays(1, &m_vao);
    }
}

// Validates and stores local vertex attributes and triangle indices.
void Geometry::setData(std::vector<glm::vec3> vertices, std::vector<unsigned> indices,
                       std::vector<glm::vec3> normals, std::vector<glm::vec2> uv){
    if (vertices.empty() || indices.empty() || indices.size() % 3 ||
        normals.size() != vertices.size()){
        throw std::invalid_argument(
            "Triangle geometry requires positions, normals and triangle indices");
    }
    if (!uv.empty() && uv.size() != vertices.size()){
        throw std::invalid_argument("UV array size");
    }
    for (auto index : indices){
        if (index >= vertices.size()){
            throw std::out_of_range("Vertex index");
        }
    }
    std::vector<Vertex> data;
    data.reserve(vertices.size());
    for (std::size_t i = 0; i < vertices.size(); ++i){
        for (int axis = 0; axis < 3; ++axis){
            if (!std::isfinite(vertices[i][axis]) || !std::isfinite(normals[i][axis])){
                throw std::invalid_argument("Non-finite geometry");
            }
        }
        data.push_back({vertices[i], normals[i], uv.empty() ? glm::vec2(0) : uv[i]});
    }
    m_vertices = std::move(data);
    m_indices = std::move(indices);
    m_dirty = true;
}

// Uploads changed mesh data and draws its indexed triangles.
void Geometry::draw() const{
    if (m_indices.empty()){
        return;
    }
    if (!m_vao){
        glGenVertexArrays(1, &m_vao);
        glGenBuffers(1, &m_vbo);
        glGenBuffers(1, &m_ebo);
    }
    glBindVertexArray(m_vao);
    if (m_dirty){
        glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(m_vertices.size() * sizeof(Vertex)),
                     m_vertices.data(), GL_STATIC_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ebo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                     static_cast<GLsizeiptr>(m_indices.size() * sizeof(unsigned)), m_indices.data(),
                     GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                              reinterpret_cast<void*>(offsetof(Vertex, position)));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                              reinterpret_cast<void*>(offsetof(Vertex, normal)));
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                              reinterpret_cast<void*>(offsetof(Vertex, texCoord)));
        m_dirty = false;
    }
    glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(m_indices.size()), GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);
}
} // namespace vr
