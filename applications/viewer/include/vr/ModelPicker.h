#pragma once
#include "CullVisitor.h"

namespace vr{
struct ModelHit{
    ModelInstance* instance;
    glm::mat4 model;
    float distance;
};

// Finds the closest visible model, including glass, while ignoring transparent debug walls.
std::optional<ModelHit> pickModel(const std::vector<RenderNode>& items, const glm::vec3& eye,
                                  const glm::vec3& direction, float* surfaceDistance=nullptr);
} // namespace vr
