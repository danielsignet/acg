#include <ft2build.h>
#include <vr/GLState.h>
#include <vr/Text.h>
#include FT_FREETYPE_H
#include <stdexcept>

namespace vr{
// Creates the shader, font atlas and buffers used for overlay drawing.
Text::Text(const std::filesystem::path& root){

    m_program = std::make_shared<Program>(root / "shaders/text.vs", root / "shaders/text.fs");
    const auto atlas = loadFont(root);
    uploadAtlas(atlas);
    createBuffers();
}

// Releases the font texture and overlay buffers.
Text::~Text(){
    glDeleteBuffers(1, &m_vbo);
    GLState::deleteVertexArrays(1, &m_vao);
    glDeleteTextures(1, &m_texture);
}

// Configures the OpenGL state used by the text overlay.
void Text::begin(int width, int height){
    GLState::disable(GL_DEPTH_TEST);
    GLState::depthMask(GL_FALSE);
    GLState::disable(GL_CULL_FACE);
    GLState::enable(GL_BLEND);
    GLState::blendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    m_program->use();
    m_program->set("uScreen", glm::vec2(width, height));
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_texture);
    m_program->set("uAtlas", 0);
}

// Uploads and draws one batch of text or rectangle vertices.
void Text::vertices(const std::vector<glm::vec4>& data, const glm::vec4& color, bool solid){
    if (data.empty()){
        return;
    }
    m_program->set("uColor", color);
    m_program->set("uSolid", solid);
    GLState::bindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(data.size() * sizeof(glm::vec4)),
                 data.data(), GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(data.size()));
    GLState::bindVertexArray(0);
}

// Draws supported text glyphs at the requested screen position and size.
void Text::draw(const std::string& text, float x, float y, float size, const glm::vec4& color){
    const float scale = size / 42;
    std::vector<glm::vec4> data;
    data.reserve(text.size() * 6);
    for (unsigned char c : text){
        if (c >= 128){
            continue;
        }
        const auto& g = m_glyphs[c];
        const float left = x + g.bearing.x * scale;
        const float top = y + (42 - g.bearing.y) * scale;
        const float right = left + g.size.x * scale;
        const float bottom = top + g.size.y * scale;
        data.insert(data.end(),{{left, top, g.uv0.x, g.uv0.y},
                                 {right, top, g.uv1.x, g.uv0.y},
                                 {right, bottom, g.uv1.x, g.uv1.y},
                                 {left, top, g.uv0.x, g.uv0.y},
                                 {right, bottom, g.uv1.x, g.uv1.y},
                                 {left, bottom, g.uv0.x, g.uv1.y}});
        x += g.advance * scale;
    }
    vertices(data, color, false);
}

// Draws a solid overlay rectangle as two triangles.
void Text::rectangle(float x, float y, float w, float h, const glm::vec4& color){
    vertices({{x, y, 0, 0},
              {x + w, y, 0, 0},
              {x + w, y + h, 0, 0},
              {x, y, 0, 0},
              {x + w, y + h, 0, 0},
              {x, y + h, 0, 0}},
             color, true);
}

// Draws a group of equally colored rectangles in one submission.
void Text::rectangles(const std::vector<glm::vec4>& rectangles, const glm::vec4& color){
    std::vector<glm::vec4> data;
    data.reserve(rectangles.size() * 6);
    for (const auto& rect : rectangles){
        const float x = rect.x, y = rect.y, right = x + rect.z, bottom = y + rect.w;
        data.insert(data.end(),{{x, y, 0, 0},
                                 {right, y, 0, 0},
                                 {right, bottom, 0, 0},
                                 {x, y, 0, 0},
                                 {right, bottom, 0, 0},
                                 {x, bottom, 0, 0}});
    }
    vertices(data, color, true);
}

// Restores depth writing and disables overlay blending.
void Text::end(){
    GLState::disable(GL_BLEND);
    GLState::depthMask(GL_TRUE);
    GLState::enable(GL_DEPTH_TEST);
}

// Rasterizes the supported font glyphs into a texture atlas.
std::vector<unsigned char> Text::loadFont(const std::filesystem::path& root){
    FT_Library library = nullptr;
    FT_Face face = nullptr;
    if (FT_Init_FreeType(&library)){
        throw std::runtime_error("FreeType initialisation failed");
    }
    if (FT_New_Face(library, (root / "fonts/arial.ttf").string().c_str(), 0, &face)){
        FT_Done_FreeType(library);
        throw std::runtime_error("Cannot load fonts/arial.ttf");
    }
    FT_Set_Pixel_Sizes(face, 0, 42);
    const int size = 1024;
    std::vector<unsigned char> atlas(size * size, 0);
    int x = 2;
    int y = 2;
    int rowHeight = 0;
    for (unsigned c = 32; c < 127; ++c){
        if (FT_Load_Char(face, c, FT_LOAD_RENDER)){
            continue;
        }
        const auto& bitmap = face->glyph->bitmap;
        if (x + static_cast<int>(bitmap.width) + 2 >= size){
            x = 2;
            y += rowHeight + 2;
            rowHeight = 0;
        }
        auto& glyph = m_glyphs[c];
        glyph.size ={static_cast<int>(bitmap.width), static_cast<int>(bitmap.rows)};
        glyph.bearing ={face->glyph->bitmap_left, face->glyph->bitmap_top};
        glyph.advance = static_cast<float>(face->glyph->advance.x) / 64;
        glyph.uv0 ={static_cast<float>(x) / size, static_cast<float>(y) / size};
        glyph.uv1 ={static_cast<float>(x + bitmap.width) / size,
                     static_cast<float>(y + bitmap.rows) / size};
        for (int row = 0; row < bitmap.rows; ++row){
            for (int col = 0; col < bitmap.width; ++col){
                atlas[(y + row) * size + x + col] = bitmap.buffer[row * bitmap.pitch + col];
            }
        }
        x += bitmap.width + 2;
        rowHeight = std::max(rowHeight, static_cast<int>(bitmap.rows));
    }
    FT_Done_Face(face);
    FT_Done_FreeType(library);
    return atlas;
}

// Uploads the glyph atlas and configures its sampling parameters.
void Text::uploadAtlas(const std::vector<unsigned char>& atlas){
    glGenTextures(1, &m_texture);
    glBindTexture(GL_TEXTURE_2D, m_texture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, 1024, 1024, 0, GL_RED, GL_UNSIGNED_BYTE, atlas.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

// Creates the vertex array and streaming buffer used for overlay drawing.
void Text::createBuffers(){
    glGenVertexArrays(1, &m_vao);
    glGenBuffers(1, &m_vbo);
    GLState::bindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, sizeof(glm::vec4), nullptr);
    GLState::bindVertexArray(0);
}
} // namespace vr
