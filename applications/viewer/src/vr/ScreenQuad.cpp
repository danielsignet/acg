#include <vr/ScreenQuad.h>

namespace vr{
// Allocates the core-profile vertex array used by generated screen vertices.
ScreenQuad::ScreenQuad(){
    glGenVertexArrays(1, &m_vertexArray);
}

// Releases the owned vertex array.
ScreenQuad::~ScreenQuad(){
    glDeleteVertexArrays(1, &m_vertexArray);
}

// Draws one screen-aligned quad and restores the caller's vertex array.
void ScreenQuad::draw() const{
    GLint previous = 0;
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &previous);
    glBindVertexArray(m_vertexArray);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glBindVertexArray(static_cast<GLuint>(previous));
}
} // namespace vr
