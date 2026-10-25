#pragma once
#include "BoundingBox.h"
#include "State.h"
#include "UpdateCallback.h"
#include <memory>
#include <string>
#include <vector>

namespace vr{
class NodeVisitor;
class Group;

class Node : public std::enable_shared_from_this<Node> {
public:
    // Creates a named node without local rendering overrides or behavior.
    explicit Node(std::string name = {}) : m_name(std::move(name)){
    }

    // Prevents copying ownership or identity of this object.
    Node(const Node&) = delete;

    // Prevents copying ownership or identity of this object.
    Node& operator=(const Node&) = delete;

    // Allows derived objects to release their owned resources safely.
    virtual ~Node() = default;

    // Dispatches this concrete node type to the visitor.
    virtual void accept(NodeVisitor& visitor) = 0;

    // Reports whether the requested node is part of this subtree.
    virtual bool contains(const Node* node) const{
        return this == node;
    }

    // Returns the node name used for lookup and graph visualization.
    const std::string& getName() const{
        return m_name;
    }

    // Changes the node name without changing its identity.
    void setName(std::string name){
        m_name = std::move(name);
    }

    // Returns the local rendering overrides attached to this node.
    const std::shared_ptr<State>& getState() const{
        return m_state;
    }

    // Attaches rendering overrides inherited by this node and its descendants.
    void setState(std::shared_ptr<State> state){
        m_state = std::move(state);
    }

    // Returns the callback executed during graph updates.
    const std::shared_ptr<UpdateCallback>& getUpdateCallback() const{
        return m_callback;
    }

    // Attaches behavior to be executed by the update visitor.
    void setUpdateCallback(std::shared_ptr<UpdateCallback> callback){
        m_callback = std::move(callback);
    }

    // Returns weak parent references without extending their lifetimes.
    const std::vector<std::weak_ptr<Node>>& getParents() const{
        return m_parents;
    }

    // Bounds are expressed in the parent's coordinates, including a local transform.
    const BoundingBox& getBound() const;

    virtual BoundingBox computeBound() const{
        return{};
    }

    void dirtyBound();

    bool isBoundDirty() const{
        return m_boundDirty;
    }

    std::size_t getBoundBuildCount() const{
        return m_boundBuildCount;
    }

    void setVisible(bool visible){
        m_visible = visible;
    }

    bool getVisible() const{
        return m_visible;
    }

    void setRoomId(std::string id){
        m_roomId = std::move(id);
    }

    const std::string& getRoomId() const{
        return m_roomId;
    }

private:
    friend class Group;
    std::string m_name;
    std::shared_ptr<State> m_state;
    std::shared_ptr<UpdateCallback> m_callback;
    std::vector<std::weak_ptr<Node>> m_parents;
    mutable BoundingBox m_bound;
    mutable bool m_boundDirty = true;
    mutable std::size_t m_boundBuildCount = 0;
    bool m_visible = true;
    std::string m_roomId;
};
} // namespace vr
