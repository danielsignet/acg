#include <vr/CullVisitor.h>
#include <vr/Geometry.h>
#include <vr/LOD.h>
#include <vr/Transform.h>

namespace vr{
void CullVisitor::collect(Node& root, const glm::mat4& vp, const glm::vec3& eye, bool enabled){
    m_frustum.reset();
    if (enabled){
        m_frustum.emplace(vp);
    }
    m_eye = eye;
    m_enabled = enabled;
    m_insideDepth = 0;
    m_room.clear();
    m_renderList.clear();
    m_stats ={};
    root.accept(*this);
}

void CullVisitor::within(Node& node, const std::function<void()>& action){
    if (!node.getVisible()){
        return;
    }
    bool inside = false;
    if (m_enabled && !m_insideDepth){
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
    const auto previousRoom = m_room;
    if (!node.getRoomId().empty()){
        m_room = node.getRoomId();
    }
    if (inside){
        ++m_insideDepth;
    }
    try{
        action();
    } catch (...){
        if (inside){
            --m_insideDepth;
        }
        m_room = previousRoom;
        throw;
    }
    if (inside){
        --m_insideDepth;
    }
    m_room = previousRoom;
}

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

void CullVisitor::geometry(Geometry& node, const glm::mat4& model, const State& state){
    m_renderList.push_back({&node, model, state, m_room});
}

void CullVisitor::lod(LOD& node, std::size_t level){
    m_stats.lodLevels[node.getName()] = level;
}
} // namespace vr
