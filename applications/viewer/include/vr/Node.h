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

class Node : public std::enable_shared_from_this<Node>{
public:
    // Creates a named node without local rendering overrides or behavior.
    explicit Node(std::string name ={}) : m_name(std::move(name)){
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
    BoundingBox getWorldBound(const glm::mat4& parent) const;

    std::size_t getWorldBoundBuildCount() const{
        return m_worldBoundBuildCount;
    }

    // Computes this node type's bound in its parent coordinate system.
    virtual BoundingBox computeBound() const{
        return{};
    }

    // Invalidates the cached bound and every ancestor reached through DAG parents.
    void dirtyBound() const;

    // Enables volume rejection for this subtree; disabled nodes still obey visibility.
    void setCullingEnabled(bool enabled){
        m_cullingEnabled = enabled;
        dirtyBound();
    }

    // Reports whether this subtree permits frustum and portal rejection.
    bool getCullingEnabled() const{
        return m_cullingEnabled;
    }

    // Keeps ancestors traversable when a descendant must bypass volume rejection.
    bool hasCullingExemption() const{
        getBound();
        return m_cullingExemption;
    }

    // Computes the exemption cached alongside the physical bounding box.
    virtual bool computeCullingExemption() const{
        return !m_cullingEnabled;
    }

    // Reports whether the next bound access must rebuild the cache.
    bool isBoundDirty() const{
        return m_boundDirty;
    }

    // Returns the number of actual bound rebuilds for diagnostics.
    std::size_t getBoundBuildCount() const{
        return m_boundBuildCount;
    }

    // Includes or excludes the subtree from rendering without changing its bound.
    void setVisible(bool visible){
        m_visible = visible;
    }

    // Returns the local rendering visibility flag.
    bool getVisible() const{
        return m_visible;
    }

    // Tags a subtree with the visibility cell inherited by its descendants.
    void setRoomId(std::string id){
        m_roomId = std::move(id);
    }

    // Returns the local cell tag; an empty tag inherits the parent cell.
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

    struct WorldBound{
        glm::mat4 parent;
        BoundingBox bound;
        std::size_t revision;
    };

    mutable std::vector<WorldBound> m_worldBounds;
    mutable std::size_t m_worldBoundBuildCount = 0;
    mutable bool m_cullingExemption = false;
    bool m_cullingEnabled = true;
    bool m_visible = true;
    std::string m_roomId;
};
} // namespace vr
