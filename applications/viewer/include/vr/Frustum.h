#pragma once
#include "BoundingBox.h"

namespace vr{
enum class Visibility{ Outside, Intersecting, Inside };

class Frustum{
public:
    // Extracts six normalized inward planes from an OpenGL view-projection matrix.
    explicit Frustum(const glm::mat4& viewProjection);

    // Conservatively classifies a bound, retaining touching and unknown bounds.
    Visibility classify(const BoundingBox& box) const;

    // Returns the normalized inward planes used by portal clipping.
    const std::array<glm::vec4, 6>& getPlanes() const{
        return m_planes;
    }

    // Reconstructs the eight world-space corners for debug visualization.
    static std::array<glm::vec3, 8> corners(const glm::mat4& viewProjection);

private:
    std::array<glm::vec4, 6> m_planes;
};
} // namespace vr
