#include <vr/ColorTarget.h>
#include <vr/FramebufferState.h>
#include <stdexcept>

namespace vr{
// Creates a floating-point color target with optional shared opaque depth.
ColorTarget::ColorTarget(int width, int height, std::shared_ptr<Texture> depth)
    : m_color(
          Texture::colorTarget(width, height, GL_RGBA16F, true, depth ? depth->getSamples() : 1)),
      m_depth(std::move(depth)){
    FramebufferState saved;
    if (m_depth && (!m_depth->isDepth() || m_depth->getTarget() == GL_TEXTURE_CUBE_MAP ||
                    m_depth->getWidth() != width || m_depth->getHeight() != height)){
        throw std::invalid_argument("Color target depth dimensions or format");
    }
    glGenFramebuffers(1, &m_framebuffer);
    try{
        bind();
        m_color->apply(0);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, m_color->getTarget(),
                               m_color->getId(), 0);
        if (m_depth){
            m_depth->apply(0);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, m_depth->getTarget(),
                                   m_depth->getId(), 0);
        }
        glDrawBuffer(GL_COLOR_ATTACHMENT0);
        glReadBuffer(GL_COLOR_ATTACHMENT0);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE){
            throw std::runtime_error("Incomplete color framebuffer");
        }
    } catch (...){
        glDeleteFramebuffers(1, &m_framebuffer);
        throw;
    }
}

// Releases the owned framebuffer before its textures.
ColorTarget::~ColorTarget(){
    glDeleteFramebuffers(1, &m_framebuffer);
}

// Binds the color target and sets its pixel viewport.
void ColorTarget::bind() const{
    glBindFramebuffer(GL_FRAMEBUFFER, m_framebuffer);
    glViewport(0, 0, m_color->getWidth(), m_color->getHeight());
}

// Resolves same-sized multisample color, optionally including depth for forward overlays.
void ColorTarget::resolveTo(const ColorTarget& destination, bool depth) const{
    if (m_color->getSamples() <= 1 || destination.m_color->getSamples() != 1 ||
        m_color->getWidth() != destination.m_color->getWidth() ||
        m_color->getHeight() != destination.m_color->getHeight() ||
        (depth && (!m_depth || !destination.m_depth))){
        throw std::invalid_argument("Incompatible multisample resolve targets");
    }
    FramebufferState saved;
    glDisable(GL_SCISSOR_TEST);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, m_framebuffer);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, destination.m_framebuffer);
    glBlitFramebuffer(0, 0, m_color->getWidth(), m_color->getHeight(), 0, 0, m_color->getWidth(),
                      m_color->getHeight(), GL_COLOR_BUFFER_BIT | (depth ? GL_DEPTH_BUFFER_BIT : 0),
                      GL_NEAREST);
}
} // namespace vr
