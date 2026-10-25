#include <cmath>
#include <glm/gtc/matrix_transform.hpp>
#include <stdexcept>
#include <vr/OrbitCallback.h>

namespace vr{
// Captures the base transform and light-ring angular velocity.
OrbitCallback::OrbitCallback(const std::shared_ptr<Transform>& target, float speed)
    : m_target(target), m_baseMatrix(target ? target->getMatrix() : glm::mat4(1)),
      m_radiansPerSecond(glm::radians(speed)){
    if (!target || !std::isfinite(speed)){
        throw std::invalid_argument("Invalid orbit animation");
    }
}

// Updates the light-ring orbit and its independent user displacement.
void OrbitCallback::update(Node& node, const UpdateContext& context){
    if (auto target = m_target.lock(); target && target.get() == &node){
        if (m_enabled){
            m_angleRadians += static_cast<float>(context.delta) * m_radiansPerSecond;
        }
        target->setMatrix(glm::translate(glm::mat4(1), m_offset) * m_baseMatrix *
                          glm::rotate(glm::mat4(1), m_angleRadians, glm::vec3(0, 1, 0)));
    }
}

} // namespace vr
