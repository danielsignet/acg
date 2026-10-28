#include <vr/Antialiasing.h>
#include <vr/FramebufferState.h>
#include <vr/GLState.h>

namespace vr{
// Loads the display-space FXAA filter and the depth-preserving presentation pass.
Antialiasing::Antialiasing(const std::filesystem::path& root)
    : m_program(std::make_shared<Program>(root / "shaders/screen-quad.vs",
                                          root / "shaders/antialiasing.fs")){
}

// Discards viewport-sized resources after a dimension or technique change.
void Antialiasing::configure(int width, int height, AntialiasingMode mode){
    if (width == m_width && height == m_height && mode == m_mode){
        return;
    }
    m_filtered.reset();
    m_forward.reset();
    m_resolved.reset();
    m_width = width;
    m_height = height;
    m_mode = mode;
}

// Configures a fullscreen pass independently of previous geometry state.
void Antialiasing::screenPass(){
    GLState::disable(GL_DEPTH_TEST);
    GLState::depthMask(GL_FALSE);
    GLState::disable(GL_BLEND);
    GLState::disable(GL_CULL_FACE);
    GLState::disable(GL_SCISSOR_TEST);
    GLState::disable(GL_POLYGON_OFFSET_FILL);
    GLState::disable(GL_SAMPLE_SHADING);
    GLState::colorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    GLState::polygonMode(GL_FRONT_AND_BACK, GL_FILL);
}

// Filters the tone-mapped scene while leaving diagnostic channels and HUD untouched.
const std::shared_ptr<Texture>& Antialiasing::filter(const std::shared_ptr<Texture>& source){
    m_screenDraws = 0;
    if (m_mode != AntialiasingMode::Fxaa){
        return source;
    }
    if (!m_filtered){
        m_filtered = std::make_unique<ColorTarget>(m_width, m_height);
    }
    screenPass();
    m_filtered->bind();
    m_program->use();
    source->apply(0);
    m_program->set("uColor", 0);
    m_program->set("uFxaa", true);
    m_program->set("uWriteDepth", false);
    // Presentation uses a separate depth unit; filtering only reads the display color.
    m_program->set("uDepth", 1);
    m_quad.draw();
    ++m_screenDraws;
    return m_filtered->getTexture();
}

// Applies the same techniques to forward rendering, preserving depth for debug geometry.
void Antialiasing::renderForward(const std::function<void()>& draw, FrameProfiler* profiler){
    m_screenDraws = 0;
    FramebufferState saved;
    GLState::disable(GL_SAMPLE_SHADING);
    GLState::disable(GL_MULTISAMPLE);
    if (m_mode == AntialiasingMode::Off){
        draw();
        return;
    }
    GLint framebuffer = 0, viewport[4]{};
    GLState::getIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &framebuffer);
    GLState::getIntegerv(GL_VIEWPORT, viewport);
    if (!m_forward){
        const int samples = m_mode == AntialiasingMode::Msaa ? 4 : 1;
        m_forward = std::make_unique<ColorTarget>(
            m_width, m_height, Texture::depth(m_width, m_height, false, samples));
        if (samples > 1){
            m_resolved =
                std::make_unique<ColorTarget>(m_width, m_height, Texture::depth(m_width, m_height));
        }
    }
    m_forward->bind();
    if (m_mode == AntialiasingMode::Msaa){
        GLState::enable(GL_MULTISAMPLE);
    }
    GLState::depthMask(GL_TRUE);
    glClearColor(0.035f, 0.052f, 0.078f, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    draw();
    FrameProfiler::Scope pass(profiler, FrameProfiler::Stage::Antialiasing);
    if (m_resolved){
        m_forward->resolveTo(*m_resolved, true);
    }
    const auto& color = m_resolved ? m_resolved->getTexture() : m_forward->getTexture();
    const auto& output = filter(color);
    screenPass();
    GLState::bindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(framebuffer));
    GLState::viewport(viewport[0], viewport[1], viewport[2], viewport[3]);
    m_program->use();
    output->apply(0);
    (m_resolved ? m_resolved : m_forward)->getDepth()->apply(1);
    m_program->set("uColor", 0);
    m_program->set("uDepth", 1);
    m_program->set("uFxaa", false);
    m_program->set("uWriteDepth", true);
    GLState::enable(GL_DEPTH_TEST);
    GLState::depthFunc(GL_ALWAYS);
    GLState::depthMask(GL_TRUE);
    m_quad.draw();
    ++m_screenDraws;
}
} // namespace vr
