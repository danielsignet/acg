#pragma once
#include "NodeVisitor.h"
#include "UpdateCallback.h"
#include <cstddef>
#include <set>

namespace vr{
class UpdateVisitor : public NodeVisitor{
public:
    // Executes each unique node callback once during this update.
    void update(Node& root, const UpdateContext& context);
    bool hasDynamicUpdates() const{ return m_dynamicUpdates; }

    // Executes this group's callback once, then visits its children.
    void apply(Group& node) override;

    // Updates this transform and its descendants once per update.
    void apply(Transform& node) override;

    // Updates this detail node and every branch once per update.
    void apply(LOD& node) override;

    // Executes this mesh's callback once even when the mesh is shared.
    void apply(Geometry& node) override;

    // Executes this light's callback once per update.
    void apply(Light& node) override;

    // Executes this camera's callback once per update.
    void apply(Camera& node) override;

    // Returns the number of callbacks executed during the latest update.
    std::size_t getCallbackCount() const{
        return m_callbackCount;
    }

    std::size_t getSkinnedVertices() const{
        return m_skinnedVertices;
    }

    std::size_t getSkinInfluences() const{
        return m_skinInfluences;
    }

private:
    bool m_dynamicUpdates = false;
    // Records a first visit before processing this node.
    bool enter(Node& node);
    UpdateContext m_context{0, 0};
    std::set<const Node*> m_visited;
    std::size_t m_callbackCount = 0;
    std::size_t m_skinnedVertices = 0, m_skinInfluences = 0;
};
} // namespace vr
