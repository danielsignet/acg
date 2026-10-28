#pragma once
#include <array>
#include <cstddef>
#include <glad/glad.h>

namespace vr{
// Three independently fenced stores; busy stores are orphaned rather than overwritten.
// Uses OpenGL 4.1 core operations and does not require persistent-mapping extensions.
class StreamBuffer final{
public:
    ~StreamBuffer();
    StreamBuffer() = default;
    StreamBuffer(const StreamBuffer&) = delete;
    StreamBuffer& operator=(const StreamBuffer&) = delete;
    GLuint acquire(std::size_t bytes, const void* data = nullptr);
    void seal();

    GLuint getBuffer() const{
        return m_slots[m_current].buffer;
    }

    std::size_t getUploadedBytes() const{
        return m_uploadedBytes;
    }

    std::size_t getOrphans() const{
        return m_orphans;
    }

private:
    struct Slot{
        GLuint buffer = 0;
        GLsync fence = nullptr;
        std::size_t capacity = 0;
    };

    std::array<Slot, 3> m_slots{};
    std::size_t m_current = 2, m_uploadedBytes = 0, m_orphans = 0;
};
} // namespace vr
