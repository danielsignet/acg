#pragma once
#include <filesystem>
#include <glad/glad.h>
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

    // Uploads pixels on first use and binds the requested texture layer.
    void apply(unsigned layer) const;

    // Returns the image width in pixels.
    int getWidth() const{
        return m_width;
    }

    // Returns the image height in pixels.
    int getHeight() const{
        return m_height;
    }

private:
    // Copies decoded image pixels before releasing the decoder allocation.
    static std::shared_ptr<Texture> decode(unsigned char* data, int width, int height);
    int m_width;
    int m_height;
    bool m_repeat;
    std::vector<unsigned char> m_rgba;
    mutable GLuint m_id = 0;
};
} // namespace vr
