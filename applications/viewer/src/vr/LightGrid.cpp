#include <vr/Camera.h>
#include <vr/Frustum.h>
#include <vr/Light.h>
#include <vr/LightGrid.h>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace vr{
void LightGrid::build(const Camera& camera, const std::vector<PositionedLight>& lights,
                      bool culling){
    if (lights.size() > State::MaxLights){
        throw std::invalid_argument("Too many lights for the deferred light grid");
    }
    struct Volume{ glm::vec3 center; float radius; unsigned bit; };
    std::array<Volume, State::MaxLights> volumes{};
    unsigned volumeCount = 0;
    unsigned global = 0, active = 0;
    for (unsigned i = 0; i < lights.size(); ++i){
        const auto& light = lights[i];
        if (!light.active || !light.node->getEnabled()) continue;
        ++active;
        const auto position = light.model * light.node->getPosition();
        const auto radius = light.node->getRange();
        // Range is measured in world units in Light::apply, even under scaled parents.
        // Directional and unbounded lights must never be rejected by a position test.
        if (!culling || position.w == 0 || radius == 0){
            global |= 1u << i;
        } else{
            volumes[volumeCount++] = {glm::vec3(position), radius, 1u << i};
        }
    }
    m_possiblePairs = active * Count;
    m_candidatePairs = 0;
    const auto vp = camera.getProjection() * camera.getView();
    for (unsigned y = 0; y < Rows; ++y){
        for (unsigned x = 0; x < Columns; ++x){
            unsigned mask = global;
            if (volumeCount){
                // Crop the projection to this tile, retaining both camera depth planes.
                // Plane/sphere tests also work with the eye inside a light, near-plane
                // intersections, orthographic views and off-screen light centers.
                glm::mat4 crop(1);
                crop[0][0] = static_cast<float>(Columns);
                crop[1][1] = static_cast<float>(Rows);
                crop[3][0] = static_cast<float>(Columns - 1) - 2.0f * x;
                crop[3][1] = static_cast<float>(Rows - 1) - 2.0f * y;
                const Frustum frustum(crop * vp);
                for (unsigned i = 0; i < volumeCount; ++i){
                    const auto& volume = volumes[i];
                    const float tolerance = 0.0001f * std::max(1.0f, volume.radius);
                    const bool outside = std::any_of(
                        frustum.getPlanes().begin(), frustum.getPlanes().end(),
                        [&](const glm::vec4& plane){
                            return glm::dot(glm::vec3(plane), volume.center) + plane.w +
                                       volume.radius < -tolerance;
                        });
                    if (!outside) mask |= volume.bit;
                }
            }
            const unsigned tile = y * Columns + x;
            m_masks[tile / 4][tile % 4] = static_cast<float>(mask);
            for (auto bits = mask; bits; bits &= bits - 1) ++m_candidatePairs;
        }
    }
}

unsigned LightGrid::getMask(unsigned tile) const{
    if (tile >= Count) throw std::out_of_range("Deferred light tile");
    return static_cast<unsigned>(m_masks[tile / 4][tile % 4]);
}
} // namespace vr
