#pragma once
#include "Transform.h"
#include "UpdateCallback.h"

namespace vr{
class OrbitCallback final : public UpdateCallback{
public:
    // Captures the base transform and light-ring angular velocity.
    OrbitCallback(const std::shared_ptr<Transform>& target, float degreesPerSecond);

    // Updates the light-ring orbit and its independent user displacement.
    void update(Node& node, const UpdateContext& context) override;

    // Enables or pauses this effect without discarding its current state.
    void setEnabled(bool enabled){
        m_enabled = enabled;
    }

    // Reports whether this effect is currently enabled.
    bool getEnabled() const{
        return m_enabled;
    }

    // Sets the light ring displacement independently of its orbit angle.
    void setOffset(const glm::vec3& offset){
        m_offset = offset;
    }

    // Returns the light ring displacement.
    const glm::vec3& getOffset() const{
        return m_offset;
    }

    // Moves the light ring without altering its current orbit angle.
    void moveBy(const glm::vec3& delta){
        m_offset += delta;
    }

private:
    std::weak_ptr<Transform> m_target;
    glm::mat4 m_baseMatrix;
    glm::vec3 m_offset{0};
    float m_radiansPerSecond;
    float m_angleRadians = 0;
    bool m_enabled = true;
};
} // namespace vr
