#include <vr/ShadowCameras.h>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace vr{
// Fits the complete scene in the directional light camera.
void fitDirectionalCamera(Camera& camera, const BoundingBox& bounds, const glm::vec3& direction){
    if (!bounds.valid() || glm::length(direction) < 1e-6f){
        throw std::invalid_argument("Directional shadow camera needs bounds and direction");
    }
    const auto forward = glm::normalize(direction);
    const float radius = glm::length(bounds.getExtent()) + 1;
    camera.setLookAt(bounds.getCenter() + forward * (radius + 1), bounds.getCenter(),
                     std::abs(forward.y) > 0.95f ? glm::vec3(0, 0, 1) : glm::vec3(0, 1, 0));
    const auto lightBounds = bounds.transformed(camera.getView());
    const auto minimum = lightBounds.getMinimum(), maximum = lightBounds.getMaximum();
    const float padding = 0.1f + radius * 0.01f;
    camera.setOrthographic(minimum.x - padding, maximum.x + padding, minimum.y - padding,
                           maximum.y + padding, std::max(0.01f, -maximum.z - padding),
                           -minimum.z + padding);
}

// Configures the six OpenGL cubemap face orientations.
void configurePointCameras(const std::array<std::shared_ptr<Camera>, 6>& cameras,
                           const glm::vec3& position, float farPlane){
    const std::array<glm::vec3, 6> directions ={glm::vec3(1, 0, 0),{-1, 0, 0},{0, 1, 0},
                                                 {0, -1, 0},{0, 0, 1},{0, 0, -1}};
    const std::array<glm::vec3, 6> up ={glm::vec3(0, -1, 0),{0, -1, 0},{0, 0, 1},
                                         {0, 0, -1},{0, -1, 0},{0, -1, 0}};
    for (unsigned face = 0; face < 6; ++face){
        if (!cameras[face]){
            throw std::invalid_argument("Missing point-light camera");
        }
        cameras[face]->setLookAt(position, position + directions[face], up[face]);
        cameras[face]->setPerspective(90, 1, 0.05f, farPlane);
    }
}
} // namespace vr
