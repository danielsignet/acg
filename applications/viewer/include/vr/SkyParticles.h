#pragma once
#include "Camera.h"
#include "Program.h"
#include <array>
#include <filesystem>

namespace vr{
// Immutable point-particle storage. Only seven formation matrices and time vary.
class SkyParticles final{
public:
    SkyParticles(const std::filesystem::path& file);
    ~SkyParticles();
    SkyParticles(const SkyParticles&)=delete;
    SkyParticles& operator=(const SkyParticles&)=delete;
    void draw(const Program& program, const Camera& camera, float time, float height) const;
    unsigned count() const{return m_count;}
    std::size_t bytes() const{return m_count*32;}
private:
    GLuint m_vao=0,m_buffer=0;
    unsigned m_count=0,m_groups=0;
    std::array<glm::mat4,8> m_formations{};
    std::array<float,8> m_speeds{};
    float m_maxPointSize=1;
};
}
