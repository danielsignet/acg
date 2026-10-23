#include <utility>
#include <vr/Camera.h>
#include <vr/Geometry.h>
#include <vr/Group.h>
#include <vr/LOD.h>
#include <vr/Light.h>
#include <vr/PathVisitor.h>
#include <vr/Transform.h>

namespace vr{
namespace{
template <class Value> class StackScope final{
public:
    // Pushes one traversal value and owns its corresponding pop operation.
    StackScope(std::vector<Value>& stack, Value value) : m_stack(stack){
        m_stack.push_back(std::move(value));
    }

    // Restores the parent scope during normal return and exception unwinding.
    ~StackScope(){
        m_stack.pop_back();
    }

    // Each pushed value must have exactly one owner.
    StackScope(const StackScope&) = delete;

    // Prevents assigning responsibility for a live stack entry.
    StackScope& operator=(const StackScope&) = delete;

private:
    std::vector<Value>& m_stack;
};
} // namespace

// Restores inherited rendering state automatically when leaving this node.
void PathVisitor::scoped(Node& node, const std::function<void()>& action){
    State state = m_states.back();
    if (node.getState()){
        state = state.merged(*node.getState());
    }
    StackScope<State> stateScope(m_states, std::move(state));
    action();
}

// Applies local state while traversing the group's children.
void PathVisitor::apply(Group& node){
    scoped(node, [&]{ traverse(node); });
}

// Accumulates the local matrix and restores it after visiting descendants.
void PathVisitor::apply(Transform& node){
    StackScope<glm::mat4> matrixScope(m_models, m_models.back() * node.getMatrix());
    scoped(node, [&]{ traverse(node); });
}

// Visits the detail branch selected by world-space camera distance.
void PathVisitor::apply(LOD& node){
    scoped(node, [&]{
        const auto center = glm::vec3(m_models.back() * glm::vec4(node.getCenter(), 1));
        const auto level = node.selectLevel(glm::distance(m_eye, center));
        lod(node, level);
        node.getChildren().at(level)->accept(*this);
    });
}

// Passes the mesh with its resolved matrix and state to the geometry hook.
void PathVisitor::apply(Geometry& node){
    scoped(node, [&]{ geometry(node, m_models.back(), m_states.back()); });
}

// Passes the light with its resolved matrix and state to the lighting hook.
void PathVisitor::apply(Light& node){
    scoped(node, [&]{ light(node, m_models.back(), m_states.back()); });
}

// Leaves the camera unchanged during rendering-path traversal.
void PathVisitor::apply(Camera&){
}
} // namespace vr
