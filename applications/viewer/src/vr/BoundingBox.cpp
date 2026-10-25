#include <cmath>
#include <stdexcept>
#include <vr/BoundingBox.h>

namespace vr{
// Creates an empty bound that can be expanded by points or child bounds.
BoundingBox::BoundingBox(const glm::vec3& minimum, const glm::vec3& maximum)
    : m_minimum(minimum), m_maximum(maximum){
    if (!valid()){
        throw std::invalid_argument("Invalid bounding box");
    }
}

// Reports whether all components are finite and ordered.
bool BoundingBox::valid() const{
    for (int axis = 0; axis < 3; ++axis){
        if (!std::isfinite(m_minimum[axis]) || !std::isfinite(m_maximum[axis]) ||
            m_minimum[axis] > m_maximum[axis]){
            return false;
        }
    }
    return true;
}

// Expands this bound to include one finite point.
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

// Returns corners in binary x/y/z order for transformation and debug drawing.
std::array<glm::vec3, 8> BoundingBox::corners() const{
    std::array<glm::vec3, 8> result{};
    for (unsigned i = 0; i < 8; ++i){
        result[i] ={i & 1 ? m_maximum.x : m_minimum.x, i & 2 ? m_maximum.y : m_minimum.y,
                     i & 4 ? m_maximum.z : m_minimum.z};
    }
    return result;
}

// Encloses all eight corners after applying an affine transformation.
BoundingBox BoundingBox::transformed(const glm::mat4& matrix) const{
    BoundingBox result;
    if (valid()){
        for (const auto& point : corners()){
            result.expandBy(glm::vec3(matrix * glm::vec4(point, 1)));
        }
    }
    return result;
}

} // namespace vr
