#include <vr/ShadowRenderer.h>
#include <vr/ShadowCollector.h>
#include <vr/ShadowCameras.h>
#include <vr/LightCollector.h>
#include <vr/Light.h>
#include <vr/Geometry.h>
#include <algorithm>
#include <chrono>

namespace vr{
// Creates directional and point-light shadow resources.
ShadowRenderer::ShadowRenderer(const std::filesystem::path& root, int sunResolution,
                               int pointResolution){
    m_depthProgram = std::make_shared<Program>(root / "shaders/shadow-depth.vs",
                                               root / "shaders/shadow-depth.fs");
    m_sun = std::make_shared<RenderToTexture>(sunResolution, false);
    m_point = std::make_shared<RenderToTexture>(pointResolution, true);
    m_sunCamera = std::make_shared<Camera>("sun-shadow");
    m_sun->setCamera(0, m_sunCamera);
    for (unsigned face = 0; face < 6; ++face){
        m_pointCameras[face] = std::make_shared<Camera>("point-shadow-" + std::to_string(face));
        m_point->setCamera(face, m_pointCameras[face]);
    }
}

// Attaches both render targets and their seven light cameras to a loaded scene.
void ShadowRenderer::attach(Scene& scene) const{
    scene.attachRenderTarget(m_sun);
    scene.attachRenderTarget(m_point);
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
    state.setUniform("uPointShadowIndex", -1);
    state.setShadowMap(0, m_sun->getTexture());
    state.setShadowMap(1, m_point->getTexture());
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
    int sunIndex = -1, pointIndex = -1;
    glm::vec3 direction(0), point(0);
    for (unsigned i = 0; i < lights.size(); ++i){
        if (!lights[i].node->getEnabled() || !state.getLightEnabled(i)){
            continue;
        }
        const auto position = lights[i].model * lights[i].node->getPosition();
        if (position.w == 0 && sunIndex < 0){
            sunIndex = static_cast<int>(i);
            direction = glm::vec3(position);
        } else if (position.w != 0 && pointIndex < 0){
            pointIndex = static_cast<int>(i);
            point = glm::vec3(position) / position.w;
        }
    }
    // No camera frustum or portal list is used here: off-camera objects still cast shadows.
    if (sunIndex >= 0){
        renderSun(state, bounds, direction, point, sunIndex);
    }
    if (pointIndex >= 0){
        renderPoint(state, bounds, point, pointIndex);
    }
    m_stats.cpuMs =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}

// Renders only depth and alpha cutouts from one light camera.
void ShadowRenderer::drawDepthPass(const Camera& camera, bool radial, const glm::vec3& point,
                                   float farPlane){
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
        if (frustum.classify(node.geometry->getBound().transformed(node.model)) ==
            Visibility::Outside){
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
void ShadowRenderer::renderPoint(State& state, const BoundingBox& bounds, const glm::vec3& point,
                                 int index){
    float farPlane = 1;
    for (auto corner : bounds.corners()){
        farPlane = std::max(farPlane, glm::distance(point, corner) + 1);
    }
    configurePointCameras(m_pointCameras, point, farPlane);
    state.setUniform("uPointShadowIndex", index);
    state.setUniform("uPointShadowFar", farPlane);
    for (unsigned face = 0; face < 6; ++face){
        m_point->render(
            face, [&](const Camera& camera){ drawDepthPass(camera, true, point, farPlane); });
    }
}
} // namespace vr
