#pragma once
#include "PortalVisibility.h"
#include "Program.h"

namespace vr{
// Clips room surfaces to the same doorway volumes used by CPU instance culling.
class PortalMask final{
public:
    // Creates the texture buffer used to upload portal planes without uniform-array limits.
    PortalMask();

    // Releases the owned OpenGL buffer and texture before context destruction.
    ~PortalMask();

    // Prevents copying ownership of the OpenGL resources.
    PortalMask(const PortalMask&) = delete;

    // Prevents assigning ownership of the OpenGL resources.
    PortalMask& operator=(const PortalMask&) = delete;

    // Uploads active visibility volumes, retaining all surfaces during conservative fallback.
    void update(const PortalResult* portals);

    // Selects the room's volume range for one color draw, leaving physical shadows untouched.
    void apply(const Program& program, const std::string& room) const;

private:
    std::map<std::string, glm::ivec2> m_ranges;
    GLuint m_buffer = 0, m_texture = 0;
    GLint m_capacity = 0;
};
} // namespace vr
