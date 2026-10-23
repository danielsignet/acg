#pragma once
#include <cstdint>
#include <filesystem>
#include <ostream>
#include <vector>

namespace vr{
class FramebufferImage final{
public:
    // Reads an RGB rectangle from the current OpenGL back buffer.
    static FramebufferImage capture(int x, int y, int width, int height);

    // Saves the captured pixels as a PNG with its top row first.
    void save(const std::filesystem::path& path) const;

    // Returns a stable hash for exact image comparisons.
    std::uint64_t hash() const;

    // Counts pixels whose channels differ by more than the rounding tolerance.
    std::size_t countChangedPixels(const FramebufferImage& other, int tolerance = 2) const;

private:
    // Stores a complete RGB image in OpenGL row order.
    FramebufferImage(int width, int height, std::vector<unsigned char> pixels);

    // Flips the rows and prepends the PNG filter byte to each row.
    std::vector<unsigned char> encodeRows() const;

    // Writes an unsigned integer using PNG's network byte order.
    static void writeInteger(std::ostream& output, unsigned value);

    // Writes a PNG chunk with its length and CRC checksum.
    static void writeChunk(std::ostream& output, const char* type,
                           const std::vector<unsigned char>& data);

    int m_width;
    int m_height;
    std::vector<unsigned char> m_pixels;
};
} // namespace vr
