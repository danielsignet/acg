#pragma once
#include "NodeVisitor.h"
#include "State.h"
#include <functional>
#include <vector>

namespace vr{
class PathVisitor : public NodeVisitor{
public:
    // Initializes traversal with a base state and the identity model matrix.
    explicit PathVisitor(State initial ={})
        : m_states{std::move(initial)}, m_models{glm::mat4(1)}{
    }

    // Applies local state while traversing the group's children.
    void apply(Group& node) override;

    // Accumulates the local matrix and restores it after visiting descendants.
    void apply(Transform& node) override;

    // Visits the detail branch selected by world-space camera distance.
    void apply(LOD& node) override;

    // Passes the mesh with its resolved matrix and state to the geometry hook.
    void apply(Geometry& node) override;

    // Passes the light with its resolved matrix and state to the lighting hook.
    void apply(Light& node) override;

    // Leaves the camera unchanged during rendering-path traversal.
    void apply(Camera& node) override;

    // Sets the world-space camera position used for per-path LOD selection.
    void setEye(const glm::vec3& eye){
        m_eye = eye;
    }

protected:
    // Handles a geometry instance with its resolved transform and rendering state.
    virtual void geometry(Geometry&, const glm::mat4&, const State&){
    }

    // Handles a light with the transform accumulated along its current path.
    virtual void light(Light&, const glm::mat4&, const State&){
    }

    // Records the detail level selected for this traversal path.
    virtual void lod(LOD&, std::size_t){
    }

    // Restores inherited rendering state automatically when leaving this node.
    void scoped(Node& node, const std::function<void()>& action);
    std::vector<State> m_states;
    std::vector<glm::mat4> m_models;
    glm::vec3 m_eye{0};
};
} // namespace vr
