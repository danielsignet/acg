#include <vr/Geometry.h>
#include <vr/LOD.h>
#include <vr/ShadowCollector.h>
#include <vr/Transform.h>

namespace vr{
// Collects all physical casters without applying observer visibility restrictions.
std::vector<RenderNode> ShadowCollector::collect(Node& root, const glm::vec3& eye){
    m_renderList.clear();
    m_roomId.clear();
    m_dynamic = false;
    setEye(eye);
    root.accept(*this);
    return std::move(m_renderList);
}

// Traverses a visible group's physical shadow casters.
void ShadowCollector::apply(Group& node){
    within(node, [&]{ PathVisitor::apply(node); });
}

void ShadowCollector::apply(Transform& node){
    within(node, [&]{ PathVisitor::apply(node); });
}

void ShadowCollector::apply(LOD& node){
    within(node, [&]{ PathVisitor::apply(node); });
}

void ShadowCollector::apply(Geometry& node){
    within(node, [&]{ PathVisitor::apply(node); });
}

// Restores the inherited room label after visiting a visible subtree.
void ShadowCollector::within(Node& node, const std::function<void()>& action){
    if (!node.getVisible()){
        return;
    }
    const auto previous = m_roomId;
    const auto previousDynamic = m_dynamic;
    m_dynamic = m_dynamic || bool(node.getUpdateCallback());
    if (!node.getRoomId().empty()){
        m_roomId = node.getRoomId();
    }
    try{
        action();
    } catch (...){
        m_roomId = previous;
        m_dynamic = previousDynamic;
        throw;
    }
    m_roomId = previous;
    m_dynamic = previousDynamic;
}

// Records an instance without OpenGL calls or camera-frustum rejection.
void ShadowCollector::geometry(Geometry& node, const glm::mat4& model, const State& state){
    if (state.getCastShadows()){
        m_renderList.push_back({&node, model, state, m_roomId});
        m_renderList.back().cullingEnabled = m_cullingEnabled;
        m_renderList.back().dynamic = m_dynamic || bool(node.getSkin());
    }
}
} // namespace vr
