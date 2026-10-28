#include <cmath>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <vr/GLState.h>
#include <vr/Geometry.h>
#include <vr/GpuSkinning.h>
#include <vr/SkeletalAnimation.h>

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
        GLState::deleteVertexArrays(1, &m_vao);
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
    m_dataRevision = nextRevision();
    m_indices = std::move(indices);
    m_dirty = true;
    m_skin.reset();
    m_gpuSkinning.reset();
    m_gpuData.reset();
    m_skinRevision = m_positionRevision = 0;
    m_skinInfluences = 0;
    m_restVertices.reset();
    m_vertexDirty = m_rayDirty = false;
    m_rayNodes.clear();
    m_rayTriangles.clear();
    dirtyBound();
}

// Computes the local bound from the stored vertex positions.
BoundingBox Geometry::computeBound() const{
    ensureSkinPositions();
    BoundingBox bound;
    for (const auto& vertex : m_vertices){
        bound.expandBy(vertex.position);
    }
    return bound;
}

// Binds render attributes independently of CPU skin weights.
void Geometry::bindAttributes(GLuint buffer) const{
    glBindBuffer(GL_ARRAY_BUFFER, buffer);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(GpuVertex),
                          reinterpret_cast<void*>(offsetof(GpuVertex, position)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(GpuVertex),
                          reinterpret_cast<void*>(offsetof(GpuVertex, normal)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(GpuVertex),
                          reinterpret_cast<void*>(offsetof(GpuVertex, texCoord)));
    glEnableVertexAttribArray(7);
    glVertexAttribPointer(7, 4, GL_FLOAT, GL_FALSE, sizeof(GpuVertex),
                          reinterpret_cast<void*>(offsetof(GpuVertex, tangent)));
}

void Geometry::bind() const{
    const bool gpu =
        m_gpuData && m_skin && m_gpuSkinning && m_gpuData->revision == m_skin->revision;
    if (!gpu){
        updateSkin();
    }
    if (!m_vao){
        glGenVertexArrays(1, &m_vao);
        glGenBuffers(1, &m_vbo);
        glGenBuffers(1, &m_ebo);
    }
    GLState::bindVertexArray(m_vao);
    if (gpu){
        const auto buffer = m_gpuData->output.getBuffer();
        if (m_boundVertexBuffer != buffer){
            bindAttributes(buffer);
            m_boundVertexBuffer = buffer;
        }
        if (m_dirty){
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ebo);
            glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                         static_cast<GLsizeiptr>(m_indices.size() * sizeof(unsigned)),
                         m_indices.data(), GL_STATIC_DRAW);
            m_dirty = false;
        }
        return;
    }
    if (m_dirty || m_vertexDirty){
        m_uploadVertices.clear();
        m_uploadVertices.reserve(m_vertices.size());
        for (const auto& vertex : m_vertices){
            m_uploadVertices.push_back(
                {vertex.position, vertex.normal, vertex.texCoord, vertex.tangent});
        }
        if (m_skin){
            m_boundVertexBuffer = m_vertexStream.acquire(
                m_uploadVertices.size() * sizeof(GpuVertex), m_uploadVertices.data());
            bindAttributes(m_boundVertexBuffer);
        } else{
            glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
            glBufferData(GL_ARRAY_BUFFER,
                         static_cast<GLsizeiptr>(m_uploadVertices.size() * sizeof(GpuVertex)),
                         m_uploadVertices.data(), GL_STATIC_DRAW);
            bindAttributes(m_vbo);
            m_boundVertexBuffer = m_vbo;
        }
        if (m_dirty){
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ebo);
            glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                         static_cast<GLsizeiptr>(m_indices.size() * sizeof(unsigned)),
                         m_indices.data(), GL_STATIC_DRAW);
        }
        m_dirty = m_vertexDirty = false;
    }
}

// Uploads changed mesh data and draws its indexed triangles.
void Geometry::draw() const{
    if (m_indices.empty()){
        return;
    }
    bind();
    glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(m_indices.size()), GL_UNSIGNED_INT, nullptr);
    if (m_skin && !m_gpuSkinning){
        m_vertexStream.seal();
    }
    GLState::bindVertexArray(0);
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
    m_instanceStream.acquire(models.size() * sizeof(glm::mat4), models.data());
    for (unsigned column = 0; column < 4; ++column){
        const unsigned attribute = 3 + column;
        glEnableVertexAttribArray(attribute);
        glVertexAttribPointer(attribute, 4, GL_FLOAT, GL_FALSE, sizeof(glm::mat4),
                              reinterpret_cast<void*>(column * sizeof(glm::vec4)));
        glVertexAttribDivisor(attribute, 1);
    }
    if (surface){
        std::vector<SurfaceTransform> bases;
        bases.reserve(models.size());
        for (const auto& model : models){
            bases.push_back(getSurfaceTransform(model));
        }
        m_normalStream.acquire(bases.size() * sizeof(SurfaceTransform), bases.data());
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
    glDrawElementsInstanced(GL_TRIANGLES, static_cast<GLsizei>(m_indices.size()), GL_UNSIGNED_INT,
                            nullptr, static_cast<GLsizei>(models.size()));
    m_instanceStream.seal();
    if (surface){
        m_normalStream.seal();
    }
    if (m_skin && !m_gpuSkinning){
        m_vertexStream.seal();
    }
    GLState::bindVertexArray(0);
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

std::vector<vr::GpuVertex> vr::Geometry::readGpuVertices() const{
    if (!m_gpuData || !m_gpuData->output.getBuffer()){
        throw std::logic_error("GPU deformation has not been prepared");
    }
    std::vector<GpuVertex> vertices(m_vertices.size());
    glBindBuffer(GL_ARRAY_BUFFER, m_gpuData->output.getBuffer());
    glGetBufferSubData(GL_ARRAY_BUFFER, 0,
                       static_cast<GLsizeiptr>(vertices.size() * sizeof(GpuVertex)),
                       vertices.data());
    return vertices;
}
