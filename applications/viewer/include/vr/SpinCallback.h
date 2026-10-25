#pragma once
#include "Transform.h"
#include "UpdateCallback.h"

namespace vr{
class SpinCallback final : public UpdateCallback{
public:
    // Captures the base transform and a normalized local rotation axis.
    SpinCallback(const std::shared_ptr<Transform>& target, float degreesPerSecond,
                 const glm::vec3& axis ={0.3f, 1, 0.2f});

    // Rotates the local object only while object animation is enabled.
    void update(Node& node, const UpdateContext& context) override;

private:
    std::weak_ptr<Transform> m_target;
    glm::mat4 m_baseMatrix;
    glm::vec3 m_axis;
    float m_radiansPerSecond;
    float m_angleRadians = 0;
};
} // namespace vr
