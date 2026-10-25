#pragma once
#include "BoundingBox.h"
#include "Program.h"
#include <memory>

namespace vr{
class DebugLines{
public:
    explicit DebugLines(const std::filesystem::path& root);
    ~DebugLines();
    DebugLines(const DebugLines&) = delete;
    DebugLines& operator=(const DebugLines&) = delete;
    void box(const BoundingBox& box, const glm::mat4& vp, const glm::vec4& color);
    void frustum(const glm::mat4& cullingVp, const glm::mat4& observerVp);

private:
    void draw(const std::array<glm::vec3, 8>& corners, const glm::mat4& vp, const glm::vec4& color);
    std::shared_ptr<Program> m_program;
    GLuint m_vao = 0, m_vbo = 0;
};
} // namespace vr
