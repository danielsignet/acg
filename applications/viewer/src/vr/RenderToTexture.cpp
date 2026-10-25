#include <vr/RenderToTexture.h>
#include <stdexcept>

namespace vr{
namespace{
class RenderTargetState{
public:
    // Keeps restoration responsibility with a single guard instance.
    RenderTargetState(const RenderTargetState&) = delete;
    // Prevents assigning ownership of the saved caller state.
    RenderTargetState& operator=(const RenderTargetState&) = delete;

    // Captures framebuffer bindings and rasterization state before a depth pass.
    RenderTargetState(){
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &m_drawFramebuffer);
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &m_readFramebuffer);
        glGetIntegerv(GL_VIEWPORT, m_viewport);
        glGetIntegerv(GL_DEPTH_FUNC, &m_depthFunction);
        glGetIntegerv(GL_POLYGON_MODE, m_polygonMode);
        glGetBooleanv(GL_COLOR_WRITEMASK, m_colorMask);
        glGetBooleanv(GL_DEPTH_WRITEMASK, &m_depthMask);
        glGetDoublev(GL_DEPTH_CLEAR_VALUE, &m_clearDepth);
        m_depthTestEnabled = glIsEnabled(GL_DEPTH_TEST);
        m_blendEnabled = glIsEnabled(GL_BLEND);
        m_cullFaceEnabled = glIsEnabled(GL_CULL_FACE);
        m_scissorEnabled = glIsEnabled(GL_SCISSOR_TEST);
        m_polygonOffsetEnabled = glIsEnabled(GL_POLYGON_OFFSET_FILL);
        glGetFloatv(GL_POLYGON_OFFSET_FACTOR, &m_polygonOffsetFactor);
        glGetFloatv(GL_POLYGON_OFFSET_UNITS, &m_polygonOffsetUnits);
    }

    // Restores caller state during normal return and exception unwinding.
    ~RenderTargetState(){
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(m_drawFramebuffer));
        glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(m_readFramebuffer));
        glViewport(m_viewport[0], m_viewport[1], m_viewport[2], m_viewport[3]);
        glDepthMask(m_depthMask);
        glDepthFunc(static_cast<GLenum>(m_depthFunction));
        glClearDepth(m_clearDepth);
        glColorMask(m_colorMask[0], m_colorMask[1], m_colorMask[2], m_colorMask[3]);
        const auto restore = [](GLenum flag, bool enabled){
            if (enabled){
                glEnable(flag);
            } else{
                glDisable(flag);
            }
        };
        restore(GL_DEPTH_TEST, m_depthTestEnabled);
        restore(GL_BLEND, m_blendEnabled);
        restore(GL_CULL_FACE, m_cullFaceEnabled);
        restore(GL_SCISSOR_TEST, m_scissorEnabled);
        restore(GL_POLYGON_OFFSET_FILL, m_polygonOffsetEnabled);
        glPolygonOffset(m_polygonOffsetFactor, m_polygonOffsetUnits);
        glPolygonMode(GL_FRONT_AND_BACK, static_cast<GLenum>(m_polygonMode[0]));
    }

private:
    GLint m_drawFramebuffer = 0, m_readFramebuffer = 0, m_viewport[4]{}, m_depthFunction = 0,
          m_polygonMode[2]{};
    GLboolean m_colorMask[4]{}, m_depthMask = GL_TRUE;
    bool m_depthTestEnabled = false, m_blendEnabled = false, m_cullFaceEnabled = false,
         m_scissorEnabled = false, m_polygonOffsetEnabled = false;
    GLfloat m_polygonOffsetFactor = 0, m_polygonOffsetUnits = 0;
    GLdouble m_clearDepth = 1;
};
} // namespace

// Creates a complete depth framebuffer backed by one image or six cubemap faces.
RenderToTexture::RenderToTexture(int resolution, bool cube)
    : m_texture(Texture::depth(resolution, resolution, cube)), m_cameras(cube ? 6 : 1){
    RenderTargetState saved;
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
    RenderTargetState saved;
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
    RenderTargetState saved;
    attach(layer);
    std::vector<float> pixels(static_cast<std::size_t>(m_texture->getWidth()) *
                              m_texture->getHeight());
    glReadPixels(0, 0, m_texture->getWidth(), m_texture->getHeight(), GL_DEPTH_COMPONENT, GL_FLOAT,
                 pixels.data());
    return pixels;
}
} // namespace vr
