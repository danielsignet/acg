#include <stdexcept>
#include <vr/FrameProfiler.h>
#include <vr/Geometry.h>
#include <vr/GpuSkinning.h>
#include <vr/SkeletalAnimation.h>

namespace vr{
// Retains the undeformed control mesh and attaches its evaluated palette.
void Geometry::setSkin(std::shared_ptr<SkinningPose> skin){
    updateSkin();
    m_dataRevision = nextRevision();
    m_gpuSkinning.reset();
    m_skin = std::move(skin);
    m_restVertices = m_skin ? std::make_shared<const std::vector<Vertex>>(m_vertices) : nullptr;
    m_skinRevision = m_positionRevision = 0;
    m_gpuData.reset();
    m_skinInfluences = 0;
    if (m_restVertices){
        for (const auto& vertex : *m_restVertices){
            for (float weight : vertex.skin.weights){
                if (weight <= 0){
                    break;
                }
                ++m_skinInfluences;
            }
        }
    }
    updateSkin();
    m_vertexDirty = true;
}

std::shared_ptr<Geometry> Geometry::instantiateSkin(std::shared_ptr<SkinningPose> pose) const{
    auto result = std::make_shared<Geometry>(getName());
    result->m_restVertices = m_restVertices;
    result->m_vertices = getRestVertices();
    result->m_indices = m_indices;
    result->m_skin = std::move(pose);
    result->m_skinInfluences = m_skinInfluences;
    result->updateSkin();
    return result;
}

void Geometry::prepareDeformation(const std::shared_ptr<GpuSkinning>& skinning){
    m_gpuSkinning = skinning && skinning->enqueue(*this) ? skinning : nullptr;
    if (m_skin && m_skin->revision != m_positionRevision){
        m_rayDirty = true;
        dirtyBound();
    }
}

// Exact CPU positions keep existing picking and shadow-camera bounds unchanged.
// Full CPU normals/tangents are evaluated only by the fallback or explicit CPU readers.
void Geometry::ensureSkinPositions() const{
    if (!m_skin || m_positionRevision == m_skin->revision){
        return;
    }
    FrameProfiler::Scope pass(m_gpuSkinning ? m_gpuSkinning->getProfiler() : nullptr,
                              FrameProfiler::Stage::SkinBounds);
    for (std::size_t i = 0; i < m_restVertices->size(); ++i){
        const auto& rest = (*m_restVertices)[i];
        glm::vec3 position(0);
        float total = 0;
        for (unsigned slot = 0; slot < SkinWeights::Count; ++slot){
            const float weight = rest.skin.weights[slot];
            if (weight <= 0){
                break;
            }
            const auto joint = rest.skin.joints[slot];
            if (joint >= m_skin->matrices.size()){
                throw std::out_of_range("Skin position joint");
            }
            position += glm::vec3(m_skin->matrices[joint] * glm::vec4(rest.position, 1)) * weight;
            total += weight;
        }
        m_vertices[i].position = total > 0 ? position / total : rest.position;
    }
    m_positionRevision = m_skin->revision;
    m_rayDirty = true;
    if (m_gpuSkinning){
        m_gpuSkinning->recordCpuPositions(m_vertices.size());
    }
}

// Updates positions and tangent frames once per evaluated skeleton pose.
std::size_t Geometry::updateSkin() const{
    if (!m_skin || m_skinRevision == m_skin->revision){
        return 0;
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
    m_skinRevision = m_positionRevision = m_skin->revision;
    m_vertexDirty = m_rayDirty = true;
    dirtyBound();
    return m_vertices.size();
}
} // namespace vr
