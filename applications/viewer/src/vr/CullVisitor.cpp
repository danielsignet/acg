#include <algorithm>
#include <vr/CullVisitor.h>
#include <vr/Geometry.h>
#include <vr/LOD.h>
#include <vr/Transform.h>

namespace vr{
// Rebuilds the render list using optional frustum and portal visibility.
void CullVisitor::collect(Node& root, const glm::mat4& vp, const glm::vec3& eye, bool enabled,
                          const PortalResult* portals){
    m_portals = portals;
    m_frustum.reset();
    if (enabled){
        m_frustum.emplace(vp);
    }
    m_eye = eye;
    m_fullyInsideDepth = 0;
    m_roomId.clear();
    m_renderList.clear();
    m_stats ={};
    root.accept(*this);
}

// Restores inherited room and fully-inside state after traversing this node.
void CullVisitor::within(Node& node, const std::function<void()>& action){
    ++m_stats.visitedNodes;
    if (!node.getVisible()){
        return;
    }
    bool inside = false;
    if (m_frustum && !m_fullyInsideDepth){
        const auto& bound = node.getBound();
        if (bound.valid()){
            ++m_stats.boundsTested;
            const auto result = m_frustum->classify(bound.transformed(m_models.back()));
            if (result == Visibility::Outside){
                ++m_stats.subtreesRejected;
                m_stats.rejectedNames.push_back(node.getName());
                return;
            }
            inside = result == Visibility::Inside;
        }
    }
    const auto& room = node.getRoomId().empty() ? m_roomId : node.getRoomId();
    if (m_portals && m_portals->active && !room.empty()){
        const auto found = m_portals->volumes.find(room);
        const auto world = node.getBound().transformed(m_models.back());
        if (found == m_portals->volumes.end() ||
            !std::any_of(found->second.begin(), found->second.end(),
                         [&](const auto& volume){ return volume.intersects(world); })){
            ++m_stats.portalRejected;
            m_stats.rejectedNames.push_back(node.getName());
            return;
        }
    }
    const auto previousRoom = m_roomId;
    if (!node.getRoomId().empty()){
        m_roomId = node.getRoomId();
    }
    if (inside){
        ++m_fullyInsideDepth;
    }
    try{
        action();
    } catch (...){
        if (inside){
            --m_fullyInsideDepth;
        }
        m_roomId = previousRoom;
        throw;
    }
    if (inside){
        --m_fullyInsideDepth;
    }
    m_roomId = previousRoom;
}

// Rejects the whole group when its bound is outside the active visibility volumes.
void CullVisitor::apply(Group& node){
    within(node, [&]{ PathVisitor::apply(node); });
}

void CullVisitor::apply(Transform& node){
    within(node, [&]{ PathVisitor::apply(node); });
}

void CullVisitor::apply(LOD& node){
    within(node, [&]{ PathVisitor::apply(node); });
}

void CullVisitor::apply(Geometry& node){
    within(node, [&]{ PathVisitor::apply(node); });
}

// Records a visible geometry instance without issuing OpenGL calls.
void CullVisitor::geometry(Geometry& node, const glm::mat4& model, const State& state){
    m_renderList.push_back({&node, model, state, m_roomId});
}

// Records the selected detail level for the traversal path.
void CullVisitor::lod(LOD& node, std::size_t level){
    m_stats.lodLevels[node.getName()] = level;
}
} // namespace vr
