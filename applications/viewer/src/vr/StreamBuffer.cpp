#include <algorithm>
#include <limits>
#include <stdexcept>
#include <vr/StreamBuffer.h>

namespace vr{
StreamBuffer::~StreamBuffer(){
    for (auto& slot : m_slots){
        if (slot.fence){
            glDeleteSync(slot.fence);
        }
        if (slot.buffer){
            glDeleteBuffers(1, &slot.buffer);
        }
    }
}

GLuint StreamBuffer::acquire(std::size_t bytes, const void* data){
    if (bytes > static_cast<std::size_t>(std::numeric_limits<GLsizeiptr>::max())){
        throw std::length_error("Streaming buffer is too large");
    }
    m_current = (m_current + 1) % m_slots.size();
    auto& slot = m_slots[m_current];
    const bool discard = m_policy == Policy::Discard;
    bool busy = false;
    if (slot.fence){
        const auto status = glClientWaitSync(slot.fence, 0, 0);
        if (status == GL_WAIT_FAILED){
            throw std::runtime_error("Streaming buffer fence failed");
        }
        busy = status != GL_ALREADY_SIGNALED && status != GL_CONDITION_SATISFIED;
        glDeleteSync(slot.fence);
        slot.fence = nullptr;
    }
    if (!slot.buffer){
        glGenBuffers(1, &slot.buffer);
    }
    glBindBuffer(GL_ARRAY_BUFFER, slot.buffer);
    if (discard || busy || bytes > slot.capacity){
        m_orphans += slot.capacity > 0 && (discard || busy);
        slot.capacity = std::max(bytes, slot.capacity);
        // Queued draws retain the previous store. Writing the replacement
        // therefore requires no explicit fence query or wait.
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(slot.capacity), nullptr,
                     GL_STREAM_DRAW);
    }
    if (data && bytes){
        glBufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(bytes), data);
        m_uploadedBytes += bytes;
    }
    return slot.buffer;
}

void StreamBuffer::seal(){
    if (m_policy == Policy::Discard){
        return;
    }
    auto& slot = m_slots[m_current];
    if (!slot.buffer){
        return;
    }
    if (slot.fence){
        glDeleteSync(slot.fence);
    }
    slot.fence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
}
} // namespace vr
