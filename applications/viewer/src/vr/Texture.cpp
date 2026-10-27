#include <vr/Texture.h>
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace vr{
// Copies decoded image pixels before releasing the decoder allocation.
std::shared_ptr<Texture> Texture::decode(unsigned char* data, int width, int height){
    if (!data){
        throw std::runtime_error(std::string("Image decoding failed: ") + stbi_failure_reason());
    }
    std::vector<unsigned char> pixels(data, data + static_cast<std::size_t>(width) * height * 4);
    stbi_image_free(data);
    return std::make_shared<Texture>(width, height, std::move(pixels));
}

// Validates and stores RGBA pixels for lazy OpenGL upload.
Texture::Texture(int width, int height, std::vector<unsigned char> rgba, bool repeat)
    : m_width(width), m_height(height), m_repeat(repeat), m_rgba(std::move(rgba)){
    if (width <= 0 || height <= 0 ||
        m_rgba.size() != static_cast<std::size_t>(width) * height * 4){
        throw std::invalid_argument("Texture dimensions do not match RGBA data");
    }
}

// Samples stored alpha with bilinear filtering and the texture's wrap mode, without GL calls.
float Texture::sampleAlpha(const glm::vec2& coordinates) const{
    if (m_rgba.empty() || !std::isfinite(coordinates.x) || !std::isfinite(coordinates.y)){
        return 1;
    }
    const auto coordinate = [&](float value){
        return m_repeat ? value - std::floor(value) : std::clamp(value, 0.0f, 1.0f);
    };
    const float x = coordinate(coordinates.x) * m_width - 0.5f,
                y = coordinate(coordinates.y) * m_height - 0.5f;
    const int left = static_cast<int>(std::floor(x)), bottom = static_cast<int>(std::floor(y));
    const auto texel = [&](int column, int row){
        column =
            m_repeat ? (column % m_width + m_width) % m_width : std::clamp(column, 0, m_width - 1);
        row = m_repeat ? (row % m_height + m_height) % m_height : std::clamp(row, 0, m_height - 1);
        return m_rgba[(static_cast<std::size_t>(row) * m_width + column) * 4 + 3] / 255.0f;
    };
    const float horizontal = x - left, vertical = y - bottom;
    const float low = texel(left, bottom) * (1 - horizontal) + texel(left + 1, bottom) * horizontal,
                high = texel(left, bottom + 1) * (1 - horizontal) +
                       texel(left + 1, bottom + 1) * horizontal;
    return low * (1 - vertical) + high * vertical;
}

// Releases the texture after its final owner is destroyed.
Texture::~Texture(){
    if (m_id){
        glDeleteTextures(1, &m_id);
    }
}

// Decodes an image file into an RGBA texture.
std::shared_ptr<Texture> Texture::fromFile(const std::filesystem::path& path){
    int width = 0;
    int height = 0;
    int channels = 0;
    stbi_set_flip_vertically_on_load(1);
    auto* data = stbi_load(path.string().c_str(), &width, &height, &channels, 4);
    if (!data){
        throw std::runtime_error("Cannot load texture " + path.string());
    }
    return decode(data, width, height);
}

// Decodes a compressed in-memory image into an RGBA texture.
std::shared_ptr<Texture> Texture::fromMemory(const unsigned char* bytes, int length){
    int width = 0;
    int height = 0;
    int channels = 0;
    stbi_set_flip_vertically_on_load(1);
    auto* data = stbi_load_from_memory(bytes, length, &width, &height, &channels, 4);
    return decode(data, width, height);
}

// Uploads pixels on first use and binds the requested texture layer.
void Texture::apply(unsigned layer) const{
    glActiveTexture(GL_TEXTURE0 + layer);
    if (m_renderFormat){
        if (!m_id){
            glGenTextures(1, &m_id);
            glBindTexture(GL_TEXTURE_2D, m_id);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                            m_linearTarget ? GL_LINEAR : GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER,
                            m_linearTarget ? GL_LINEAR : GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glTexImage2D(GL_TEXTURE_2D, 0, m_renderFormat, m_width, m_height, 0, GL_RGBA, GL_FLOAT,
                         nullptr);
        } else{
            glBindTexture(GL_TEXTURE_2D, m_id);
        }
        return;
    }
    if (m_depth){
        uploadDepth();
        return;
    }
    if (!m_id){
        glGenTextures(1, &m_id);
        glBindTexture(GL_TEXTURE_2D, m_id);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, m_repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, m_repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, m_width, m_height, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                     m_rgba.data());
        glGenerateMipmap(GL_TEXTURE_2D);
    } else{
        glBindTexture(GL_TEXTURE_2D, m_id);
    }
}

// Validates the dimensions of a depth resource without allocating color pixels.
Texture::Texture(int width, int height, bool cube)
    : m_width(width), m_height(height), m_repeat(false), m_depth(true), m_cube(cube){
    if (width <= 0 || height <= 0 || (cube && width != height)){
        throw std::invalid_argument("Invalid depth texture dimensions");
    }
}

// Creates a lazy floating-point depth texture, with six faces for a cubemap.
std::shared_ptr<Texture> Texture::depth(int width, int height, bool cube){
    return std::shared_ptr<Texture>(new Texture(width, height, cube));
}

// Allocates the depth image or its six faces on first use.
void Texture::uploadDepth() const{
    const auto target = getTarget();
    if (!m_id){
        glGenTextures(1, &m_id);
        glBindTexture(target, m_id);
        glTexParameteri(target, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(target, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(target, GL_TEXTURE_WRAP_S, m_cube ? GL_CLAMP_TO_EDGE : GL_CLAMP_TO_BORDER);
        glTexParameteri(target, GL_TEXTURE_WRAP_T, m_cube ? GL_CLAMP_TO_EDGE : GL_CLAMP_TO_BORDER);
        if (m_cube){
            glTexParameteri(target, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
        }
        const float border[] ={1, 1, 1, 1};
        glTexParameterfv(target, GL_TEXTURE_BORDER_COLOR, border);
        glTexParameteri(target, GL_TEXTURE_COMPARE_MODE, GL_NONE);
        for (unsigned face = 0; face < (m_cube ? 6u : 1u); ++face){
            glTexImage2D(m_cube ? GL_TEXTURE_CUBE_MAP_POSITIVE_X + face : GL_TEXTURE_2D, 0,
                         GL_DEPTH_COMPONENT32F, m_width, m_height, 0, GL_DEPTH_COMPONENT, GL_FLOAT,
                         nullptr);
        }
    } else{
        glBindTexture(target, m_id);
    }
}
} // namespace vr

// Creates a nearest or linearly sampled color attachment without CPU pixel storage.
std::shared_ptr<vr::Texture> vr::Texture::colorTarget(int width, int height, GLenum format,
                                                      bool linear){
    if (format != GL_RGBA8 && format != GL_RGBA16F && format != GL_RGBA32F){
        throw std::invalid_argument("Unsupported color attachment format");
    }
    auto texture = std::shared_ptr<Texture>(new Texture(width, height, false));
    texture->m_depth = false;
    texture->m_renderFormat = format;
    texture->m_linearTarget = linear;
    return texture;
}
