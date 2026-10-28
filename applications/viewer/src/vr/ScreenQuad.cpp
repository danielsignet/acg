#include <vr/GLState.h>
#include <vr/ScreenQuad.h>

namespace vr{
// Allocates the core-profile vertex array used by generated screen vertices.
ScreenQuad::ScreenQuad(){
    glGenVertexArrays(1, &m_vertexArray);
}

// Releases the owned vertex array.
ScreenQuad::~ScreenQuad(){
    GLState::deleteVertexArrays(1, &m_vertexArray);
}

// Draws one screen-aligned quad and restores the caller's vertex array.
void ScreenQuad::draw() const{
    GLint previous = 0;
    GLState::getIntegerv(GL_VERTEX_ARRAY_BINDING, &previous);
    GLState::bindVertexArray(m_vertexArray);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    GLState::bindVertexArray(static_cast<GLuint>(previous));
}
} // namespace vr
