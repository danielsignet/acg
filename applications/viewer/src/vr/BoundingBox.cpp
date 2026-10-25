#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <vr/BoundingBox.h>

namespace vr{
BoundingBox::BoundingBox(const glm::vec3& minimum, const glm::vec3& maximum)
    : m_minimum(minimum), m_maximum(maximum){
    if (!valid()){
        throw std::invalid_argument("Invalid bounding box");
    }
}

bool BoundingBox::valid() const{
    for (int axis = 0; axis < 3; ++axis){
        if (!std::isfinite(m_minimum[axis]) || !std::isfinite(m_maximum[axis]) ||
            m_minimum[axis] > m_maximum[axis]){
            return false;
        }
    }
    return true;
}

void BoundingBox::expandBy(const glm::vec3& point){
    for (int axis = 0; axis < 3; ++axis){
        if (!std::isfinite(point[axis])){
            throw std::invalid_argument("Non-finite bound point");
        }
    }
    m_minimum = glm::min(m_minimum, point);
    m_maximum = glm::max(m_maximum, point);
}

void BoundingBox::expandBy(const BoundingBox& box){
    if (box.valid()){
        expandBy(box.m_minimum);
        expandBy(box.m_maximum);
    }
}

std::array<glm::vec3, 8> BoundingBox::corners() const{
    std::array<glm::vec3, 8> result{};
    for (unsigned i = 0; i < 8; ++i){
        result[i] = {i & 1 ? m_maximum.x : m_minimum.x, i & 2 ? m_maximum.y : m_minimum.y,
                     i & 4 ? m_maximum.z : m_minimum.z};
    }
    return result;
}

BoundingBox BoundingBox::transformed(const glm::mat4& matrix) const{
    BoundingBox result;
    if (valid()){
        for (const auto& point : corners()){
            result.expandBy(glm::vec3(matrix * glm::vec4(point, 1)));
        }
    }
    return result;
}

Frustum::Frustum(const glm::mat4& vp){
    const auto row = [&](int r){ return glm::vec4(vp[0][r], vp[1][r], vp[2][r], vp[3][r]); };
    // OpenGL clip space: -w <= x,y,z <= w. Plane normals point inward.
    m_planes = {row(3) + row(0), row(3) - row(0), row(3) + row(1),
                row(3) - row(1), row(3) + row(2), row(3) - row(2)};
    for (auto& plane : m_planes){
        const float length = glm::length(glm::vec3(plane));
        if (!(length > 1e-8f) || !std::isfinite(length)){
            throw std::invalid_argument("Invalid view-projection matrix");
        }
        plane /= length;
    }
}

Visibility Frustum::classify(const BoundingBox& box) const{
    if (!box.valid()){
        return Visibility::Intersecting;
    }
    bool inside = true;
    for (const auto& plane : m_planes){
        const auto n = glm::vec3(plane);
        const float distance = glm::dot(n, box.getCenter()) + plane.w;
        const float radius = glm::dot(glm::abs(n), box.getExtent());
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

std::array<glm::vec3, 8> Frustum::corners(const glm::mat4& vp){
    const auto inverse = glm::inverse(vp);
    std::array<glm::vec3, 8> points{};
    for (unsigned i = 0; i < 8; ++i){
        const auto p = inverse * glm::vec4(i & 1 ? 1.0f : -1.0f, i & 2 ? 1.0f : -1.0f,
                                           i & 4 ? 1.0f : -1.0f, 1);
        if (std::abs(p.w) < 1e-8f){
            throw std::invalid_argument("Unbounded frustum");
        }
        points[i] = glm::vec3(p) / p.w;
    }
    return points;
}
} // namespace vr
