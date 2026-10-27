#include <vr/DeferredRenderer.h>
#include <cmath>
#include <vr/Camera.h>
#include <vr/FramebufferState.h>
#include <vr/Light.h>
#include <algorithm>
#include <stdexcept>

namespace vr{
// Loads the geometry, lighting, post-processing and buffer-preview programs.
DeferredRenderer::DeferredRenderer(const std::filesystem::path& root){
    m_occlusion = std::make_unique<AmbientOcclusion>(root);
    m_dof = std::make_unique<DepthOfField>(root);
    const auto shaders = root / "shaders";
    m_geometryProgram =
        std::make_shared<Program>(shaders / "phong-shading.vs", shaders / "gbuffer.fs");
    m_lightingProgram =
        std::make_shared<Program>(shaders / "screen-quad.vs", shaders / "deferred-lighting.fs");
    m_postProgram =
        std::make_shared<Program>(shaders / "screen-quad.vs", shaders / "post-process.fs");
    m_viewProgram =
        std::make_shared<Program>(shaders / "screen-quad.vs", shaders / "gbuffer-view.fs");
    GLint units = 0;
    glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &units);
    if (units < GBuffer::Count + State::MaxShadowMaps){
        throw std::runtime_error("Deferred lighting requires sixteen fragment texture units");
    }
}

// Replaces viewport-sized attachments only after all replacements are complete.
void DeferredRenderer::resize(int width, int height){
    if (width <= 0 || height <= 0 || (width == m_width && height == m_height)){
        return;
    }
    auto gbuffer = std::make_unique<GBuffer>(width, height);
    auto color = std::make_unique<ColorTarget>(width, height, gbuffer->getDepth());
    auto composite = std::make_unique<ColorTarget>(width, height);
    std::array<std::unique_ptr<ColorTarget>, 2> bloom;
    for (unsigned i = 0; i < 2; ++i){
        bloom[i] = std::make_unique<ColorTarget>(std::max(1, width / 2), std::max(1, height / 2));
    }
    m_occlusion->resize(width, height);
    m_dof->resize(width, height);
    m_gbuffer = std::move(gbuffer);
    m_color = std::move(color);
    m_composite = std::move(composite);
    m_bloomBlur = std::move(bloom);
    m_width = width;
    m_height = height;
}

// Configures a screen pass without stale geometry rasterization state.
void DeferredRenderer::screenPass(){
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_POLYGON_OFFSET_FILL);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
}

// Renders opaque surfaces, screen-space lighting, transparency and post effects.
void DeferredRenderer::render(const Camera& camera, const State& state,
                              const std::vector<PositionedLight>& lights,
                              const std::function<void(const std::shared_ptr<Program>&)>& opaque,
                              const std::function<void()>& transparent){
    if (!m_gbuffer){
        throw std::logic_error("Deferred viewport has not been initialized");
    }
    FramebufferState saved;
    GLint framebuffer = 0, viewport[4]{};
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &framebuffer);
    glGetIntegerv(GL_VIEWPORT, viewport);
    m_screenDraws = 0;
    {
        FrameProfiler::Scope pass(m_profiler, FrameProfiler::Stage::Geometry);
        m_gbuffer->begin();
        opaque(m_geometryProgram);
    }
    if (m_ambientOcclusion){
        FrameProfiler::Scope pass(m_profiler, FrameProfiler::Stage::Occlusion);
        m_occlusion->render(camera, *m_gbuffer);
        m_screenDraws += 4;
    }
    {
        FrameProfiler::Scope pass(m_profiler, FrameProfiler::Stage::Lighting);
        light(camera, state, lights);
    }
    {
        FrameProfiler::Scope pass(m_profiler, FrameProfiler::Stage::Transparency);
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);
        transparent();
    }
    screenPass();
    postProcess(camera);
    FrameProfiler::Scope presentPass(m_profiler, FrameProfiler::Stage::Present);
    present(camera, static_cast<GLuint>(framebuffer), viewport);
}

// Binds surface attachments and evaluates lights once per visible pixel.
void DeferredRenderer::light(const Camera& camera, const State& state,
                             const std::vector<PositionedLight>& lights){
    m_color->bind();
    auto lighting = state;
    lighting.setProgram(m_lightingProgram);
    lighting.apply();
    screenPass();
    state.applyShadowMaps(*m_lightingProgram, GBuffer::Count);
    const std::array<const char*, GBuffer::Count> names{
        {"uGPosition", "uGNormal", "uGAlbedo", "uGSpecular", "uGAmbient", "uGEmissive", "uGPlane"}};
    for (unsigned i = 0; i < GBuffer::Count; ++i){
        const auto& texture = i == GBuffer::Ambient ? getAmbientBuffer() : m_gbuffer->get(i);
        texture->apply(i);
        m_lightingProgram->set(names[i], static_cast<int>(i));
    }
    m_lightingProgram->set("uEye", camera.getPosition());
    m_lightingProgram->set("uLightCount", static_cast<int>(lights.size()));
    for (unsigned i = 0; i < lights.size(); ++i){
        lights[i].node->apply(*m_lightingProgram, i, lights[i].model, true);
    }
    drawQuad();
}

// Runs a separable Gaussian filter without sampling from its output attachment.
void DeferredRenderer::blur(const std::shared_ptr<Texture>& source,
                            std::array<std::unique_ptr<ColorTarget>, 2>& targets, unsigned passes){
    auto input = source;
    for (unsigned pass = 0; pass < passes; ++pass){
        const unsigned output = pass % 2;
        targets[output]->bind();
        input->apply(0);
        m_postProgram->set("uPass", output == 0 ? 2 : 3);
        drawQuad();
        input = targets[output]->getTexture();
    }
}

// Sets the positive camera-space hit depth; no target leaves the image sharp.
void DeferredRenderer::setFocusDistance(std::optional<float> distance){
    if (distance && (!std::isfinite(*distance) || *distance <= 0)){
        throw std::invalid_argument("Focus distance must be finite and positive");
    }
    m_focusDistance = distance;
}

// Extracts and filters intermediate images before composing the final color target.
void DeferredRenderer::postProcess(const Camera& camera){
    m_postProgram->use();
    m_postProgram->set("uColor", 0);
    m_postProgram->set("uDofFar", 1);
    m_postProgram->set("uBloom", 2);
    m_postProgram->set("uDepth", 3);
    m_postProgram->set("uPosition", 4);
    m_postProgram->set("uEmissive", 5);
    m_postProgram->set("uDofNear", 6);
    m_postProgram->set("uInverseProjection", glm::inverse(camera.getProjection()));
    m_postProgram->set("uEye", camera.getPosition());
    if (m_bloom){
        FrameProfiler::Scope pass(m_profiler, FrameProfiler::Stage::Bloom);
        m_bloomBlur[1]->bind();
        m_color->getTexture()->apply(0);
        m_postProgram->set("uPass", 1);
        drawQuad();
        blur(m_bloomBlur[1]->getTexture(), m_bloomBlur, 6);
    }
    if (m_depthOfField && m_focusDistance){
        FrameProfiler::Scope pass(m_profiler, FrameProfiler::Stage::DepthOfField);
        m_dof->render(camera, m_color->getTexture(), m_gbuffer->getDepth(), *m_focusDistance);
        m_screenDraws += DepthOfField::ScreenDraws;
        m_postProgram->use();
    }
    FrameProfiler::Scope pass(m_profiler, FrameProfiler::Stage::Composite);
    m_composite->bind();
    m_color->getTexture()->apply(0);
    m_dof->getFar()->apply(1);
    m_bloomBlur[1]->getTexture()->apply(2);
    m_gbuffer->getDepth()->apply(3);
    m_gbuffer->get(GBuffer::Position)->apply(4);
    m_gbuffer->get(GBuffer::Emissive)->apply(5);
    m_dof->getNear()->apply(6);
    m_postProgram->set("uPass", 0);
    m_postProgram->set("uBloomEnabled", m_bloom);
    m_postProgram->set("uDofEnabled", m_depthOfField && m_focusDistance.has_value());
    m_postProgram->set("uFocusDistance", m_focusDistance.value_or(1.0f));
    drawQuad();
}

// Presents the selected output and optionally the surface and AO thumbnails.
void DeferredRenderer::present(const Camera& camera, GLuint framebuffer, const GLint* viewport){
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, framebuffer);
    glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
    m_viewProgram->use();
    m_composite->getTexture()->apply(0);
    m_gbuffer->get(GBuffer::Position)->apply(1);
    m_gbuffer->get(GBuffer::Normal)->apply(2);
    m_gbuffer->get(GBuffer::Albedo)->apply(3);
    m_gbuffer->get(GBuffer::Specular)->apply(4);
    m_gbuffer->getDepth()->apply(5);
    getAmbientBuffer()->apply(6);
    m_gbuffer->get(GBuffer::Emissive)->apply(7);
    m_gbuffer->get(GBuffer::Plane)->apply(8);
    const std::array<const char*, 9> names{{"uColor", "uPosition", "uNormal", "uAlbedo",
                                            "uSpecular", "uDepth", "uAmbient", "uEmissive",
                                            "uPlane"}};
    for (unsigned i = 0; i < names.size(); ++i){
        m_viewProgram->set(names[i], static_cast<int>(i));
    }
    m_viewProgram->set("uInverseProjection", glm::inverse(camera.getProjection()));
    m_viewProgram->set("uViewMode", static_cast<int>(m_view));
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_ALWAYS);
    glDepthMask(GL_TRUE);
    drawQuad();
    if (m_previews){
        glDisable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);
        for (unsigned i = 0; i < PreviewCount; ++i){
            const auto rect = previewRectangle(i);
            glViewport(viewport[0] + rect.x, viewport[1] + m_height - rect.y - rect.w, rect.z,
                       rect.w);
            m_viewProgram->set("uViewMode", static_cast<int>(i + 1));
            drawQuad();
        }
    }
}

// Draws and counts one screen-aligned quad.
void DeferredRenderer::drawQuad(){
    m_quad.draw();
    ++m_screenDraws;
}

// Returns a thumbnail rectangle in top-left framebuffer coordinates.
glm::ivec4 DeferredRenderer::previewRectangle(unsigned index) const{
    if (index >= PreviewCount){
        throw std::out_of_range("G-buffer preview index");
    }
    const int width = std::max(1, (m_width - 12 * static_cast<int>(PreviewCount + 1)) /
                                      static_cast<int>(PreviewCount));
    const int height = std::min(132, std::max(1, width * m_height / std::max(1, m_width)));
    return{12 + static_cast<int>(index) * (width + 12), m_height - 110 - height, width, height};
}

// Returns the latest geometry buffers for numerical diagnostics.
const GBuffer& DeferredRenderer::getGBuffer() const{
    if (!m_gbuffer){
        throw std::logic_error("G-buffer has not been initialized");
    }
    return *m_gbuffer;
}

// Returns ambient RGB with AO in alpha, reusing the existing lighting sampler.
const std::shared_ptr<Texture>& DeferredRenderer::getAmbientBuffer() const{
    return m_ambientOcclusion ? m_occlusion->getTexture() : getGBuffer().get(GBuffer::Ambient);
}
} // namespace vr
