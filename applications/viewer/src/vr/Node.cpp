#include <vr/Node.h>

namespace vr{
// Bounds are expressed in the parent's coordinates, including a local transform.
const BoundingBox& Node::getBound() const{
    if (m_boundDirty){
        m_bound = computeBound();
        m_cullingExemption = computeCullingExemption();
        m_boundDirty = false;
        ++m_boundBuildCount;
    }
    return m_bound;
}

BoundingBox Node::getWorldBound(const glm::mat4& parent) const{
    const auto& local = getBound();
    for (const auto& entry : m_worldBounds){
        if (entry.revision == m_boundBuildCount && entry.parent == parent){
            return entry.bound;
        }
    }
    const auto world = local.transformed(parent);
    if (m_worldBounds.size() == 8){
        m_worldBounds.clear();
    }
    m_worldBounds.push_back({parent, world, m_boundBuildCount});
    ++m_worldBoundBuildCount;
    return world;
}

// Invalidates the cached bound and every ancestor reached through DAG parents.
void Node::dirtyBound() const{
    if (m_boundDirty){
        return;
    }
    m_boundDirty = true;
    for (const auto& parent : m_parents){
        if (auto node = parent.lock()){
            node->dirtyBound();
        }
    }
}

} // namespace vr
