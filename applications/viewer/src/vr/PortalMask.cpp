#include <vr/PortalMask.h>

namespace vr{
// Creates the texture buffer used to upload portal planes without uniform-array limits.
PortalMask::PortalMask(){
    glGetIntegerv(GL_MAX_TEXTURE_BUFFER_SIZE, &m_capacity);
    glGenBuffers(1, &m_buffer);
    glGenTextures(1, &m_texture);
}

// Releases the owned OpenGL buffer and texture before context destruction.
PortalMask::~PortalMask(){
    glDeleteTextures(1, &m_texture);
    glDeleteBuffers(1, &m_buffer);
}

// Uploads active visibility volumes, retaining all surfaces during conservative fallback.
void PortalMask::update(const PortalResult* portals){
    m_ranges.clear();
    if (!portals || !portals->active){
        return;
    }
    std::vector<glm::vec4> data;
    for (const auto& entry : portals->volumes){
        const auto first = data.size();
        for (const auto& volume : entry.second){
            data.emplace_back(static_cast<float>(volume.planes.size()), 0, 0, 0);
            data.insert(data.end(), volume.planes.begin(), volume.planes.end());
        }
        if (data.size() > static_cast<std::size_t>(m_capacity)){
            m_ranges.clear();
            return;
        }
        m_ranges.emplace(entry.first, glm::ivec2(static_cast<int>(first),
                                                 static_cast<int>(entry.second.size())));
    }
    if (data.empty()){
        return;
    }
    glBindBuffer(GL_TEXTURE_BUFFER, m_buffer);
    glBufferData(GL_TEXTURE_BUFFER, static_cast<GLsizeiptr>(data.size() * sizeof(glm::vec4)),
                 data.data(), GL_DYNAMIC_DRAW);
    glActiveTexture(GL_TEXTURE6);
    glBindTexture(GL_TEXTURE_BUFFER, m_texture);
    glTexBuffer(GL_TEXTURE_BUFFER, GL_RGBA32F, m_buffer);
}

// Selects the room's volume range for one color draw, leaving physical shadows untouched.
void PortalMask::apply(const Program& program, const std::string& room) const{
    const auto found = m_ranges.find(room);
    const auto range = found == m_ranges.end() ? glm::ivec2(0) : found->second;
    program.set("uPortalPlanes", 6);
    program.set("uPortalFirst", range.x);
    program.set("uPortalVolumeCount", range.y);
    if (range.y){
        glActiveTexture(GL_TEXTURE6);
        glBindTexture(GL_TEXTURE_BUFFER, m_texture);
    }
}
} // namespace vr
