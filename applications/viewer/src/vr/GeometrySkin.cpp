#include <stdexcept>
#include <vr/Geometry.h>
#include <vr/SkeletalAnimation.h>

namespace vr{
// Retains the undeformed control mesh and attaches its evaluated palette.
void Geometry::setSkin(std::shared_ptr<SkinningPose> skin){
    m_skin = std::move(skin);
    m_restVertices = m_skin ? std::make_shared<const std::vector<Vertex>>(m_vertices) : nullptr;
    m_skinRevision = 0;
    updateSkin();
}

std::shared_ptr<Geometry> Geometry::instantiateSkin(std::shared_ptr<SkinningPose> pose) const{
    auto result = std::make_shared<Geometry>(getName());
    result->m_restVertices = m_restVertices;
    result->m_vertices = getRestVertices();
    result->m_indices = m_indices;
    result->m_skin = std::move(pose);
    result->updateSkin();
    return result;
}

// Updates positions and tangent frames once per evaluated skeleton pose.
void Geometry::updateSkin(){
    if (!m_skin || m_skinRevision == m_skin->revision){
        return;
    }
    for (std::size_t i = 0; i < m_restVertices->size(); ++i){
        const auto& rest = (*m_restVertices)[i];
        auto& vertex = m_vertices[i];
        glm::vec3 position(0), normal(0), tangent(0);
        float total = 0;
        for (unsigned slot = 0; slot < SkinWeights::Count; ++slot){
            const float weight = rest.skin.weights[slot];
            if (weight <= 0){
                break;
            }
            const auto joint = rest.skin.joints[slot];
            if (joint >= m_skin->matrices.size()){
                throw std::out_of_range("Skin joint");
            }
            position += glm::vec3(m_skin->matrices[joint] * glm::vec4(rest.position, 1)) * weight;
            normal += m_skin->normals[joint] * rest.normal * weight;
            tangent += glm::mat3(m_skin->matrices[joint]) * glm::vec3(rest.tangent) * weight;
            total += weight;
        }
        if (total <= 0){
            continue;
        }
        vertex.position = position / total;
        vertex.normal = glm::length(normal) > 1e-8f ? glm::normalize(normal) : rest.normal;
        tangent -= vertex.normal * glm::dot(vertex.normal, tangent);
        vertex.tangent = glm::vec4(
            glm::length(tangent) > 1e-8f ? glm::normalize(tangent) : glm::vec3(0), rest.tangent.w);
    }
    m_skinRevision = m_skin->revision;
    m_vertexDirty = m_rayDirty = true;
    dirtyBound();
}
} // namespace vr
