#pragma once
#include "PathVisitor.h"

namespace vr{
struct PositionedLight{
    Light* node;
    glm::mat4 model;
    bool active = true;
};

// Collects transformed lights independently of observer frustum and portal visibility.
std::vector<PositionedLight> collectLights(Node& root, const glm::vec3& eye);
} // namespace vr
