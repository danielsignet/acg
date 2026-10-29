#pragma once
#include <array>
#include <cstddef>
#include <glad/glad.h>

namespace vr{
// Rotates three stores. Discard replaces storage without polling GPU fences;
// Reuse keeps signaled stores and orphans busy ones. Both use OpenGL 4.1 core.
class StreamBuffer final{
public:
    enum class Policy{ Reuse, Discard };
    ~StreamBuffer();
    StreamBuffer() = default;
    explicit StreamBuffer(Policy policy) : m_policy(policy){
    }
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
    Policy m_policy = Policy::Reuse;
};
} // namespace vr
