#include <cmath>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <vr/Geometry.h>

namespace vr{
// Releases the OpenGL buffers owned by this mesh.
Geometry::~Geometry(){
    if (m_instanceNormals){
        glDeleteBuffers(1, &m_instanceNormals);
    }
    if (m_instances){
        glDeleteBuffers(1, &m_instances);
    }
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
                       std::vector<glm::vec3> normals, std::vector<glm::vec2> uv,
                       std::vector<glm::vec4> tangents, std::vector<SkinWeights> skin){
    if (vertices.empty() || indices.empty() || indices.size() % 3 ||
        normals.size() != vertices.size()){
        throw std::invalid_argument(
            "Triangle geometry requires positions, normals and triangle indices");
    }
    if (!uv.empty() && uv.size() != vertices.size()){
        throw std::invalid_argument("UV array size");
    }
    if (!tangents.empty() && tangents.size() != vertices.size()){
        throw std::invalid_argument("Tangent array size");
    }
    if (!skin.empty() && skin.size() != vertices.size()){
        throw std::invalid_argument("Skin array size");
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
        const auto coordinates = uv.empty() ? glm::vec2(0) : uv[i];
        const auto tangent = tangents.empty() ? glm::vec4(0) : tangents[i];
        for (int axis = 0; axis < 4; ++axis){
            if (!std::isfinite(tangent[axis]) || (axis < 2 && !std::isfinite(coordinates[axis]))){
                throw std::invalid_argument("Non-finite texture basis");
            }
        }
        data.push_back({vertices[i], normals[i], coordinates, tangent,
                        skin.empty() ? SkinWeights{} : skin[i]});
    }
    if (tangents.empty() && !uv.empty()){
        buildTangents(data, indices);
    }
    m_vertices = std::move(data);
    m_indices = std::move(indices);
    m_dirty = true;
    m_skin.reset();
    m_skinInfluences = 0;
    m_restVertices.reset();
    m_vertexDirty = m_rayDirty = false;
    m_rayNodes.clear();
    m_rayTriangles.clear();
    dirtyBound();
}

// Computes the local bound from the stored vertex positions.
BoundingBox Geometry::computeBound() const{
    BoundingBox bound;
    for (const auto& vertex : m_vertices){
        bound.expandBy(vertex.position);
    }
    return bound;
}

// Binds the VAO and uploads changed vertices and indices for either draw path.
void Geometry::bind() const{
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
        glEnableVertexAttribArray(7);
        glVertexAttribPointer(7, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                              reinterpret_cast<void*>(offsetof(Vertex, tangent)));
        m_dirty = false;
        m_vertexDirty = false;
    }
    if (m_vertexDirty){
        glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(m_vertices.size() * sizeof(Vertex)),
                     m_vertices.data(), GL_STREAM_DRAW);
        m_vertexDirty = false;
    }
}

// Uploads changed mesh data and draws its indexed triangles.
void Geometry::draw() const{
    if (m_indices.empty()){
        return;
    }
    bind();
    glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(m_indices.size()), GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);
}

// Streams model matrices and draws this mesh once for all supplied instances.
void Geometry::drawInstanced(const std::vector<glm::mat4>& models, bool surface) const{
    if (m_indices.empty() || models.empty()){
        return;
    }
    if (models.size() > static_cast<std::size_t>(std::numeric_limits<GLsizei>::max())){
        throw std::length_error("Too many geometry instances");
    }
    bind();
    if (!m_instances){
        glGenBuffers(1, &m_instances);
        glBindBuffer(GL_ARRAY_BUFFER, m_instances);
        for (unsigned column = 0; column < 4; ++column){
            const unsigned attribute = 3 + column;
            glEnableVertexAttribArray(attribute);
            glVertexAttribPointer(attribute, 4, GL_FLOAT, GL_FALSE, sizeof(glm::mat4),
                                  reinterpret_cast<void*>(column * sizeof(glm::vec4)));
            glVertexAttribDivisor(attribute, 1);
        }
    }
    glBindBuffer(GL_ARRAY_BUFFER, m_instances);
    const auto bytes = static_cast<GLsizeiptr>(models.size() * sizeof(glm::mat4));
    glBufferData(GL_ARRAY_BUFFER, bytes, models.data(), GL_STREAM_DRAW);
    if (surface){
        if (!m_instanceNormals){
            glGenBuffers(1, &m_instanceNormals);
            glBindBuffer(GL_ARRAY_BUFFER, m_instanceNormals);
            for (unsigned column = 0; column < 3; ++column){
                const unsigned attribute = 8 + column;
                glEnableVertexAttribArray(attribute);
                glVertexAttribPointer(attribute, 3, GL_FLOAT, GL_FALSE, sizeof(SurfaceTransform),
                                      reinterpret_cast<void*>(offsetof(SurfaceTransform, normal) +
                                                              column * sizeof(glm::vec3)));
                glVertexAttribDivisor(attribute, 1);
            }
            glEnableVertexAttribArray(11);
            glVertexAttribPointer(11, 1, GL_FLOAT, GL_FALSE, sizeof(SurfaceTransform),
                                  reinterpret_cast<void*>(offsetof(SurfaceTransform, tangentSign)));
            glVertexAttribDivisor(11, 1);
        }
        std::vector<SurfaceTransform> bases;
        bases.reserve(models.size());
        for (const auto& model : models){
            bases.push_back(getSurfaceTransform(model));
        }
        glBindBuffer(GL_ARRAY_BUFFER, m_instanceNormals);
        glBufferData(GL_ARRAY_BUFFER,
                     static_cast<GLsizeiptr>(bases.size() * sizeof(SurfaceTransform)), bases.data(),
                     GL_STREAM_DRAW);
    }
    glDrawElementsInstanced(GL_TRIANGLES, static_cast<GLsizei>(m_indices.size()), GL_UNSIGNED_INT,
                            nullptr, static_cast<GLsizei>(models.size()));
    glBindVertexArray(0);
}

Geometry::SurfaceTransform Geometry::getSurfaceTransform(const glm::mat4& model) const{
    for (const auto& cached : m_surfaceTransforms){
        if (cached.first == model){
            return cached.second;
        }
    }
    const glm::mat3 linear(model);
    const float orientation = glm::determinant(
        glm::mat3(glm::normalize(linear[0]), glm::normalize(linear[1]), glm::normalize(linear[2])));
    if (!std::isfinite(orientation) || std::abs(orientation) < 1e-8f){
        throw std::runtime_error("Singular model transform");
    }
    const SurfaceTransform basis{glm::transpose(glm::inverse(linear)),
                                 orientation < 0 ? -1.0f : 1.0f};
    if (m_surfaceTransforms.size() == 8){
        m_surfaceTransforms.clear();
    }
    m_surfaceTransforms.emplace_back(model, basis);
    return basis;
}
} // namespace vr
