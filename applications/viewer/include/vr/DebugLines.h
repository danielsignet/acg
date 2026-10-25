#pragma once
#include "BoundingBox.h"
#include "Program.h"
#include <memory>

namespace vr{
class DebugLines{
public:
    // Creates the debug shader and reusable line buffers.
    explicit DebugLines(const std::filesystem::path& root);
    // Releases the line buffers while the OpenGL context is alive.
    ~DebugLines();
    // Prevents copying ownership of OpenGL resources.
    DebugLines(const DebugLines&) = delete;
    // Prevents assigning ownership of OpenGL resources.
    DebugLines& operator=(const DebugLines&) = delete;
    // Draws a valid world-space bound from the observer camera.
    void box(const BoundingBox& box, const glm::mat4& vp, const glm::vec4& color);
    // Draws the frozen culling volume from the observer camera.
    void frustum(const glm::mat4& cullingVp, const glm::mat4& observerVp);

private:
    // Submits the twelve edges connecting the eight supplied corners.
    void draw(const std::array<glm::vec3, 8>& corners, const glm::mat4& vp, const glm::vec4& color);
    std::shared_ptr<Program> m_program;
    GLuint m_vao = 0, m_vbo = 0;
};
} // namespace vr
