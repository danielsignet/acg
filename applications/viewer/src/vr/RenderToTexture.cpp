#include <stdexcept>
#include <vr/FramebufferState.h>
#include <vr/GLState.h>
#include <vr/RenderToTexture.h>

namespace vr{

// Creates a complete depth framebuffer backed by one image or six cubemap faces.
RenderToTexture::RenderToTexture(int resolution, bool cube)
    : m_framebuffers(cube ? 6 : 1), m_texture(Texture::depth(resolution, resolution, cube)),
      m_cameras(cube ? 6 : 1), m_contentRevisions(cube ? 6 : 1){
    FramebufferState saved;
    m_texture->apply(cube ? 5 : 4);
    glGenFramebuffers(static_cast<GLsizei>(m_framebuffers.size()), m_framebuffers.data());
    try{
        for (unsigned layer = 0; layer < m_framebuffers.size(); ++layer){
            attach(layer);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                                   cube ? GL_TEXTURE_CUBE_MAP_POSITIVE_X + layer : GL_TEXTURE_2D,
                                   m_texture->getId(), 0);
            glDrawBuffer(GL_NONE);
            glReadBuffer(GL_NONE);
            if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE){
                throw std::runtime_error("Incomplete depth framebuffer");
            }
        }
    } catch (...){
        GLState::deleteFramebuffers(static_cast<GLsizei>(m_framebuffers.size()),
                                    m_framebuffers.data());
        throw;
    }
}

// Releases the owned framebuffer before its OpenGL context is destroyed.
RenderToTexture::~RenderToTexture(){
    GLState::deleteFramebuffers(static_cast<GLsizei>(m_framebuffers.size()), m_framebuffers.data());
}

// Binding a fixed attachment avoids rebuilding and validating it in every shadow pass.
void RenderToTexture::attach(unsigned layer) const{
    GLState::bindFramebuffer(GL_FRAMEBUFFER, m_framebuffers.at(layer));
}

// Assigns one independent camera to the requested depth image.
void RenderToTexture::setCamera(unsigned layer, const std::shared_ptr<Camera>& camera){
    if (!camera){
        throw std::invalid_argument("Render target needs a camera");
    }
    m_cameras.at(layer) = camera;
}

// Renders a camera into its depth image and restores caller state on return or throw.
void RenderToTexture::render(unsigned layer, const std::function<void(const Camera&)>& draw){
    FramebufferState saved;
    renderFace(layer, draw);
}

void RenderToTexture::renderFace(unsigned layer, const std::function<void(const Camera&)>& draw){
    const auto& camera = m_cameras.at(layer);
    if (!camera){
        throw std::logic_error("Render target camera not assigned");
    }
    attach(layer);
    GLState::viewport(0, 0, m_texture->getWidth(), m_texture->getHeight());
    GLState::enable(GL_DEPTH_TEST);
    GLState::depthFunc(GL_LESS);
    GLState::depthMask(GL_TRUE);
    GLState::colorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    GLState::disable(GL_BLEND);
    GLState::disable(GL_CULL_FACE);
    GLState::disable(GL_SCISSOR_TEST);
    GLState::disable(GL_POLYGON_OFFSET_FILL);
    GLState::polygonMode(GL_FRONT_AND_BACK, GL_FILL);
    GLState::clearDepth(1);
    glClear(GL_DEPTH_BUFFER_BIT);
    ++m_contentRevisions.at(layer);
    draw(*camera);
}

void RenderToTexture::copyDepthFrom(const RenderToTexture& source, unsigned layer){
    GLState::bindFramebuffer(GL_READ_FRAMEBUFFER, source.m_framebuffers.at(0));
    GLState::bindFramebuffer(GL_DRAW_FRAMEBUFFER, m_framebuffers.at(layer));
    glBlitFramebuffer(0, 0, source.m_texture->getWidth(), source.m_texture->getHeight(), 0, 0,
                      m_texture->getWidth(), m_texture->getHeight(), GL_DEPTH_BUFFER_BIT,
                      GL_NEAREST);
}

// Reads an actual depth image for diagnostics without changing the caller target.
std::vector<float> RenderToTexture::readDepth(unsigned layer) const{
    FramebufferState saved;
    attach(layer);
    std::vector<float> pixels(static_cast<std::size_t>(m_texture->getWidth()) *
                              m_texture->getHeight());
    glReadPixels(0, 0, m_texture->getWidth(), m_texture->getHeight(), GL_DEPTH_COMPONENT, GL_FLOAT,
                 pixels.data());
    return pixels;
}
} // namespace vr
