#pragma once
#include <array>
#include <glm/glm.hpp>
#include <limits>

namespace vr{
class BoundingBox{
public:
    // Creates an empty bound that can be expanded by points or child bounds.
    BoundingBox() = default;

    // Creates a finite, ordered axis-aligned bound.
    BoundingBox(const glm::vec3& minimum, const glm::vec3& maximum);

    // Reports whether all components are finite and ordered.
    bool valid() const;

    // Expands this bound to include one finite point.
    void expandBy(const glm::vec3& point);

    // Expands this bound to include a nonempty child bound.
    void expandBy(const BoundingBox& box);

    // Encloses all eight corners after applying an affine transformation.
    BoundingBox transformed(const glm::mat4& matrix) const;

    // Returns the midpoint of a valid bound.
    glm::vec3 getCenter() const{
        return (m_minimum + m_maximum) * 0.5f;
    }

    // Returns the half-size along each axis of a valid bound.
    glm::vec3 getExtent() const{
        return (m_maximum - m_minimum) * 0.5f;
    }

    // Returns the minimum corner in local coordinates.
    const glm::vec3& getMinimum() const{
        return m_minimum;
    }

    // Returns the maximum corner in local coordinates.
    const glm::vec3& getMaximum() const{
        return m_maximum;
    }

    // Returns corners in binary x/y/z order for transformation and debug drawing.
    std::array<glm::vec3, 8> corners() const;

private:
    glm::vec3 m_minimum{std::numeric_limits<float>::infinity()};
    glm::vec3 m_maximum{-std::numeric_limits<float>::infinity()};
};
} // namespace vr
