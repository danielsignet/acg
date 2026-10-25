#pragma once
#include "Frustum.h"
#include "PathVisitor.h"
#include "PortalVisibility.h"
#include <map>
#include <optional>

namespace vr{
struct RenderNode{
    Geometry* geometry;
    glm::mat4 model;
    State state;
    std::string room;
};

struct CullStats{
    unsigned boundsTested = 0;
    unsigned subtreesRejected = 0;
    unsigned portalRejected = 0;
    std::map<std::string, std::size_t> lodLevels;
    std::vector<std::string> rejectedNames;
};

// Collects visible instances without issuing any OpenGL calls.
class CullVisitor : public PathVisitor{
public:
    // Initializes collection with the inherited rendering defaults.
    explicit CullVisitor(State initial ={}) : PathVisitor(std::move(initial)){
    }

    // Rebuilds the render list using optional frustum and portal visibility.
    void collect(Node& root, const glm::mat4& viewProjection, const glm::vec3& lodEye,
                 bool enabled = true, const PortalResult* portals = nullptr);

    // Rejects the whole group when its bound is outside the active visibility volumes.
    void apply(Group& node) override;

    // Tests the parent-space bound before accumulating the local transformation.
    void apply(Transform& node) override;

    // Tests the bound enclosing all detail levels before selecting the live-camera level.
    void apply(LOD& node) override;

    // Adds a geometry only when its inherited visibility and bounds allow rendering.
    void apply(Geometry& node) override;

    // Returns geometry instances with accumulated transforms and rendering states.
    const std::vector<RenderNode>& getRenderList() const{
        return m_renderList;
    }

    // Returns bound tests and subtree rejections from the latest collection.
    const CullStats& getStats() const{
        return m_stats;
    }

private:
    // Restores inherited room and fully-inside state after traversing this node.
    void within(Node& node, const std::function<void()>& action);

    // Records a visible geometry instance without issuing OpenGL calls.
    void geometry(Geometry& node, const glm::mat4& model, const State& state) override;

    // Records the selected detail level for the traversal path.
    void lod(LOD& node, std::size_t level) override;
    std::optional<Frustum> m_frustum;
    const PortalResult* m_portals = nullptr;
    unsigned m_fullyInsideDepth = 0;
    std::string m_roomId;
    CullStats m_stats;
    std::vector<RenderNode> m_renderList;
};
} // namespace vr
