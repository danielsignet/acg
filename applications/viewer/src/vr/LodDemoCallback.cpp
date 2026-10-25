#include <cmath>
#include <glm/gtc/matrix_transform.hpp>
#include <stdexcept>
#include <vr/LodDemoCallback.h>

namespace vr{
// Stores the camera and world-space point used by the LOD tour.
LodDemoCallback::LodDemoCallback(const std::shared_ptr<Camera>& camera, const glm::vec3& center)
    : m_camera(camera), m_center(center){
    if (!camera){
        throw std::invalid_argument("LOD demo needs a camera");
    }
}

// Enables or pauses this effect without discarding its current state.
void LodDemoCallback::setEnabled(bool enabled){
    if (enabled && !m_enabled){
        m_elapsedSeconds = 0;
    }
    m_enabled = enabled;
}

// Moves the enabled camera tour through all six cow detail levels.
void LodDemoCallback::update(Node& node, const UpdateContext& context){
    if (!m_enabled){
        return;
    }
    if (auto camera = m_camera.lock(); camera && camera.get() == &node){
        m_elapsedSeconds += context.delta;
        const std::array<float, 14> times{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 14, 15};
        const std::array<float, 14> distances{40, 40, 26, 26, 18, 18, 14, 14, 10, 10, 6, 6, 40, 40};
        const float phase = static_cast<float>(std::fmod(m_elapsedSeconds, times.back()));
        std::size_t segment = 0;
        while (segment + 2 < times.size() && phase >= times[segment + 1]){
            ++segment;
        }
        float t = (phase - times[segment]) / (times[segment + 1] - times[segment]);
        t = t * t * (3 - 2 * t);
        const float distance = glm::mix(distances[segment], distances[segment + 1], t);
        camera->setLookAt(m_center + glm::normalize(glm::vec3(0.3f, 0.18f, 1)) * distance,
                          m_center);
    }
}
} // namespace vr
