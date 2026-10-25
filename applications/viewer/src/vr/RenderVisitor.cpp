#include <chrono>
#include <cmath>
#include <stdexcept>
#include <vr/Camera.h>
#include <vr/Geometry.h>
#include <vr/LOD.h>
#include <vr/Light.h>
#include <vr/RenderVisitor.h>

namespace vr{
// Collects transformed lights and then draws the scene instances.
void RenderVisitor::render(Node& root, const Camera& camera, const glm::mat4* frozen){
    m_camera = &camera;
    setEye(camera.getPosition());
    m_lights.clear();
    m_stats ={};
    m_collectingLights = true;
    root.accept(*this);
    m_collectingLights = false;
    // An off-screen light can still illuminate a visible object.
    const auto start = std::chrono::steady_clock::now();
    m_culler.collect(root, frozen ? *frozen : camera.getProjection() * camera.getView(),
                     camera.getPosition(), m_culling);
    m_stats.cullCpuMs =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    m_stats.boundsTested = m_culler.getStats().boundsTested;
    m_stats.subtreesRejected = m_culler.getStats().subtreesRejected;
    m_stats.lodLevels = m_culler.getStats().lodLevels;
    const auto submit = std::chrono::steady_clock::now();
    for (const auto& item : m_culler.getRenderList()){
        geometry(*item.geometry, item.model, item.state);
        if (!item.room.empty()){
            ++m_stats.roomDraws[item.room];
        }
    }
    m_stats.submitCpuMs =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - submit)
            .count();
}

// Handles a geometry instance with its resolved transform and rendering state.
void RenderVisitor::geometry(Geometry& node, const glm::mat4& model, const State& state){
    if (m_collectingLights){
        return;
    }
    state.apply();
    const auto& program = *state.getProgram();
    program.set("uModel", model);
    program.set("uView", m_camera->getView());
    program.set("uProjection", m_camera->getProjection());
    const glm::mat3 linear(model);
    if (std::abs(glm::determinant(linear)) < 1e-8f){
        throw std::runtime_error("Singular model transform");
    }
    program.set("uNormal", glm::transpose(glm::inverse(linear)));
    program.set("uEye", m_camera->getPosition());
    program.set("uLightCount", static_cast<int>(m_lights.size()));
    for (unsigned i = 0; i < m_lights.size(); ++i){
        m_lights[i].node->apply(program, i, m_lights[i].model, state.getLightEnabled(i));
    }
    node.draw();
    ++m_stats.draws;
    m_stats.triangles += node.getTriangleCount();
}

// Handles a light with the transform accumulated along its current path.
void RenderVisitor::light(Light& node, const glm::mat4& model, const State&){
    if (!m_collectingLights){
        return;
    }
    if (m_lights.size() >= State::MaxLights){
        throw std::runtime_error("Too many lights");
    }
    m_lights.push_back({&node, model});
}

// Records the detail level selected for this traversal path.
void RenderVisitor::lod(LOD& node, std::size_t level){
    if (!m_collectingLights){
        m_stats.lodLevels[node.getName()] = level;
    }
}
} // namespace vr
