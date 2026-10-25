#pragma once
#include <glad/glad.h>

namespace vr{
class FramebufferState{
public:
    // Keeps restoration responsibility with a single guard instance.
    FramebufferState(const FramebufferState&) = delete;
    // Prevents assigning ownership of the saved caller state.
    FramebufferState& operator=(const FramebufferState&) = delete;

    // Captures framebuffer bindings and rasterization state before a render pass.
    FramebufferState();
    // Restores caller state during normal return and exception unwinding.
    ~FramebufferState();

private:
    GLint m_drawFramebuffer = 0, m_readFramebuffer = 0, m_viewport[4]{}, m_depthFunction = 0,
          m_polygonMode[2]{};
    GLboolean m_colorMask[4]{}, m_depthMask = GL_TRUE;
    bool m_depthTestEnabled = false, m_blendEnabled = false, m_cullFaceEnabled = false,
         m_scissorEnabled = false, m_polygonOffsetEnabled = false;
    GLfloat m_polygonOffsetFactor = 0, m_polygonOffsetUnits = 0;
    GLdouble m_clearDepth = 1;
};
} // namespace vr
