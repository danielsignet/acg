#pragma once
#include "SceneGraph.h"

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

// Rotate an entire subgraph. Lights and their markers inherit the same matrix.
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

// An opt-in camera dolly keeps real distance-based LOD visible in the demo.
class LodDemoCallback final : public UpdateCallback{
public:
    // Stores the camera and world-space point used by the LOD tour.
    LodDemoCallback(const std::shared_ptr<Camera>& camera, const glm::vec3& center);

    // Moves the enabled camera tour through all six cow detail levels.
    void update(Node& node, const UpdateContext& context) override;

    // Enables or pauses this effect without discarding its current state.
    void setEnabled(bool enabled);

    // Reports whether this effect is currently enabled.
    bool getEnabled() const{
        return m_enabled;
    }

private:
    std::weak_ptr<Camera> m_camera;
    glm::vec3 m_center;
    double m_elapsedSeconds = 0;
    bool m_enabled = false;
};
} // namespace vr
