#include <utility>
#include <vr/Camera.h>
#include <vr/Geometry.h>
#include <vr/Group.h>
#include <vr/LOD.h>
#include <vr/Light.h>
#include <vr/ModelInstance.h>
#include <vr/PathVisitor.h>
#include <vr/Transform.h>

namespace vr{
namespace{
class CullingScope final{
public:
    // Inherits a local exemption and restores the parent value on every exit path.
    CullingScope(bool& enabled, bool local) : m_enabled(enabled), m_previous(enabled){
        m_enabled = enabled && local;
    }

    // Restores the parent flag during normal return and exception unwinding.
    ~CullingScope(){
        m_enabled = m_previous;
    }

    // Each inherited flag has exactly one restoration owner.
    CullingScope(const CullingScope&) = delete;

    // Prevents replacing responsibility for a live inherited flag.
    CullingScope& operator=(const CullingScope&) = delete;

private:
    bool& m_enabled;
    bool m_previous;
};

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
    CullingScope culling(m_cullingEnabled, node.getCullingEnabled());
    if (!node.getState()){
        action();
        return;
    }
    StackScope<State> stateScope(m_states, m_states.back().merged(*node.getState()));
    action();
}

// Applies local state while traversing the group's children.
void PathVisitor::apply(Group& node){
    auto instance = m_instances.back();
    if (auto model = dynamic_cast<ModelInstance*>(&node)){
        instance ={model, m_models.back()};
    }
    StackScope<InstancePath> scope(m_instances, instance);
    scoped(node, [&]{ traverse(node); });
}

// Accumulates the local matrix and restores it after visiting descendants.
void PathVisitor::apply(Transform& node){
    StackScope<glm::mat4> matrixScope(m_models, m_models.back() * node.getMatrix());
    scoped(node, [&]{ traverse(node); });
}

// Partitions inherited coverage between neighboring detail levels during a crossfade.
void PathVisitor::apply(LOD& node){
    scoped(node, [&]{
        const auto center = glm::vec3(m_models.back() * glm::vec4(node.getCenter(), 1));
        const float distance = glm::distance(m_eye, center);
        const auto level = node.selectLevel(distance);
        lod(node, level);
        const auto selection = node.select(distance);
        if (!m_lodFading || selection.nearLevel == selection.farLevel){
            node.getChildren().at(level)->accept(*this);
            return;
        }
        const auto& uniforms = m_states.back().getUniforms();
        const auto found = uniforms.find("uLodRange");
        const auto range =
            found == uniforms.end() ? glm::vec2(0, 1) : std::get<glm::vec2>(found->second);
        const float split = glm::mix(range.y, range.x, selection.blend);
        const auto visit = [&](std::size_t index, glm::vec2 coverage){
            auto state = m_states.back();
            state.setUniform("uLodRange", coverage);
            StackScope<State> scope(m_states, std::move(state));
            node.getChildren().at(index)->accept(*this);
        };
        visit(selection.nearLevel,{range.x, split});
        visit(selection.farLevel,{split, range.y});
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
