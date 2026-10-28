#include <algorithm>
#include <chrono>
#include <glm/gtc/matrix_transform.hpp>
#include <vr/FramebufferState.h>
#include <vr/GLState.h>
#include <vr/Geometry.h>
#include <vr/Light.h>
#include <vr/LightCollector.h>
#include <vr/ShadowCameras.h>
#include <vr/ShadowCollector.h>
#include <vr/ShadowRenderer.h>

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
    m_pointCaches.emplace_back();
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
    m_batches.clear();
    m_worldBounds.clear();
    m_visibleModels.clear();
    const auto start = std::chrono::steady_clock::now();
    auto root = scene.getRoot();
    if (!root->getState()){
        root->setState(std::make_shared<State>());
    }
    auto& state = *root->getState();
    state.setPassUniform("uShadows", enabled);
    state.setPassUniform("uShadowPCF", filtered);
    state.setPassUniform("uSunShadowIndex", -1);
    for (unsigned i = 0; i < State::MaxLights; ++i){
        state.setPassUniform("uPointShadowSlot[" + std::to_string(i) + "]", -1);
    }
    if (!enabled){
        return;
    }
    m_casters = m_collector.collect(*root, lodEye);
    const auto lights = collectLights(*root, lodEye);
    const auto bounds = root->getBound();
    if (!bounds.valid()){
        return;
    }
    prepareCasters();
    // Preserve caller state once, rather than querying/restoring it for all 25 light views.
    FramebufferState saved;
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
                m_pointCaches.emplace_back();
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

// Resolves depth-only batch state and world bounds once for this frame's casters.
void ShadowRenderer::prepareCasters(){
    std::vector<StaticCaster> staticCasters;
    m_worldBounds.reserve(m_casters.size());
    for (std::size_t index = 0; index < m_casters.size(); ++index){
        const auto& node = m_casters[index];
        m_worldBounds.push_back(node.geometry->getWorldBound(node.model));
        const auto alpha = node.state.getUniforms().find("uAlphaCutoff");
        float cutoff =
            alpha == node.state.getUniforms().end() ? 0.0f : std::get<float>(alpha->second);
        auto albedo = cutoff > 0 ? node.state.getTexture(0) : nullptr;
        if (!albedo){
            cutoff = 0;
        }
        const auto coverage = node.state.getUniforms().find("uLodRange");
        const auto range = coverage == node.state.getUniforms().end()
                               ? glm::vec2(0, 1)
                               : std::get<glm::vec2>(coverage->second);
        const bool dynamic = node.dynamic || (albedo && albedo->isRenderTarget());
        if (!dynamic){
            staticCasters.push_back({node.geometry, node.geometry->getDataRevision(),
                                     albedo ? albedo->getRevision() : 0, node.model, cutoff, range,
                                     node.cullingEnabled});
        }
        const auto found =
            std::find_if(m_batches.begin(), m_batches.end(), [&](const Batch& batch){
                return batch.geometry == node.geometry && batch.albedo == albedo &&
                       batch.alphaCutoff == cutoff && batch.lodRange == range &&
                       batch.dynamic == dynamic;
            });
        if (found == m_batches.end()){
            m_batches.push_back(
                {node.geometry, std::move(albedo), cutoff, range,{index}, dynamic});
        } else{
            found->casters.push_back(index);
        }
    }
    m_hasStatic = !staticCasters.empty();
    if (staticCasters != m_staticCasters){
        m_staticCasters = std::move(staticCasters);
        ++m_staticRevision;
    }
}

void ShadowRenderer::invalidate(){
    m_sunCache ={};
    for (auto& target : m_pointCaches){
        for (auto& face : target){
            face ={};
        }
    }
}

bool ShadowRenderer::hasDynamicCasters(const Frustum& frustum, const Frustum* cone) const{
    for (const auto& batch : m_batches){
        if (!batch.dynamic){
            continue;
        }
        for (const auto index : batch.casters){
            if (!m_casters[index].cullingEnabled ||
                (frustum.classify(m_worldBounds[index]) != Visibility::Outside &&
                 (!cone || cone->classify(m_worldBounds[index]) != Visibility::Outside))){
                return true;
            }
        }
    }
    return false;
}

// Moving light views use the normal single pass. Stable views cache static depth,
// copy it before dynamic draws, and can reuse the complete face when no dynamic caster remains.
void ShadowRenderer::renderFace(RenderToTexture& target, unsigned layer, FaceCache& cache,
                                bool radial, const glm::vec3& point, float farPlane,
                                const Frustum* cone){
    ++m_stats.passes;
    const auto& camera = *target.getCameras().at(layer);
    const auto projection = camera.getProjection() * camera.getView();
    const bool sameCone = cone ? cache.cone && *cache.cone == cone->getPlanes() : !cache.cone;
    const bool stable = cache.observed && cache.projection == projection && sameCone &&
                        cache.staticRevision == m_staticRevision && cache.point == point &&
                        cache.farPlane == farPlane && cache.radial == radial;
    if (!m_caching || !m_hasStatic || !stable){
        cache.ready = false;
        cache.observed = true;
        cache.projection = projection;
        cache.point = point;
        cache.farPlane = farPlane;
        cache.radial = radial;
        cache.cone =
            cone ? std::optional<std::array<glm::vec4, 6>>(cone->getPlanes()) : std::nullopt;
        cache.staticRevision = m_staticRevision;
        cache.hadDynamic = true;
        target.renderFace(
            layer, [&](const Camera& view){ drawDepthPass(view, radial, point, farPlane, cone); });
        cache.activeRevision = target.getContentRevision(layer);
        return;
    }
    if (!cache.ready){
        if (!cache.depth){
            cache.depth = std::make_unique<RenderToTexture>(target.getTexture()->getWidth(), false);
        }
        cache.depth->setCamera(0, target.getCameras().at(layer));
        cache.depth->renderFace(0, [&](const Camera& view){
            drawDepthPass(view, radial, point, farPlane, cone, Casters::Static);
        });
        cache.ready = true;
        ++m_stats.staticRebuilds;
    }
    ++m_stats.cacheHits;
    const bool dynamic = hasDynamicCasters(Frustum(projection), cone);
    if (!dynamic && !cache.hadDynamic && cache.activeRevision == target.getContentRevision(layer)){
        ++m_stats.reusedFaces;
        return;
    }
    target.renderFace(layer, [&](const Camera& view){
        target.copyDepthFrom(*cache.depth, layer);
        if (dynamic){
            drawDepthPass(view, radial, point, farPlane, cone, Casters::Dynamic);
        }
    });
    cache.hadDynamic = dynamic;
    cache.activeRevision = target.getContentRevision(layer);
}

// Renders only depth and alpha cutouts from one light camera.
void ShadowRenderer::drawDepthPass(const Camera& camera, bool radial, const glm::vec3& point,
                                   float farPlane, const Frustum* cone, Casters selection){
    const auto vp = camera.getProjection() * camera.getView();
    const Frustum frustum(vp);
    m_depthProgram->use();
    m_depthProgram->set("uLightVP", vp);
    m_depthProgram->set("uPointPass", radial);
    m_depthProgram->set("uShadowLightPosition", point);
    m_depthProgram->set("uShadowFarPlane", farPlane);
    m_depthProgram->set("uAlbedo", 0);
    if (!radial){
        GLState::enable(GL_POLYGON_OFFSET_FILL);
        GLState::polygonOffset(1.1f, 2);
    }
    for (const auto& batch : m_batches){
        if ((selection == Casters::Static && batch.dynamic) ||
            (selection == Casters::Dynamic && !batch.dynamic)){
            continue;
        }
        m_visibleModels.clear();
        for (const auto index : batch.casters){
            if (m_casters[index].cullingEnabled){
                ++m_stats.boundsTested;
                const auto& bound = m_worldBounds[index];
                if (frustum.classify(bound) == Visibility::Outside ||
                    (cone && cone->classify(bound) == Visibility::Outside)){
                    ++m_stats.rejected;
                    continue;
                }
            } else{
                ++m_stats.unculled;
            }
            m_visibleModels.push_back(m_casters[index].model);
        }
        if (m_visibleModels.empty()){
            continue;
        }
        m_depthProgram->set("uAlphaCutoff", batch.alphaCutoff);
        m_depthProgram->set("uLodRange", batch.lodRange);
        m_depthProgram->set("uHasAlbedo", static_cast<bool>(batch.albedo));
        if (batch.albedo){
            batch.albedo->apply(0);
        } else{
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, 0);
        }
        const bool instanced = m_instancing && m_visibleModels.size() > 1;
        m_depthProgram->set("uInstanced", instanced);
        if (instanced){
            batch.geometry->drawInstanced(m_visibleModels);
            ++m_stats.draws;
        } else{
            for (const auto& model : m_visibleModels){
                m_depthProgram->set("uModel", model);
                batch.geometry->draw();
                ++m_stats.draws;
            }
        }
        m_stats.instances += static_cast<unsigned>(m_visibleModels.size());
        m_stats.triangles += batch.geometry->getTriangleCount() * m_visibleModels.size();
    }
}

// Fits and renders the directional camera, then publishes its projection to State.
void ShadowRenderer::renderSun(State& state, const BoundingBox& bounds, const glm::vec3& direction,
                               const glm::vec3& point, int index){
    fitDirectionalCamera(*m_sunCamera, bounds, direction);
    const auto vp = m_sunCamera->getProjection() * m_sunCamera->getView();
    state.setPassUniform("uSunShadowIndex", index);
    state.setPassUniform("uSunVP", vp);
    state.setPassUniform("uSunPlane", glm::transpose(glm::inverse(glm::mat3(vp))));
    renderFace(*m_sun, 0, m_sunCache, false, point, 1);
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
    state.setPassUniform("uPointShadowSlot[" + std::to_string(index) + "]", static_cast<int>(slot));
    state.setPassUniform("uPointShadowFar[" + std::to_string(slot) + "]", farPlane);
    for (unsigned face = 0; face < 6; ++face){
        renderFace(*target, face, m_pointCaches.at(slot).at(face), true, point, farPlane,
                   cone ? &*cone : nullptr);
    }
}
} // namespace vr
