#include <vr/Node.h>
#include <vr/Transform.h>

namespace vr{
const BoundingBox& Node::getBound() const{
    if (m_boundDirty){
        m_bound = computeBound();
        m_boundDirty = false;
        ++m_boundBuildCount;
    }
    return m_bound;
}

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

BoundingBox Transform::computeBound() const{
    return Group::computeBound().transformed(m_matrix);
}
} // namespace vr
