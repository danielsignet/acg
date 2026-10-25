#include <algorithm>
#include <chrono>
#include <cmath>
#include <stdexcept>
#include <vr/Camera.h>
#include <vr/Geometry.h>
#include <vr/LOD.h>
#include <vr/Light.h>
#include <vr/RenderVisitor.h>
#include <vr/DeferredRenderer.h>

namespace vr{
// Collects transformed lights and then draws the scene instances.
void RenderVisitor::render(Node& root, const Camera& camera, const glm::mat4* frozen,
                           const PortalResult* portals, DeferredRenderer* deferred){
    m_camera = &camera;
    m_deferred = deferred;
    m_globalState = root.getState() ? m_states.front().merged(*root.getState()) : m_states.front();
    setEye(camera.getPosition());
    m_stats ={};
    collectLights(root);
    collectGeometry(root, camera, frozen ? *frozen : camera.getProjection() * camera.getView(),
                    portals);
    drawRenderList(camera);
}

// Collects lights before culling so off-screen lights still affect visible geometry.
void RenderVisitor::collectLights(Node& root){
    m_lights = vr::collectLights(root, m_camera->getPosition());
}

// Builds the visible render list and records culling statistics.
void RenderVisitor::collectGeometry(Node& root, const Camera& camera,
                                    const glm::mat4& viewProjection, const PortalResult* portals){
    const auto start = std::chrono::steady_clock::now();
    m_culler.collect(root, viewProjection, camera.getPosition(), m_culling, portals);
    m_stats.cullCpuMs =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    m_stats.boundsTested = m_culler.getStats().boundsTested;
    m_stats.subtreesRejected = m_culler.getStats().subtreesRejected;
    m_stats.lodLevels = m_culler.getStats().lodLevels;
    m_stats.portalRejected = m_culler.getStats().portalRejected;
}

// Draws opaque instances first and transparent instances in stable back-to-front order.
void RenderVisitor::drawRenderList(const Camera& camera){
    std::vector<const RenderNode*> opaque, transparent;
    for (const auto& item : m_culler.getRenderList()){
        (item.state.getBlend() ? transparent : opaque).push_back(&item);
    }
    const auto distance = [&](const RenderNode* item){
        const auto center =
            glm::vec3(item->model * glm::vec4(item->geometry->getBound().getCenter(), 1));
        return glm::dot(center - camera.getPosition(), center - camera.getPosition());
    };
    std::stable_sort(transparent.begin(), transparent.end(),
                     [&](const auto* a, const auto* b){ return distance(a) > distance(b); });
    const auto submit = std::chrono::steady_clock::now();
    if (m_deferred){
        m_deferred->render(
            camera, m_globalState, m_lights,
            [&](const auto& program){
                for (const auto* item : opaque){
                    drawInstance(*item, program);
                }
            },
            [&]{
                for (const auto* item : transparent){
                    drawInstance(*item,{}, true);
                }
            });
    } else{
        for (const auto* item : opaque){
            drawInstance(*item);
        }
        for (const auto* item : transparent){
            drawInstance(*item);
        }
    }
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    m_stats.submitCpuMs =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - submit)
            .count();
}

// Submits one resolved instance and updates room and transparency counts.
void RenderVisitor::drawInstance(const RenderNode& item, const std::shared_ptr<Program>& program,
                                 bool linearOutput){
    drawGeometry(item, program, linearOutput);
    if (!item.room.empty()){
        ++m_stats.roomDraws[item.room];
    }
    if (item.state.getBlend()){
        ++m_stats.transparentDraws;
    }
}

// Submits a mesh with its resolved transform and rendering state.
void RenderVisitor::drawGeometry(const RenderNode& item,
                                 const std::shared_ptr<Program>& overrideProgram,
                                 bool linearOutput){
    auto& node = *item.geometry;
    const auto& model = item.model;
    auto state = item.state;
    if (overrideProgram){
        state.setProgram(overrideProgram);
    }
    state.apply();
    const auto& program = *state.getProgram();
    program.set("uLinearOutput", linearOutput);
    int lightMask = 0;
    for (unsigned i = 0; i < State::MaxLights; ++i){
        if (state.getLightEnabled(i)){
            lightMask |= 1 << i;
        }
    }
    program.set("uLightMask", lightMask);
    program.set("uToon", m_shadingMode == ShadingMode::Toon);
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

} // namespace vr
