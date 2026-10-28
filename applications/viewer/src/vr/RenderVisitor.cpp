#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <tuple>
#include <vr/Camera.h>
#include <vr/DeferredRenderer.h>
#include <vr/EnvironmentMap.h>
#include <vr/FramebufferState.h>
#include <vr/GLState.h>
#include <vr/Geometry.h>
#include <vr/LOD.h>
#include <vr/Light.h>
#include <vr/PlanarReflection.h>
#include <vr/RenderVisitor.h>

namespace vr{
// Collects lights and geometry, invokes the optional preparation hook, then draws the scene.
void RenderVisitor::render(Node& root, const Camera& camera, const glm::mat4* frozen,
                           const PortalResult* portals, DeferredRenderer* deferred,
                           const std::function<void()>& beforeDraw){
    m_camera = &camera;
    m_deferred = deferred;
    m_globalState = root.getState() ? m_states.front().merged(*root.getState()) : m_states.front();
    setEye(camera.getPosition());
    m_stats ={};
    GLint viewport[4]{};
    GLState::getIntegerv(GL_VIEWPORT, viewport);
    m_viewport = glm::vec4(viewport[0], viewport[1], viewport[2], viewport[3]);
    m_environmentPrograms.clear();
    collectLights(root);
    collectGeometry(root, camera, frozen ? *frozen : camera.getProjection() * camera.getView(),
                    portals);
    if (beforeDraw){
        beforeDraw();
    }
    drawRenderList(camera);
}

// Collects lights before culling so off-screen lights still affect visible geometry.
void RenderVisitor::collectLights(Node& root){
    FrameProfiler::Scope pass(m_profiler, FrameProfiler::Stage::Lights);
    m_lights = vr::collectLights(root, m_camera->getPosition());
    for (unsigned i = 0; i < m_lights.size(); ++i){
        m_stats.lights += m_lights[i].node->getEnabled() && m_globalState.getLightEnabled(i);
    }
}

// Builds the visible render list and records culling statistics.
void RenderVisitor::collectGeometry(Node& root, const Camera& camera,
                                    const glm::mat4& viewProjection, const PortalResult* portals){
    FrameProfiler::Scope pass(m_profiler, FrameProfiler::Stage::Culling);
    const auto start = std::chrono::steady_clock::now();
    m_culler.collect(root, viewProjection, camera.getPosition(), m_culling, portals);
    m_stats.cullCpuMs =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    m_stats.visitedNodes = m_culler.getStats().visitedNodes;
    m_stats.boundsTested = m_culler.getStats().boundsTested;
    m_stats.subtreesRejected = m_culler.getStats().subtreesRejected;
    m_stats.lodLevels = m_culler.getStats().lodLevels;
    m_stats.portalRejected = m_culler.getStats().portalRejected;
}

// Draws opaque instances first and transparent instances in stable back-to-front order.
void RenderVisitor::drawRenderList(const Camera& camera){
    m_globalState.setUniform("uBrdfMode", m_shadingMode == ShadingMode::CookTorrance ? 2
                                          : m_shadingMode == ShadingMode::BlinnPhong ? 1
                                                                                     : 0);
    std::vector<const RenderNode*> opaque, transparent;
    for (const auto& item : m_culler.getRenderList()){
        const auto mirror = item.state.getUniforms().find("uMirror");
        if ((m_clipPlane || m_environmentCapture) && mirror != item.state.getUniforms().end() &&
            std::get<bool>(mirror->second)){
            continue;
        }
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
            [&](const auto& program){ drawOpaque(opaque, program, false); },
            [&]{
                for (const auto* item : transparent){
                    drawInstance(*item,{}, true);
                }
            });
    } else{
        if (m_environment){
            FrameProfiler::Scope pass(m_profiler, FrameProfiler::Stage::Environment);
            m_environment->drawSky(camera, m_linearOutput);
            ++m_stats.screenDraws;
        }
        {
            FrameProfiler::Scope pass(m_profiler, FrameProfiler::Stage::Forward);
            drawOpaque(opaque,{}, m_linearOutput);
        }
        {
            FrameProfiler::Scope pass(m_profiler, FrameProfiler::Stage::Transparency);
            for (const auto* item : transparent){
                drawInstance(*item,{}, m_linearOutput);
            }
        }
    }
    GLState::depthMask(GL_TRUE);
    GLState::disable(GL_BLEND);
    m_stats.submitCpuMs =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - submit)
            .count();
}

void RenderVisitor::drawOpaque(const std::vector<const RenderNode*>& items,
                               const std::shared_ptr<Program>& program, bool linearOutput){
    if (!m_instancing){
        for (const auto* item : items){
            drawInstance(*item, program, linearOutput);
        }
        return;
    }

    struct Candidate{
        const RenderNode* item;
        std::size_t state;
    };

    std::vector<Candidate> sorted;
    sorted.reserve(items.size());
    for (const auto* item : items){
        sorted.push_back({item, item->state.renderKey()});
    }
    const auto key = [](const Candidate& entry){
        return std::tuple<std::uintptr_t, std::uintptr_t, std::size_t, const std::string&>{
            reinterpret_cast<std::uintptr_t>(entry.item->state.getProgram().get()),
            reinterpret_cast<std::uintptr_t>(entry.item->geometry), entry.state, entry.item->room};
    };
    std::stable_sort(sorted.begin(), sorted.end(),
                     [&](const auto& a, const auto& b){ return key(a) < key(b); });
    std::vector<glm::mat4> models;
    for (std::size_t first = 0; first < sorted.size();){
        const auto& item = *sorted[first].item;
        std::size_t end = first + 1;
        const auto glass = item.state.getUniforms().find("uGlass");
        const bool refractive =
            glass != item.state.getUniforms().end() && std::get<bool>(glass->second);
        const auto effectiveProgram = program ? program : item.state.getProgram();
        if (!refractive && effectiveProgram && effectiveProgram->supportsInstancing()){
            while (end < sorted.size() && key(sorted[end]) == key(sorted[first]) &&
                   item.state.sameRenderState(sorted[end].item->state)){
                ++end;
            }
        }
        if (end - first == 1){
            drawInstance(item, program, linearOutput);
        } else{
            models.clear();
            models.reserve(end - first);
            for (auto i = first; i < end; ++i){
                const auto& model = sorted[i].item->model;
                item.geometry->getSurfaceTransform(model);
                models.push_back(model);
            }
            drawGeometry(item, program, linearOutput, &models);
            if (!item.room.empty()){
                ++m_stats.roomDraws[item.room];
            }
        }
        first = end;
    }
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

// Resolves color into a separate texture, including multisample and reflected views.
void RenderVisitor::captureTransmission(){
    FramebufferState saved;
    GLint source = 0;
    GLState::getIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &source);
    const int width = static_cast<int>(m_viewport.z), height = static_cast<int>(m_viewport.w);
    if (!m_transmission || m_transmission->getTexture()->getWidth() != width ||
        m_transmission->getTexture()->getHeight() != height){
        m_transmission = std::make_unique<ColorTarget>(width, height);
    }
    m_transmission->bind();
    GLState::bindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(source));
    GLState::disable(GL_SCISSOR_TEST);
    glBlitFramebuffer(static_cast<int>(m_viewport.x), static_cast<int>(m_viewport.y),
                      static_cast<int>(m_viewport.x) + width,
                      static_cast<int>(m_viewport.y) + height, 0, 0, width, height,
                      GL_COLOR_BUFFER_BIT, GL_NEAREST);
    ++m_stats.screenCopies;
}

// Submits a mesh with its resolved transform and rendering state.
void RenderVisitor::drawGeometry(const RenderNode& item,
                                 const std::shared_ptr<Program>& overrideProgram, bool linearOutput,
                                 const std::vector<glm::mat4>* models){
    auto& node = *item.geometry;
    const auto& model = item.model;
    auto state = item.state;
    const auto glassUniform = state.getUniforms().find("uGlass");
    const bool glass =
        glassUniform != state.getUniforms().end() && std::get<bool>(glassUniform->second);
    if (glass){
        captureTransmission();
    }
    if (overrideProgram){
        state.setProgram(overrideProgram);
    }
    state.apply();
    const auto& program = *state.getProgram();
    program.set("uInstanced", models != nullptr);
    if (m_environmentPrograms.insert(program.getId()).second){
        if (m_environment){
            m_environment->apply(program);
        }
        program.set("uEnvironment", 14);
        program.set("uEnvironmentBrdf", 15);
        program.set("uViewport", glm::vec2(m_viewport.z, m_viewport.w));
        program.set("uViewportOrigin", glm::vec2(m_viewport));
        program.set("uView", m_camera->getView());
        program.set("uProjection", m_camera->getProjection());
        program.set("uEye", m_camera->getPosition());
        program.set("uLightCount", static_cast<int>(m_lights.size()));
        for (unsigned i = 0; i < m_lights.size(); ++i){
            // Per-object light switches are already represented by uLightMask.
            m_lights[i].node->apply(program, i, m_lights[i].model, true);
        }
    }
    program.set("uEnvironmentEnabled", m_environment != nullptr);
    if (m_environmentCapture){
        program.set("uReflectionMode", static_cast<int>(ReflectionMode::Environment));
    }
    const auto mirror = state.getUniforms().find("uMirror");
    const bool mirrorSurface =
        mirror != state.getUniforms().end() && std::get<bool>(mirror->second);
    const auto reflection = mirrorSurface && m_reflection ? m_reflection->getTexture() : nullptr;
    program.set("uMirrorAvailable", reflection != nullptr);
    if (reflection){
        reflection->apply(3);
    }
    if (glass){
        m_transmission->getTexture()->apply(3);
        const auto& bound = node.getBound();
        const auto halfSize = bound.getExtent();
        const glm::vec3 center(model * glm::vec4(bound.getCenter(), 1));
        const float radius = std::max({glm::length(glm::vec3(model[0])) * halfSize.x,
                                       glm::length(glm::vec3(model[1])) * halfSize.y,
                                       glm::length(glm::vec3(model[2])) * halfSize.z});
        program.set("uGlassSphere", glm::vec4(center, radius));
    }
    program.set("uClipEnabled", m_clipPlane.has_value());
    program.set("uClipPlane", m_clipPlane.value_or(glm::vec4(0)));
    program.set("uLinearOutput", linearOutput);
    const auto coverage = state.getUniforms().find("uLodRange");
    program.set("uLodRange", coverage == state.getUniforms().end()
                                 ? glm::vec2(0, 1)
                                 : std::get<glm::vec2>(coverage->second));
    int lightMask = 0;
    for (unsigned i = 0; i < State::MaxLights; ++i){
        if (state.getLightEnabled(i)){
            lightMask |= 1 << i;
        }
    }
    program.set("uLightMask", lightMask);
    program.set("uToon", m_shadingMode == ShadingMode::Toon);
    program.set("uNormalMapping", m_shadingMode != ShadingMode::UnmappedPhong);
    program.set("uBrdfMode", m_shadingMode == ShadingMode::CookTorrance ? 2
                             : m_shadingMode == ShadingMode::BlinnPhong ? 1
                                                                        : 0);
    program.set("uModel", model);
    const auto basis = node.getSurfaceTransform(model);
    program.set("uNormal", basis.normal);
    program.set("uTangentSign", basis.tangentSign);
    if (models){
        node.drawInstanced(*models, true);
        ++m_stats.instancedDraws;
    } else{
        node.draw();
    }
    ++m_stats.draws;
    const auto count = models ? models->size() : 1;
    m_stats.instances += static_cast<unsigned>(count);
    m_stats.triangles += node.getTriangleCount() * count;
}

} // namespace vr
