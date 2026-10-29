#include <vr/Camera.h>
#include <vr/FrameProfiler.h>
#include <vr/Geometry.h>
#include <vr/Group.h>
#include <vr/LOD.h>
#include <vr/Light.h>
#include <vr/ModelInstance.h>
#include <vr/SkeletalAnimation.h>
#include <vr/Transform.h>
#include <vr/UpdateVisitor.h>

namespace vr{
// Executes each unique node callback once during this update.
void UpdateVisitor::update(Node& root, const UpdateContext& context){
    FrameProfiler::Scope update(context.profiler, FrameProfiler::Stage::Update);
    m_visited.clear();
    m_callbackCount = 0;
    m_dynamicUpdates = false;
    m_skinnedVertices = m_skinInfluences = 0;
    m_context = context;
    root.accept(*this);
}

// Records a first visit before processing this node.
bool UpdateVisitor::enter(Node& node){
    if (!node.getUpdateEnabled()) return false;
    if (!m_visited.insert(&node).second){
        return false;
    }
    if (auto callback = node.getUpdateCallback()){
        callback->update(node, m_context);
        m_dynamicUpdates = m_dynamicUpdates || callback->changesRenderedContent();
        ++m_callbackCount;
    }
    return true;
}

// Executes this group's callback once, then visits its children.
void UpdateVisitor::apply(Group& node){
    if (enter(node)){
        if (auto instance = dynamic_cast<ModelInstance*>(&node)){
            if (instance->getVisible()){
                const auto children = instance->getChildren();
                for (const auto& child : children){
                    if (child != instance->getInactiveVariant()){
                        child->accept(*this);
                    }
                }
            }
        } else{
            traverse(node);
        }
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
        node.prepareDeformation(m_context.gpuSkinning);
        if (!node.hasGpuSkinning() && node.getSkin() &&
            node.getAppliedSkinRevision() != node.getSkin()->revision){
            FrameProfiler::Scope skinning(m_context.profiler, FrameProfiler::Stage::Skinning);
            m_skinnedVertices += node.updateSkin();
            m_skinInfluences += node.getSkinInfluenceCount();
        }
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
