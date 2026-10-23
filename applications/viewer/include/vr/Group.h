#pragma once
#include "Node.h"
#include "NodeVisitor.h"

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

    // Returns the ordered collection of shared child nodes.
    const std::vector<std::shared_ptr<Node>>& getChildren() const{
        return m_children;
    }

private:
    std::vector<std::shared_ptr<Node>> m_children;
};
} // namespace vr
