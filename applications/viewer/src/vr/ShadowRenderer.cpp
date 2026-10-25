#include <vr/ShadowRenderer.h>
#include <vr/ShadowCollector.h>
#include <vr/ShadowCameras.h>
#include <vr/LightCollector.h>
#include <vr/Light.h>
#include <vr/Geometry.h>
#include <algorithm>
#include <chrono>
#include <glm/gtc/matrix_transform.hpp>

namespace vr{
// Creates directional and point-light shadow resources.
ShadowRenderer::ShadowRenderer(const std::filesystem::path& root, int sunResolution,
                               int pointResolution)
    : m_pointResolution(pointResolution){
    m_depthProgram = std::make_shared<Program>(root / "shaders/shadow-depth.vs",
                                               root / "shaders/shadow-depth.fs");
    m_sun = std::make_shared<RenderToTexture>(sunResolution, false);
    m_sunCamera = std::make_shared<Camera>("sun-shadow");
    m_sun->setCamera(0, m_sunCamera);
    m_points.push_back(createPointTarget(0));
}

// Creates a cubemap with six independent cameras for one positional light.
std::shared_ptr<RenderToTexture> ShadowRenderer::createPointTarget(unsigned slot) const{
    auto target = std::make_shared<RenderToTexture>(m_pointResolution, true);
    for (unsigned face = 0; face < 6; ++face){
        target->setCamera(face, std::make_shared<Camera>("point-shadow-" + std::to_string(slot) +
                                                         "-" + std::to_string(face)));
    }
    return target;
}

// Attaches allocated depth targets and their independent cameras to a loaded scene.
void ShadowRenderer::attach(Scene& scene) const{
    scene.attachRenderTarget(m_sun);
    for (const auto& target : m_points){
        scene.attachRenderTarget(target);
    }
}

// Updates light depth images independently of observer frustum and portal culling.
void ShadowRenderer::render(Scene& scene, const glm::vec3& lodEye, bool enabled, bool filtered){
    m_stats ={};
    m_casters.clear();
    const auto start = std::chrono::steady_clock::now();
    auto root = scene.getRoot();
    if (!root->getState()){
        root->setState(std::make_shared<State>());
    }
    auto& state = *root->getState();
    state.setUniform("uShadows", enabled);
    state.setUniform("uShadowPCF", filtered);
    state.setUniform("uSunShadowIndex", -1);
    for (unsigned i = 0; i < State::MaxLights; ++i){
        state.setUniform("uPointShadowSlot[" + std::to_string(i) + "]", -1);
    }
    if (!enabled){
        return;
    }
    ShadowCollector collector(m_depthProgram);
    m_casters = collector.collect(*root, lodEye);
    const auto lights = collectLights(*root, lodEye);
    const auto bounds = root->getBound();
    if (!bounds.valid()){
        return;
    }
    bool sunRendered = false;
    unsigned slot = 0;
    for (unsigned i = 0; i < lights.size(); ++i){
        if (!lights[i].node->getEnabled() || !state.getLightEnabled(i)){
            continue;
        }
        const auto position = lights[i].model * lights[i].node->getPosition();
        if (position.w == 0 && !sunRendered){
            renderSun(state, bounds, glm::vec3(position), glm::vec3(0), static_cast<int>(i));
            sunRendered = true;
        } else if (position.w != 0){
            if (slot == m_points.size()){
                auto target = createPointTarget(slot);
                scene.attachRenderTarget(target);
                m_points.push_back(std::move(target));
            }
            renderPoint(state, bounds, lights[i], i, slot++);
        }
    }
    state.setShadowMap(0, m_sun->getTexture());
    for (unsigned i = 0; i < State::MaxLights; ++i){
        state.setShadowMap(i + 1, i < m_points.size() ? m_points[i]->getTexture() : nullptr);
    }
    m_stats.cpuMs =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}

// Renders only depth and alpha cutouts from one light camera.
void ShadowRenderer::drawDepthPass(const Camera& camera, bool radial, const glm::vec3& point,
                                   float farPlane, const Frustum* cone){
    const auto vp = camera.getProjection() * camera.getView();
    const Frustum frustum(vp);
    m_depthProgram->use();
    m_depthProgram->set("uLightVP", vp);
    m_depthProgram->set("uPointPass", radial);
    m_depthProgram->set("uShadowLightPosition", point);
    m_depthProgram->set("uShadowFarPlane", farPlane);
    m_depthProgram->set("uAlbedo", 0);
    if (!radial){
        glEnable(GL_POLYGON_OFFSET_FILL);
        glPolygonOffset(1.1f, 2);
    }
    for (const auto& node : m_casters){
        const auto bound = node.geometry->getBound().transformed(node.model);
        if (frustum.classify(bound) == Visibility::Outside ||
            (cone && cone->classify(bound) == Visibility::Outside)){
            continue;
        }
        m_depthProgram->set("uModel", node.model);
        const auto alpha = node.state.getUniforms().find("uAlphaCutoff");
        m_depthProgram->set("uAlphaCutoff", alpha == node.state.getUniforms().end()
                                                ? 0.0f
                                                : std::get<float>(alpha->second));
        const auto texture = node.state.getTexture(0);
        m_depthProgram->set("uHasAlbedo", static_cast<bool>(texture));
        if (texture){
            texture->apply(0);
        } else{
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, 0);
        }
        node.geometry->draw();
        ++m_stats.draws;
        m_stats.triangles += node.geometry->getTriangleCount();
    }
    ++m_stats.passes;
}

// Fits and renders the directional camera, then publishes its projection to State.
void ShadowRenderer::renderSun(State& state, const BoundingBox& bounds, const glm::vec3& direction,
                               const glm::vec3& point, int index){
    fitDirectionalCamera(*m_sunCamera, bounds, direction);
    const auto vp = m_sunCamera->getProjection() * m_sunCamera->getView();
    state.setUniform("uSunShadowIndex", index);
    state.setUniform("uSunVP", vp);
    state.setUniform("uSunPlane", glm::transpose(glm::inverse(glm::mat3(vp))));
    m_sun->render(0, [&](const Camera& camera){ drawDepthPass(camera, false, point, 1); });
}

// Renders all six radial depth faces and publishes their shared far distance.
void ShadowRenderer::renderPoint(State& state, const BoundingBox& bounds,
                                 const PositionedLight& light, unsigned index, unsigned slot){
    const auto position = light.model * light.node->getPosition();
    const auto point = glm::vec3(position) / position.w;
    float farPlane = 1;
    for (auto corner : bounds.corners()){
        farPlane = std::max(farPlane, glm::distance(point, corner) + 1);
    }
    if (light.node->getRange() > 0){
        farPlane = std::min(farPlane, light.node->getRange());
    }
    std::array<std::shared_ptr<Camera>, 6> cameras;
    const auto& target = m_points.at(slot);
    std::copy(target->getCameras().begin(), target->getCameras().end(), cameras.begin());
    configurePointCameras(cameras, point, farPlane);
    std::optional<Frustum> cone;
    if (light.node->getSpot()){
        const auto direction =
            glm::normalize(glm::mat3(light.model) * light.node->getSpotDirection());
        Camera beam;
        beam.setLookAt(point, point + direction,
                       std::abs(direction.y) > 0.95f ? glm::vec3(0, 0, 1) : glm::vec3(0, 1, 0));
        beam.setPerspective(light.node->getOuterAngle() * 2, 1, 0.05f, farPlane);
        cone.emplace(beam.getProjection() * beam.getView());
    }
    state.setUniform("uPointShadowSlot[" + std::to_string(index) + "]", static_cast<int>(slot));
    state.setUniform("uPointShadowFar[" + std::to_string(slot) + "]", farPlane);
    for (unsigned face = 0; face < 6; ++face){
        target->render(face, [&](const Camera& camera){
            drawDepthPass(camera, true, point, farPlane, cone ? &*cone : nullptr);
        });
    }
}
} // namespace vr
