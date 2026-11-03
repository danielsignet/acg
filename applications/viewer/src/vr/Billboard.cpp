#include <vr/Billboard.h>
#include <algorithm>
#include <cmath>

namespace vr{
glm::mat4 Billboard::facingMatrix(const glm::mat4& parent, const glm::vec3& eye) const{
    const auto base = getWorldMatrix(parent);
    const glm::vec3 up(base[1]);
    const auto axisLength = glm::dot(up, up);
    if (axisLength < 1e-12f) return base;
    auto view = eye - glm::vec3(base[3]);
    view -= up * (glm::dot(view, up) / axisLength);
    // Transform a desired world normal covariantly, including nonuniform scale/shear.
    // The corresponding local rotation leaves Y and the ground anchor unchanged.
    auto normal = glm::transpose(glm::mat3(base)) * view;
    normal.y = 0;
    const auto length = glm::dot(normal, normal);
    if (!std::isfinite(length) || length < 1e-12f) return base;
    normal /= std::sqrt(length);
    const auto right = glm::cross(glm::vec3(0, 1, 0), normal);
    const glm::mat4 rotation(glm::vec4(right, 0), glm::vec4(0, 1, 0, 0),
                             glm::vec4(normal, 0), glm::vec4(0, 0, 0, 1));
    return base * rotation;
}

BoundingBox Billboard::computeBound() const{
    const auto children = Group::computeBound();
    if (!children.valid()) return {};
    float radius = 0;
    for (const auto& corner : children.corners())
        radius = std::max(radius, glm::length(glm::vec2(corner.x, corner.z)));
    // Enclose every yaw before applying the local matrix. Ancestors can cache this
    // volume regardless of which camera, probe or mirror traverses the billboard.
    return BoundingBox({-radius, children.getMinimum().y, -radius},
                       {radius, children.getMaximum().y, radius}).transformed(getMatrix());
}
} // namespace vr
