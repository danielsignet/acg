#pragma once
#include "Node.h"
#include "NodeVisitor.h"
#include <optional>

namespace vr{
class Group : public Node{
public:
    using Node::Node;

    // Dispatches this concrete node type to the visitor.
    void accept(NodeVisitor& visitor) override{
        visitor.apply(*this);
    }

    // Adds a shared child after rejecting null pointers, duplicate edges and cycles.
    void addChild(const std::shared_ptr<Node>& child);

    // Reports whether the requested node is part of this subtree.
    bool contains(const Node* node) const override;
    bool containsLights() const override;
    void dirtyLightPresence() override;

    // Encloses every child bound in the group's coordinate system.
    BoundingBox computeBound() const override;

    // Preserves exempt descendants when an ancestor lies outside a visibility volume.
    bool computeCullingExemption() const override;

    // Returns the ordered collection of shared child nodes.
    const std::vector<std::shared_ptr<Node>>& getChildren() const{
        return m_children;
    }

private:
    std::vector<std::shared_ptr<Node>> m_children;
    mutable std::optional<bool> m_containsLights;
};
} // namespace vr
