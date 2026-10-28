#include <vr/FramebufferState.h>

namespace vr{
// Captures framebuffer bindings and rasterization state before a render pass.
FramebufferState::FramebufferState(){
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &m_drawFramebuffer);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &m_readFramebuffer);
    glGetIntegerv(GL_VIEWPORT, m_viewport);
    glGetIntegerv(GL_DEPTH_FUNC, &m_depthFunction);
    glGetIntegerv(GL_POLYGON_MODE, m_polygonMode);
    glGetIntegerv(GL_FRONT_FACE, &m_frontFace);
    glGetIntegerv(GL_BLEND_SRC_RGB, &m_blendSourceRgb);
    glGetIntegerv(GL_BLEND_DST_RGB, &m_blendDestinationRgb);
    glGetIntegerv(GL_BLEND_SRC_ALPHA, &m_blendSourceAlpha);
    glGetIntegerv(GL_BLEND_DST_ALPHA, &m_blendDestinationAlpha);
    glGetIntegerv(GL_BLEND_EQUATION_RGB, &m_blendEquationRgb);
    glGetIntegerv(GL_BLEND_EQUATION_ALPHA, &m_blendEquationAlpha);
    glGetBooleanv(GL_COLOR_WRITEMASK, m_colorMask);
    glGetBooleanv(GL_DEPTH_WRITEMASK, &m_depthMask);
    glGetDoublev(GL_DEPTH_CLEAR_VALUE, &m_clearDepth);
    m_depthTestEnabled = glIsEnabled(GL_DEPTH_TEST);
    m_blendEnabled = glIsEnabled(GL_BLEND);
    m_cullFaceEnabled = glIsEnabled(GL_CULL_FACE);
    m_scissorEnabled = glIsEnabled(GL_SCISSOR_TEST);
    m_polygonOffsetEnabled = glIsEnabled(GL_POLYGON_OFFSET_FILL);
    m_multisampleEnabled = glIsEnabled(GL_MULTISAMPLE);
    m_sampleShadingEnabled = glIsEnabled(GL_SAMPLE_SHADING);
    glGetFloatv(GL_MIN_SAMPLE_SHADING_VALUE, &m_minSampleShading);
    glGetFloatv(GL_POLYGON_OFFSET_FACTOR, &m_polygonOffsetFactor);
    glGetFloatv(GL_POLYGON_OFFSET_UNITS, &m_polygonOffsetUnits);
}

// Restores caller state during normal return and exception unwinding.
FramebufferState::~FramebufferState(){
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(m_drawFramebuffer));
    glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(m_readFramebuffer));
    glViewport(m_viewport[0], m_viewport[1], m_viewport[2], m_viewport[3]);
    glDepthMask(m_depthMask);
    glDepthFunc(static_cast<GLenum>(m_depthFunction));
    glClearDepth(m_clearDepth);
    glColorMask(m_colorMask[0], m_colorMask[1], m_colorMask[2], m_colorMask[3]);
    glFrontFace(static_cast<GLenum>(m_frontFace));
    glBlendFuncSeparate(
        static_cast<GLenum>(m_blendSourceRgb), static_cast<GLenum>(m_blendDestinationRgb),
        static_cast<GLenum>(m_blendSourceAlpha), static_cast<GLenum>(m_blendDestinationAlpha));
    glBlendEquationSeparate(static_cast<GLenum>(m_blendEquationRgb),
                            static_cast<GLenum>(m_blendEquationAlpha));
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
    restore(GL_MULTISAMPLE, m_multisampleEnabled);
    restore(GL_SAMPLE_SHADING, m_sampleShadingEnabled);
    glMinSampleShading(m_minSampleShading);
    glPolygonOffset(m_polygonOffsetFactor, m_polygonOffsetUnits);
    glPolygonMode(GL_FRONT_AND_BACK, static_cast<GLenum>(m_polygonMode[0]));
}
} // namespace vr
