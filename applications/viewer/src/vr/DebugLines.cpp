#include <vector>
#include <vr/DebugLines.h>
#include <vr/Frustum.h>

namespace vr{
// Creates the debug shader and reusable line buffers.
DebugLines::DebugLines(const std::filesystem::path& root){
    m_program = std::make_shared<Program>(root / "shaders/debug.vs", root / "shaders/debug.fs");
    glGenVertexArrays(1, &m_vao);
    glGenBuffers(1, &m_vbo);
    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), nullptr);
    glBindVertexArray(0);
}

// Releases the line buffers while the OpenGL context is alive.
DebugLines::~DebugLines(){
    glDeleteBuffers(1, &m_vbo);
    glDeleteVertexArrays(1, &m_vao);
}

// Submits the twelve edges connecting the eight supplied corners.
void DebugLines::draw(const std::array<glm::vec3, 8>& corners, const glm::mat4& vp,
                      const glm::vec4& color){
    std::vector<glm::vec3> vertices;
    for (unsigned i = 0; i < 8; ++i){
        for (unsigned bit :{1u, 2u, 4u}){
            if (!(i & bit)){
                vertices.push_back(corners[i]);
                vertices.push_back(corners[i | bit]);
            }
        }
    }
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    m_program->use();
    m_program->set("uVP", vp);
    m_program->set("uColor", color);
    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertices.size() * sizeof(glm::vec3)),
                 vertices.data(), GL_STREAM_DRAW);
    glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(vertices.size()));
    glBindVertexArray(0);
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
}

// Draws a valid world-space bound from the observer camera.
void DebugLines::box(const BoundingBox& box, const glm::mat4& vp, const glm::vec4& color){
    if (box.valid()){
        draw(box.corners(), vp, color);
    }
}

// Draws the frozen culling volume from the observer camera.
void DebugLines::frustum(const glm::mat4& cullingVp, const glm::mat4& observerVp){
    draw(Frustum::corners(cullingVp), observerVp,{1, 0.4f, 0.9f, 1});
}
} // namespace vr
