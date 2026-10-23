#include <vr/Light.h>
#include <vr/Program.h>

namespace vr{
// Uploads the transformed light and its enabled state to the shader.
void Light::apply(const Program& program, unsigned index, const glm::mat4& model,
                  bool enabled) const{
    const auto prefix = "uLights[" + std::to_string(index) + "].";
    program.set(prefix + "position", model * m_position);
    program.set(prefix + "ambient", m_ambient);
    program.set(prefix + "diffuse", m_diffuse);
    program.set(prefix + "specular", m_specular);
    program.set(prefix + "attenuation", m_attenuation);
    program.set(prefix + "enabled", m_enabled && enabled);
}
} // namespace vr
