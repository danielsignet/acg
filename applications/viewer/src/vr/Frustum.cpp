#include <cmath>
#include <stdexcept>
#include <vr/Frustum.h>

namespace vr{
// Extracts six normalized inward planes from an OpenGL view-projection matrix.
Frustum::Frustum(const glm::mat4& viewProjection){
    const auto row = [&](int index){
        return glm::vec4(viewProjection[0][index], viewProjection[1][index],
                         viewProjection[2][index], viewProjection[3][index]);
    };
    // OpenGL clip space: -w <= x,y,z <= w. Plane normals point inward.
    m_planes ={row(3) + row(0), row(3) - row(0), row(3) + row(1),
                row(3) - row(1), row(3) + row(2), row(3) - row(2)};
    for (auto& plane : m_planes){
        const float length = glm::length(glm::vec3(plane));
        if (!(length > 1e-8f) || !std::isfinite(length)){
            throw std::invalid_argument("Invalid view-projection matrix");
        }
        plane /= length;
    }
}

// Conservatively classifies a bound, retaining touching and unknown bounds.
Visibility Frustum::classify(const BoundingBox& box) const{
    if (!box.valid()){
        return Visibility::Intersecting;
    }
    bool inside = true;
    for (const auto& plane : m_planes){
        const auto normal = glm::vec3(plane);
        const float distance = glm::dot(normal, box.getCenter()) + plane.w;
        const float radius = glm::dot(glm::abs(normal), box.getExtent());
        // Conservatively retain touching geometry, including numerical roundoff.
        if (distance + radius < -0.0001f){
            return Visibility::Outside;
        }
        if (distance - radius < 0.0001f){
            inside = false;
        }
    }
    return inside ? Visibility::Inside : Visibility::Intersecting;
}

// Reconstructs the eight world-space corners for debug visualization.
std::array<glm::vec3, 8> Frustum::corners(const glm::mat4& viewProjection){
    const auto inverse = glm::inverse(viewProjection);
    std::array<glm::vec3, 8> points{};
    for (unsigned i = 0; i < 8; ++i){
        const auto point = inverse * glm::vec4(i & 1 ? 1.0f : -1.0f, i & 2 ? 1.0f : -1.0f,
                                               i & 4 ? 1.0f : -1.0f, 1);
        if (std::abs(point.w) < 1e-8f){
            throw std::invalid_argument("Unbounded frustum");
        }
        points[i] = glm::vec3(point) / point.w;
    }
    return points;
}
} // namespace vr
