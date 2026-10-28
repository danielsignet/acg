#pragma once
#include <filesystem>
#include <glad/glad.h>
#include <glm/vec2.hpp>
#include <memory>
#include <vector>

namespace vr{
class Texture{
public:
    // Validates and stores RGBA pixels for lazy OpenGL upload.
    Texture(int width, int height, std::vector<unsigned char> rgba, bool repeat = true);

    // Releases the texture after its final owner is destroyed.
    ~Texture();

    // Prevents copying ownership or identity of this object.
    Texture(const Texture&) = delete;

    // Prevents copying ownership or identity of this object.
    Texture& operator=(const Texture&) = delete;

    // Decodes an image file into an RGBA texture.
    static std::shared_ptr<Texture> fromFile(const std::filesystem::path& path);

    // Decodes a compressed in-memory image into an RGBA texture.
    static std::shared_ptr<Texture> fromMemory(const unsigned char* data, int length);

    // Creates a lazy floating-point depth texture, with six faces for a cubemap.
    static std::shared_ptr<Texture> depth(int width, int height, bool cube = false,
                                          int samples = 1);

    // Creates a nearest or linearly sampled color attachment without CPU pixel storage.
    static std::shared_ptr<Texture> colorTarget(int width, int height, GLenum format = GL_RGBA16F,
                                                bool linear = false, int samples = 1);
    // Creates a floating-point cubemap with an explicitly allocated mip chain.
    static std::shared_ptr<Texture> colorCube(int size, unsigned levels);
    // Packs multiple HDR reflection cubemaps into one sampler binding.
    static std::shared_ptr<Texture> colorCubeArray(int size, unsigned levels, unsigned layers);

    // Returns the OpenGL texture identifier after upload.
    GLuint getId() const{
        return m_id;
    }

    // Returns the sampler target for a color, depth or cubemap texture.
    GLenum getTarget() const{
        return m_samples > 1  ? GL_TEXTURE_2D_MULTISAMPLE
               : m_cubeLayers ? GL_TEXTURE_CUBE_MAP_ARRAY
               : m_cube       ? GL_TEXTURE_CUBE_MAP
                              : GL_TEXTURE_2D;
    }

    // Returns the number of coverage samples stored per pixel.
    int getSamples() const{
        return m_samples;
    }

    // Reports whether this resource stores depth rather than RGBA color.
    bool isDepth() const{
        return m_depth;
    }

    // Uploads pixels on first use and binds the requested texture layer.
    void apply(unsigned layer) const;

    // Samples stored alpha with bilinear filtering and the texture's wrap mode, without GL calls.
    float sampleAlpha(const glm::vec2& coordinates) const;

    // Returns the image width in pixels.
    int getWidth() const{
        return m_width;
    }

    // Returns the image height in pixels.
    int getHeight() const{
        return m_height;
    }

private:
    // Validates the dimensions of a depth resource without allocating color pixels.
    Texture(int width, int height, bool cube);

    // Allocates the depth image or its six faces on first use.
    void uploadDepth() const;

    // Copies decoded image pixels before releasing the decoder allocation.
    static std::shared_ptr<Texture> decode(unsigned char* data, int width, int height);
    int m_width;
    int m_height;
    bool m_repeat;
    bool m_depth = false;
    bool m_cube = false;
    GLenum m_renderFormat = 0;
    bool m_linearTarget = false;
    int m_samples = 1;
    unsigned m_levels = 1;
    unsigned m_cubeLayers = 0;
    std::vector<unsigned char> m_rgba;
    mutable GLuint m_id = 0;
};
} // namespace vr
