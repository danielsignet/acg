#include <vr/Camera.h>
#include <vr/Geometry.h>
#include <vr/Group.h>
#include <vr/LOD.h>
#include <vr/Light.h>
#include <vr/NodeVisitor.h>
#include <vr/Transform.h>

namespace vr{
// Allows derived objects to release their owned resources safely.
NodeVisitor::~NodeVisitor() = default;

// Visits a snapshot of the children in depth-first order.
void NodeVisitor::traverse(Group& group){
    const auto children = group.getChildren();
    for (const auto& child : children){
        child->accept(*this);
    }
}

// Visits each child of this group in depth-first order.
void NodeVisitor::apply(Group& node){
    traverse(node);
}

// Delegates transforms to the default group traversal.
void NodeVisitor::apply(Transform& node){
    apply(static_cast<Group&>(node));
}

// Visits every detail branch during an unrestricted traversal.
void NodeVisitor::apply(LOD& node){
    apply(static_cast<Group&>(node));
}

// Leaves geometry processing to specialized visitors.
void NodeVisitor::apply(Geometry&){
}

// Leaves light processing to specialized visitors.
void NodeVisitor::apply(Light&){
}

// Leaves camera processing to specialized visitors.
void NodeVisitor::apply(Camera&){
}
} // namespace vr
