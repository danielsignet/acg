#include <algorithm>
#include <stdexcept>
#include <vr/Group.h>

namespace vr{
// Adds a shared child after rejecting null pointers, duplicate edges and cycles.
void Group::addChild(const std::shared_ptr<Node>& child){
    if (!child){
        throw std::invalid_argument("Null child");
    }
    if (child->contains(this)){
        throw std::invalid_argument("A scene graph cannot contain cycles");
    }
    if (std::find(m_children.begin(), m_children.end(), child) != m_children.end()){
        throw std::invalid_argument("Duplicate child in the same group");
    }
    auto self = shared_from_this();
    m_children.push_back(child);
    child->m_parents.push_back(self);
    dirtyBound();
}

// Encloses every child bound in the group's coordinate system.
BoundingBox Group::computeBound() const{
    BoundingBox bound;
    for (const auto& child : m_children){
        bound.expandBy(child->getBound());
    }
    return bound;
}

// Preserves exempt descendants when an ancestor lies outside a visibility volume.
bool Group::computeCullingExemption() const{
    return !getCullingEnabled() ||
           std::any_of(m_children.begin(), m_children.end(),
                       [](const auto& child){ return child->hasCullingExemption(); });
}

// Reports whether the requested node is part of this subtree.
bool Group::contains(const Node* node) const{
    if (this == node){
        return true;
    }
    return std::any_of(m_children.begin(), m_children.end(),
                       [node](const auto& child){ return child->contains(node); });
}

} // namespace vr
