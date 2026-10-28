#include <vr/Camera.h>
#include <vr/Geometry.h>
#include <vr/Group.h>
#include <vr/LOD.h>
#include <vr/Light.h>
#include <vr/Transform.h>
#include <vr/UpdateVisitor.h>

namespace vr{
// Executes each unique node callback once during this update.
void UpdateVisitor::update(Node& root, const UpdateContext& context){
    m_visited.clear();
    m_callbackCount = 0;
    m_context = context;
    root.accept(*this);
}

// Records a first visit before processing this node.
bool UpdateVisitor::enter(Node& node){
    if (!m_visited.insert(&node).second){
        return false;
    }
    if (auto callback = node.getUpdateCallback()){
        callback->update(node, m_context);
        ++m_callbackCount;
    }
    return true;
}

// Executes this group's callback once, then visits its children.
void UpdateVisitor::apply(Group& node){
    if (enter(node)){
        traverse(node);
    }
}

// Updates this transform and its descendants once per update.
void UpdateVisitor::apply(Transform& node){
    apply(static_cast<Group&>(node));
}

// Updates this detail node and every branch once per update.
void UpdateVisitor::apply(LOD& node){
    apply(static_cast<Group&>(node));
}

// Executes this mesh's callback once even when the mesh is shared.
void UpdateVisitor::apply(Geometry& node){
    if (enter(node)){
        node.updateSkin();
    }
}

// Executes this light's callback once per update.
void UpdateVisitor::apply(Light& node){
    enter(node);
}

// Executes this camera's callback once per update.
void UpdateVisitor::apply(Camera& node){
    enter(node);
}
} // namespace vr
