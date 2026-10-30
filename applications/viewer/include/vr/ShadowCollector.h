#pragma once
#include "CullVisitor.h"
#include <unordered_map>

namespace vr{
class ShadowCollector : public PathVisitor{
public:
    // Retains the constructor argument for callers; depth state is bound by ShadowRenderer.
    explicit ShadowCollector(const std::shared_ptr<Program>& ={}) : PathVisitor(){
        setLodFading(true);
    }

    // Collects all physical casters without applying observer visibility restrictions.
    std::vector<RenderNode> collect(Node& root, const glm::vec3& eye);
    // Retain an unchanged root list without copying every resolved State each frame.
    std::shared_ptr<const std::vector<RenderNode>> collectShared(Node& root, const glm::vec3& eye);
    void setStateCaching(bool enabled){
        PathVisitor::setStateCaching(enabled);
        m_caching = enabled;
        m_branches.clear();
    }
    std::size_t getBranchCacheHits() const{ return m_branchHits; }

    // Traverses a visible group's physical shadow casters.
    void apply(Group& node) override;

    // Accumulates a visible transform before collecting its descendants.
    void apply(Transform& node) override;

    // Selects the same live-camera detail level as the color pass.
    void apply(LOD& node) override;

    // Collects a visible geometry when its resolved state permits shadow casting.
    void apply(Geometry& node) override;

private:
    struct Stamp{
        const State* state = nullptr;
        std::uint64_t stateRevision = 0, matrixRevision = 0, revision = 0;
        bool visible = false, culling = false, dynamic = false, skinned = false, hasLod = false;
        std::string room;
        glm::vec3 lodCenter{0};
        float lodFade = 0;
        std::vector<float> lodRanges;
        std::vector<std::pair<Node*, std::uint64_t>> children;
    };
    struct Branch{
        std::uint64_t revision, stateRevision;
        glm::mat4 model;
        glm::vec3 eye;
        std::string room;
        bool dynamic, culling;
        std::shared_ptr<const std::vector<RenderNode>> items;
    };
    std::uint64_t refresh(Node& node);
    Node* m_root = nullptr;
    std::unordered_map<Node*, Stamp> m_stamps;
    std::unordered_map<Node*, std::vector<Branch>> m_branches;
    bool m_caching = true;
    std::size_t m_branchHits = 0;
    std::shared_ptr<const std::vector<RenderNode>> m_result;
    State mergeState(const State& parent, const State& local) const override{
        return parent.mergedShadow(local);
    }

    // Restores the inherited room label after visiting a visible subtree.
    void within(Node& node, const std::function<void()>& action);

    // Records an instance without OpenGL calls or camera-frustum rejection.
    void geometry(Geometry& node, const glm::mat4& model, const State& state) override;

    std::vector<RenderNode> m_renderList;
    std::string m_roomId;
    bool m_dynamic = false;
};
} // namespace vr
