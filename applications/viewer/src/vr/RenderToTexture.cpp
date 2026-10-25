#include <vr/RenderToTexture.h>
#include <vr/FramebufferState.h>
#include <stdexcept>

namespace vr{

// Creates a complete depth framebuffer backed by one image or six cubemap faces.
RenderToTexture::RenderToTexture(int resolution, bool cube)
    : m_texture(Texture::depth(resolution, resolution, cube)), m_cameras(cube ? 6 : 1){
    FramebufferState saved;
    m_texture->apply(cube ? 5 : 4);
    glGenFramebuffers(1, &m_framebuffer);
    try{
        attach(0);
    } catch (...){
        glDeleteFramebuffers(1, &m_framebuffer);
        throw;
    }
}

// Releases the owned framebuffer before its OpenGL context is destroyed.
RenderToTexture::~RenderToTexture(){
    if (m_framebuffer){
        glDeleteFramebuffers(1, &m_framebuffer);
    }
}

// Attaches one depth face and checks framebuffer completeness.
void RenderToTexture::attach(unsigned layer) const{
    if (layer >= m_cameras.size()){
        throw std::out_of_range("Render target layer");
    }
    glBindFramebuffer(GL_FRAMEBUFFER, m_framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                           m_texture->getTarget() == GL_TEXTURE_CUBE_MAP
                               ? GL_TEXTURE_CUBE_MAP_POSITIVE_X + layer
                               : GL_TEXTURE_2D,
                           m_texture->getId(), 0);
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE){
        throw std::runtime_error("Incomplete depth framebuffer");
    }
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
    const auto& camera = m_cameras.at(layer);
    if (!camera){
        throw std::logic_error("Render target camera not assigned");
    }
    FramebufferState saved;
    attach(layer);
    glViewport(0, 0, m_texture->getWidth(), m_texture->getHeight());
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDepthMask(GL_TRUE);
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_POLYGON_OFFSET_FILL);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    glClearDepth(1);
    glClear(GL_DEPTH_BUFFER_BIT);
    draw(*camera);
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
