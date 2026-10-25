#pragma once
#include "Camera.h"
#include <array>

namespace vr{
// Fits an orthographic light camera around every world-space scene bound corner.
void fitDirectionalCamera(Camera& camera, const BoundingBox& bounds, const glm::vec3& direction);

// Positions the six perspective cameras using OpenGL cubemap face directions.
void configurePointCameras(const std::array<std::shared_ptr<Camera>, 6>& cameras,
                           const glm::vec3& position, float farPlane);
} // namespace vr
