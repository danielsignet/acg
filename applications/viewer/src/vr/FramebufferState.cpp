#include <vr/FramebufferState.h>
#include <vr/GLState.h>

namespace vr{
// Captures framebuffer bindings and rasterization state before a render pass.
FramebufferState::FramebufferState(){
    GLState::getIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &m_drawFramebuffer);
    GLState::getIntegerv(GL_READ_FRAMEBUFFER_BINDING, &m_readFramebuffer);
    GLState::getIntegerv(GL_VIEWPORT, m_viewport);
    GLState::getIntegerv(GL_DEPTH_FUNC, &m_depthFunction);
    GLState::getIntegerv(GL_POLYGON_MODE, m_polygonMode);
    GLState::getIntegerv(GL_FRONT_FACE, &m_frontFace);
    GLState::getIntegerv(GL_BLEND_SRC_RGB, &m_blendSourceRgb);
    GLState::getIntegerv(GL_BLEND_DST_RGB, &m_blendDestinationRgb);
    GLState::getIntegerv(GL_BLEND_SRC_ALPHA, &m_blendSourceAlpha);
    GLState::getIntegerv(GL_BLEND_DST_ALPHA, &m_blendDestinationAlpha);
    GLState::getIntegerv(GL_BLEND_EQUATION_RGB, &m_blendEquationRgb);
    GLState::getIntegerv(GL_BLEND_EQUATION_ALPHA, &m_blendEquationAlpha);
    GLState::getBooleanv(GL_COLOR_WRITEMASK, m_colorMask);
    GLState::getBooleanv(GL_DEPTH_WRITEMASK, &m_depthMask);
    GLState::getDoublev(GL_DEPTH_CLEAR_VALUE, &m_clearDepth);
    m_depthTestEnabled = GLState::isEnabled(GL_DEPTH_TEST);
    m_blendEnabled = GLState::isEnabled(GL_BLEND);
    m_cullFaceEnabled = GLState::isEnabled(GL_CULL_FACE);
    m_scissorEnabled = GLState::isEnabled(GL_SCISSOR_TEST);
    m_polygonOffsetEnabled = GLState::isEnabled(GL_POLYGON_OFFSET_FILL);
    m_multisampleEnabled = GLState::isEnabled(GL_MULTISAMPLE);
    m_sampleShadingEnabled = GLState::isEnabled(GL_SAMPLE_SHADING);
    GLState::getFloatv(GL_MIN_SAMPLE_SHADING_VALUE, &m_minSampleShading);
    GLState::getFloatv(GL_POLYGON_OFFSET_FACTOR, &m_polygonOffsetFactor);
    GLState::getFloatv(GL_POLYGON_OFFSET_UNITS, &m_polygonOffsetUnits);
}

// Restores caller state during normal return and exception unwinding.
FramebufferState::~FramebufferState(){
    GLState::bindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(m_drawFramebuffer));
    GLState::bindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(m_readFramebuffer));
    GLState::viewport(m_viewport[0], m_viewport[1], m_viewport[2], m_viewport[3]);
    GLState::depthMask(m_depthMask);
    GLState::depthFunc(static_cast<GLenum>(m_depthFunction));
    GLState::clearDepth(m_clearDepth);
    GLState::colorMask(m_colorMask[0], m_colorMask[1], m_colorMask[2], m_colorMask[3]);
    GLState::frontFace(static_cast<GLenum>(m_frontFace));
    GLState::blendFuncSeparate(
        static_cast<GLenum>(m_blendSourceRgb), static_cast<GLenum>(m_blendDestinationRgb),
        static_cast<GLenum>(m_blendSourceAlpha), static_cast<GLenum>(m_blendDestinationAlpha));
    GLState::blendEquationSeparate(static_cast<GLenum>(m_blendEquationRgb),
                                   static_cast<GLenum>(m_blendEquationAlpha));
    const auto restore = [](GLenum flag, bool enabled){
        if (enabled){
            GLState::enable(flag);
        } else{
            GLState::disable(flag);
        }
    };
    restore(GL_DEPTH_TEST, m_depthTestEnabled);
    restore(GL_BLEND, m_blendEnabled);
    restore(GL_CULL_FACE, m_cullFaceEnabled);
    restore(GL_SCISSOR_TEST, m_scissorEnabled);
    restore(GL_POLYGON_OFFSET_FILL, m_polygonOffsetEnabled);
    restore(GL_MULTISAMPLE, m_multisampleEnabled);
    restore(GL_SAMPLE_SHADING, m_sampleShadingEnabled);
    GLState::minSampleShading(m_minSampleShading);
    GLState::polygonOffset(m_polygonOffsetFactor, m_polygonOffsetUnits);
    GLState::polygonMode(GL_FRONT_AND_BACK, static_cast<GLenum>(m_polygonMode[0]));
}
} // namespace vr
