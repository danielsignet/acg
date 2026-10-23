#include <algorithm>
#include <cmath>
#include <fstream>
#include <glad/glad.h>
#include <stdexcept>
#include <utility>
#include <vr/FramebufferImage.h>
#include <zlib.h>

namespace vr{
// Stores a complete RGB image in OpenGL row order.
FramebufferImage::FramebufferImage(int width, int height, std::vector<unsigned char> pixels)
    : m_width(width), m_height(height), m_pixels(std::move(pixels)){
}

// Reads an RGB rectangle from the current OpenGL back buffer.
FramebufferImage FramebufferImage::capture(int x, int y, int width, int height){
    if (width <= 0 || height <= 0){
        throw std::invalid_argument("Framebuffer capture requires positive dimensions");
    }

    std::vector<unsigned char> pixels(static_cast<std::size_t>(width) * height * 3);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadBuffer(GL_BACK);
    glReadPixels(x, y, width, height, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
    return FramebufferImage(width, height, std::move(pixels));
}

// Returns a stable hash for exact image comparisons.
std::uint64_t FramebufferImage::hash() const{
    std::uint64_t result = 1469598103934665603ull;
    for (const auto value : m_pixels){
        result = (result ^ value) * 1099511628211ull;
    }
    return result;
}

// Counts pixels whose channels differ by more than the rounding tolerance.
std::size_t FramebufferImage::countChangedPixels(const FramebufferImage& other,
                                                 int tolerance) const{
    if (m_width != other.m_width || m_height != other.m_height){
        throw std::invalid_argument("Image dimensions must match");
    }
    std::size_t changed = 0;
    for (std::size_t offset = 0; offset < m_pixels.size(); offset += 3){
        for (std::size_t channel = 0; channel < 3; ++channel){
            if (std::abs(static_cast<int>(m_pixels[offset + channel]) -
                         static_cast<int>(other.m_pixels[offset + channel])) > tolerance){
                ++changed;
                break;
            }
        }
    }
    return changed;
}

// Flips the rows and prepends the PNG filter byte to each row.
std::vector<unsigned char> FramebufferImage::encodeRows() const{
    const auto stride = static_cast<std::size_t>(m_width) * 3;
    std::vector<unsigned char> rows((stride + 1) * m_height, 0);
    for (int row = 0; row < m_height; ++row){
        std::copy_n(m_pixels.data() + (m_height - 1 - row) * stride, stride,
                    rows.data() + row * (stride + 1) + 1);
    }
    return rows;
}

// Writes an unsigned integer using PNG's network byte order.
void FramebufferImage::writeInteger(std::ostream& output, unsigned value){
    for (int shift :{24, 16, 8, 0}){
        output.put(static_cast<char>(value >> shift));
    }
}

// Writes a PNG chunk with its length and CRC checksum.
void FramebufferImage::writeChunk(std::ostream& output, const char* type,
                                  const std::vector<unsigned char>& data){
    writeInteger(output, static_cast<unsigned>(data.size()));
    output.write(type, 4);
    auto checksum = crc32(0, reinterpret_cast<const Bytef*>(type), 4);
    if (!data.empty()){
        output.write(reinterpret_cast<const char*>(data.data()),
                     static_cast<std::streamsize>(data.size()));
        checksum = crc32(checksum, data.data(), static_cast<uInt>(data.size()));
    }
    writeInteger(output, static_cast<unsigned>(checksum));
}

// Writes a complete DOT document to the requested file.
void FramebufferImage::save(const std::filesystem::path& path) const{
    const auto rows = encodeRows();
    auto compressedSize = compressBound(static_cast<uLong>(rows.size()));
    std::vector<unsigned char> compressed(compressedSize);
    if (compress2(compressed.data(), &compressedSize, rows.data(), static_cast<uLong>(rows.size()),
                  Z_BEST_SPEED) != Z_OK){
        throw std::runtime_error("PNG compression failed");
    }
    compressed.resize(compressedSize);
    if (path.has_parent_path()){
        std::filesystem::create_directories(path.parent_path());
    }

    std::ofstream output(path, std::ios::binary);
    const unsigned char signature[] ={137, 80, 78, 71, 13, 10, 26, 10};
    output.write(reinterpret_cast<const char*>(signature), sizeof(signature));
    std::vector<unsigned char> header;
    for (unsigned value :{static_cast<unsigned>(m_width), static_cast<unsigned>(m_height)}){
        for (int shift :{24, 16, 8, 0}){
            header.push_back(static_cast<unsigned char>(value >> shift));
        }
    }
    header.insert(header.end(),{8, 2, 0, 0, 0});
    writeChunk(output, "IHDR", header);
    writeChunk(output, "IDAT", compressed);
    writeChunk(output, "IEND",{});
    output.close();
    if (!output){
        throw std::runtime_error("Cannot save screenshot: " + path.string());
    }
}
} // namespace vr
