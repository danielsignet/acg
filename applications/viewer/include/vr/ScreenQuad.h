#pragma once
#include <glad/glad.h>

namespace vr{
class ScreenQuad final{
public:
    // Allocates the core-profile vertex array used by generated screen vertices.
    ScreenQuad();
    // Releases the owned vertex array.
    ~ScreenQuad();
    // Prevents copying vertex array ownership.
    ScreenQuad(const ScreenQuad&) = delete;
    // Prevents assigning vertex array ownership.
    ScreenQuad& operator=(const ScreenQuad&) = delete;
    // Draws one screen-aligned quad and restores the caller's vertex array.
    void draw() const;

private:
    GLuint m_vertexArray = 0;
};
} // namespace vr
