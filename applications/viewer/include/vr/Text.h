#pragma once
#include "Resources.h"

namespace vr{
class Text{
public:
    // Creates the shader, font atlas and buffers used for overlay drawing.
    explicit Text(const std::filesystem::path& root);

    // Releases the font texture and overlay buffers.
    ~Text();

    // Prevents copying ownership or identity of this object.
    Text(const Text&) = delete;

    // Prevents copying ownership or identity of this object.
    Text& operator=(const Text&) = delete;

    // Configures the OpenGL state used by the text overlay.
    void begin(int width, int height);

    // Draws supported text glyphs at the requested screen position and size.
    void draw(const std::string& text, float x, float y, float size, const glm::vec4& color);

    // Draws a solid overlay rectangle as two triangles.
    void rectangle(float x, float y, float width, float height, const glm::vec4& color);

    // Draws a group of equally colored rectangles in one submission.
    void rectangles(const std::vector<glm::vec4>& rectangles, const glm::vec4& color);

    // Restores depth writing and disables overlay blending.
    void end();

private:
    // Rasterizes the supported font glyphs into a texture atlas.
    std::vector<unsigned char> loadFont(const std::filesystem::path& root);

    // Uploads the glyph atlas and configures its sampling parameters.
    void uploadAtlas(const std::vector<unsigned char>& atlas);

    // Creates the vertex array and streaming buffer used for overlay drawing.
    void createBuffers();

    struct Glyph{
        glm::vec2 uv0;
        glm::vec2 uv1;
        glm::ivec2 size;
        glm::ivec2 bearing;
        float advance = 0;
    };

    std::array<Glyph, 128> m_glyphs;
    std::shared_ptr<Program> m_program;
    GLuint m_texture = 0;
    GLuint m_vao = 0;
    GLuint m_vbo = 0;

    // Uploads and draws one batch of text or rectangle vertices.
    void vertices(const std::vector<glm::vec4>& data, const glm::vec4& color, bool solid);
};
} // namespace vr
