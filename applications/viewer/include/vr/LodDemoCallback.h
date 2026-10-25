#pragma once
#include "Camera.h"
#include <array>

namespace vr{
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
