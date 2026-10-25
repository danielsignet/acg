#include <vr/Light.h>
#include <vr/Program.h>
#include <cmath>
#include <stdexcept>

namespace vr{
// Configures a normalized spotlight direction and its inner/outer cone angles.
void Light::setSpot(const glm::vec3& direction, float innerAngle, float outerAngle){
    if (!std::isfinite(glm::length(direction)) || glm::length(direction) < 0.0001f ||
        !(innerAngle > 0 && innerAngle < outerAngle && outerAngle < 89)){
        throw std::invalid_argument("Invalid spotlight cone");
    }
    m_spot = true;
    m_spotDirection = glm::normalize(direction);
    m_innerAngle = innerAngle;
    m_outerAngle = outerAngle;
}

// Sets a finite lighting range; zero leaves ordinary lights unbounded.
void Light::setRange(float distance){
    if (!std::isfinite(distance) || distance < 0){
        throw std::invalid_argument("Invalid light range");
    }
    m_range = distance;
}

// Uploads the transformed light and its enabled state to the shader.
void Light::apply(const Program& program, unsigned index, const glm::mat4& model,
                  bool enabled) const{
    const auto prefix = "uLights[" + std::to_string(index) + "].";
    program.set(prefix + "position", model * m_position);
    program.set(prefix + "ambient", m_ambient);
    program.set(prefix + "diffuse", m_diffuse);
    program.set(prefix + "specular", m_specular);
    program.set(prefix + "attenuation", m_attenuation);
    program.set(prefix + "spotlight", m_spot);
    program.set(prefix + "direction", glm::normalize(glm::mat3(model) * m_spotDirection));
    program.set(prefix + "innerCos", std::cos(glm::radians(m_innerAngle)));
    program.set(prefix + "outerCos", std::cos(glm::radians(m_outerAngle)));
    program.set(prefix + "range", m_range);
    program.set(prefix + "enabled", m_enabled && enabled);
}
} // namespace vr
