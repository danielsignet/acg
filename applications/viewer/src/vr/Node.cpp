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

// Invalidates the cached bound and every ancestor reached through DAG parents.
void Node::dirtyBound(){
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
