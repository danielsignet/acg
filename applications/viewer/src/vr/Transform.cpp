#include <vr/Transform.h>

namespace vr{
glm::mat4 Transform::getWorldMatrix(const glm::mat4& parent) const{
    for (const auto& cached : m_worlds){
        if (cached.first == parent){
            return cached.second;
        }
    }
    const auto world = parent * m_matrix;
    // Bound memory for an asset instanced under many independently moving parents.
    if (m_worlds.size() == 8){
        m_worlds.clear();
    }
    m_worlds.emplace_back(parent, world);
    return world;
}

// Transforms the union of child bounds into the parent coordinate system.
BoundingBox Transform::computeBound() const{
    return Group::computeBound().transformed(m_matrix);
}
} // namespace vr
