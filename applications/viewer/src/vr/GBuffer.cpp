#include <vr/GBuffer.h>
#include <vr/FramebufferState.h>
#include <stdexcept>

namespace vr{
// Allocates floating-point surface data and a shared camera depth texture.
GBuffer::GBuffer(int width, int height) : m_depth(Texture::depth(width, height)){
    FramebufferState saved;
    GLint attachments = 0, drawBuffers = 0;
    glGetIntegerv(GL_MAX_COLOR_ATTACHMENTS, &attachments);
    glGetIntegerv(GL_MAX_DRAW_BUFFERS, &drawBuffers);
    if (attachments < Count || drawBuffers < Count){
        throw std::runtime_error("Deferred shading requires seven color attachments");
    }
    glGenFramebuffers(1, &m_framebuffer);
    try{
        glBindFramebuffer(GL_FRAMEBUFFER, m_framebuffer);
        std::array<GLenum, Count> buffers{};
        for (unsigned i = 0; i < Count; ++i){
            const auto format = i == Position ? GL_RGBA32F : i == Albedo ? GL_RGBA8 : GL_RGBA16F;
            m_textures[i] = Texture::colorTarget(width, height, format);
            m_textures[i]->apply(0);
            buffers[i] = GL_COLOR_ATTACHMENT0 + i;
            glFramebufferTexture2D(GL_FRAMEBUFFER, buffers[i], GL_TEXTURE_2D,
                                   m_textures[i]->getId(), 0);
        }
        m_depth->apply(0);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, m_depth->getId(),
                               0);
        glDrawBuffers(Count, buffers.data());
        glReadBuffer(GL_COLOR_ATTACHMENT0);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE){
            throw std::runtime_error("Incomplete G-buffer");
        }
    } catch (...){
        glDeleteFramebuffers(1, &m_framebuffer);
        throw;
    }
}

// Releases the framebuffer before its attachment textures.
GBuffer::~GBuffer(){
    glDeleteFramebuffers(1, &m_framebuffer);
}

// Binds and clears every surface attachment for a new geometry pass.
void GBuffer::begin() const{
    glBindFramebuffer(GL_FRAMEBUFFER, m_framebuffer);
    glViewport(0, 0, m_depth->getWidth(), m_depth->getHeight());
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDepthMask(GL_TRUE);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDisable(GL_BLEND);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_POLYGON_OFFSET_FILL);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    const float clear[4] ={0, 0, 0, 0};
    for (unsigned i = 0; i < Count; ++i){
        glClearBufferfv(GL_COLOR, i, clear);
    }
    glClearDepth(1);
    glClear(GL_DEPTH_BUFFER_BIT);
}

// Returns a surface attachment for lighting, previews or diagnostics.
const std::shared_ptr<Texture>& GBuffer::get(unsigned attachment) const{
    return m_textures.at(attachment);
}

// Reads unencoded floating-point surface values from one attachment.
std::vector<float> GBuffer::read(unsigned attachment) const{
    get(attachment);
    FramebufferState saved;
    glBindFramebuffer(GL_READ_FRAMEBUFFER, m_framebuffer);
    glReadBuffer(GL_COLOR_ATTACHMENT0 + attachment);
    std::vector<float> data(static_cast<std::size_t>(m_depth->getWidth()) * m_depth->getHeight() *
                            4);
    glReadPixels(0, 0, m_depth->getWidth(), m_depth->getHeight(), GL_RGBA, GL_FLOAT, data.data());
    return data;
}
} // namespace vr
