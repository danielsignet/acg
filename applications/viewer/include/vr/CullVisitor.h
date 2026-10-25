#pragma once
#include "BoundingBox.h"
#include "PathVisitor.h"
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
    std::map<std::string, std::size_t> lodLevels;
    std::vector<std::string> rejectedNames;
};

// Collects visible instances without issuing any OpenGL calls.
class CullVisitor : public PathVisitor{
public:
    explicit CullVisitor(State initial = {}) : PathVisitor(std::move(initial)){
    }

    void collect(Node& root, const glm::mat4& viewProjection, const glm::vec3& lodEye,
                 bool enabled = true);
    void apply(Group& node) override;
    void apply(Transform& node) override;
    void apply(LOD& node) override;
    void apply(Geometry& node) override;

    const std::vector<RenderNode>& getRenderList() const{
        return m_renderList;
    }

    const CullStats& getStats() const{
        return m_stats;
    }

private:
    void within(Node& node, const std::function<void()>& action);
    void geometry(Geometry& node, const glm::mat4& model, const State& state) override;
    void lod(LOD& node, std::size_t level) override;
    std::optional<Frustum> m_frustum;
    bool m_enabled = true;
    unsigned m_insideDepth = 0;
    std::string m_room;
    CullStats m_stats;
    std::vector<RenderNode> m_renderList;
};
} // namespace vr
