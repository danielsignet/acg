#include <cmath>
#include <glm/gtc/matrix_transform.hpp>
#include <stdexcept>
#include <vr/SpinCallback.h>

namespace vr{
// Captures the base transform and a normalized local rotation axis.
SpinCallback::SpinCallback(const std::shared_ptr<Transform>& target, float speed,
                           const glm::vec3& axis)
    : m_target(target), m_baseMatrix(target ? target->getMatrix() : glm::mat4(1)), m_axis(axis),
      m_radiansPerSecond(glm::radians(speed)){
    if (!target || !std::isfinite(speed) || glm::length(axis) < 0.00001f){
        throw std::invalid_argument("Invalid spin animation");
    }
    m_axis = glm::normalize(axis);
}

// Rotates the local object only while object animation is enabled.
void SpinCallback::update(Node& node, const UpdateContext& context){
    if (auto target = m_target.lock(); target && target.get() == &node){
        if (context.animate){
            m_angleRadians += static_cast<float>(context.delta) * m_radiansPerSecond;
        }
        target->setMatrix(m_baseMatrix * glm::rotate(glm::mat4(1), m_angleRadians, m_axis));
    }
}

} // namespace vr
