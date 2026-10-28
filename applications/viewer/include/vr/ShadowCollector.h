#pragma once
#include "CullVisitor.h"

namespace vr{
class ShadowCollector : public PathVisitor{
public:
    // Retains the constructor argument for callers; depth state is bound by ShadowRenderer.
    explicit ShadowCollector(const std::shared_ptr<Program>& ={}) : PathVisitor(){
        setLodFading(true);
    }

    // Collects all physical casters without applying observer visibility restrictions.
    std::vector<RenderNode> collect(Node& root, const glm::vec3& eye);

    // Traverses a visible group's physical shadow casters.
    void apply(Group& node) override;

    // Accumulates a visible transform before collecting its descendants.
    void apply(Transform& node) override;

    // Selects the same live-camera detail level as the color pass.
    void apply(LOD& node) override;

    // Collects a visible geometry when its resolved state permits shadow casting.
    void apply(Geometry& node) override;

private:
    State mergeState(const State& parent, const State& local) const override{
        return parent.mergedShadow(local);
    }

    // Restores the inherited room label after visiting a visible subtree.
    void within(Node& node, const std::function<void()>& action);

    // Records an instance without OpenGL calls or camera-frustum rejection.
    void geometry(Geometry& node, const glm::mat4& model, const State& state) override;

    std::vector<RenderNode> m_renderList;
    std::string m_roomId;
};
} // namespace vr
