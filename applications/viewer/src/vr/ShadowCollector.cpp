#include <vr/Geometry.h>
#include <algorithm>
#include <vr/Group.h>
#include <vr/FrameWork.h>
#include <vr/LOD.h>
#include <vr/ShadowCollector.h>
#include <vr/Transform.h>

namespace vr{
// Collects all physical casters without applying observer visibility restrictions.
std::vector<RenderNode> ShadowCollector::collect(Node& root, const glm::vec3& eye){
    return *collectShared(root,eye);
}

std::shared_ptr<const std::vector<RenderNode>> ShadowCollector::collectShared(Node& root, const glm::vec3& eye){
    if (m_root != &root){ m_root=&root; m_stamps.clear(); m_branches.clear(); }
    m_branchHits=0;
    m_result.reset();
    if (m_caching && frameWorkOptimized()) refresh(root);
    m_renderList.clear();
    m_roomId.clear();
    m_dynamic = false;
    setEye(eye);
    root.accept(*this);
    if(!m_result) m_result=std::make_shared<const std::vector<RenderNode>>(std::move(m_renderList));
    return m_result;
}

// Check cheap local revisions once per graph node. Resolved depth state and
// transforms are reused independently for every DAG placement of an unchanged branch.
std::uint64_t ShadowCollector::refresh(Node& node){
    auto& old=m_stamps[&node];
    const auto* state=node.getState().get();
    const auto* transform=dynamic_cast<Transform*>(&node);
    const auto* geometry=dynamic_cast<Geometry*>(&node);
    const auto* lod=dynamic_cast<LOD*>(&node);
    const auto stateRevision=state?state->getShadowRevision():0;
    const auto matrixRevision=transform?transform->getMatrixRevision():0;
    const bool dynamic=bool(node.getUpdateCallback()), skinned=geometry && bool(geometry->getSkin());
    bool changed=!old.revision || old.state!=state || old.stateRevision!=stateRevision ||
        old.matrixRevision!=matrixRevision || old.visible!=node.getVisible() ||
        old.culling!=node.getCullingEnabled() || old.dynamic!=dynamic || old.skinned!=skinned ||
        old.room!=node.getRoomId();
    bool hasLod=lod!=nullptr;
    if (lod) changed=changed || old.lodCenter!=lod->getCenter() ||
        old.lodFade!=lod->getFadeWidth() || old.lodRanges!=lod->getRanges();
    if (auto* group=dynamic_cast<Group*>(&node)){
        const auto& children=group->getChildren();
        if (old.children.size()!=children.size()){old.children.resize(children.size());changed=true;}
        for (std::size_t i=0;i<children.size();++i){
            const auto revision=refresh(*children[i]);
            const auto entry=std::make_pair(children[i].get(),revision);
            changed=changed || old.children[i]!=entry;
            old.children[i]=entry;
            hasLod=hasLod || m_stamps.at(children[i].get()).hasLod;
        }
    }
    if (changed){
        old.state=state;old.stateRevision=stateRevision;old.matrixRevision=matrixRevision;
        old.visible=node.getVisible();old.culling=node.getCullingEnabled();old.dynamic=dynamic;
        old.skinned=skinned;old.room=node.getRoomId();old.hasLod=hasLod;
        if(lod){old.lodCenter=lod->getCenter();old.lodFade=lod->getFadeWidth();old.lodRanges=lod->getRanges();}
        old.revision=nextRevision();
    }
    return old.revision;
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
    const bool cache=m_caching && frameWorkOptimized() && dynamic_cast<Group*>(&node);
    const auto stamp=cache?m_stamps.at(&node).revision:0;
    const auto eye=cache && m_stamps.at(&node).hasLod?m_eye:glm::vec3(0);
    if(cache){
        for(const auto& branch:m_branches[&node])
            if(branch.revision==stamp && branch.stateRevision==m_states.back().getRevision() &&
               branch.model==m_models.back() && branch.eye==eye && branch.room==m_roomId &&
               branch.dynamic==m_dynamic && branch.culling==m_cullingEnabled){
                if(&node==m_root) m_result=branch.items;
                else m_renderList.insert(m_renderList.end(),branch.items->begin(),branch.items->end());
                ++m_branchHits;
                return;
            }
    }
    const auto first=m_renderList.size();
    const auto parentModel=m_models.back();
    const auto parentState=m_states.back().getRevision();
    const auto parentCulling=m_cullingEnabled;
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
    if(cache){
        auto& branches=m_branches[&node];
        if(branches.size()>=8) branches.clear();
        branches.erase(std::remove_if(branches.begin(),branches.end(),[&](const Branch& b){
            return b.model==parentModel && b.room==previous;
        }),branches.end());
        branches.push_back({stamp,parentState,parentModel,eye,previous,previousDynamic,parentCulling,
            std::make_shared<const std::vector<RenderNode>>(m_renderList.begin()+first,m_renderList.end())});
        if(&node==m_root) m_result=branches.back().items;
    }
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
