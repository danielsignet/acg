#pragma once
#include <array>
#include <glm/glm.hpp>
#include <limits>

namespace vr{
class BoundingBox{
public:
    BoundingBox() = default;
    BoundingBox(const glm::vec3& minimum, const glm::vec3& maximum);
    bool valid() const;
    void expandBy(const glm::vec3& point);
    void expandBy(const BoundingBox& box);
    BoundingBox transformed(const glm::mat4& matrix) const;

    glm::vec3 getCenter() const{
        return (m_minimum + m_maximum) * .5f;
    }

    glm::vec3 getExtent() const{
        return (m_maximum - m_minimum) * .5f;
    }

    const glm::vec3& getMinimum() const{
        return m_minimum;
    }

    const glm::vec3& getMaximum() const{
        return m_maximum;
    }

    std::array<glm::vec3, 8> corners() const;

private:
    glm::vec3 m_minimum{std::numeric_limits<float>::infinity()};
    glm::vec3 m_maximum{-std::numeric_limits<float>::infinity()};
};
enum class Visibility{ Outside, Intersecting, Inside };

class Frustum{
public:
    explicit Frustum(const glm::mat4& viewProjection);
    Visibility classify(const BoundingBox& box) const;

    const std::array<glm::vec4, 6>& getPlanes() const{
        return m_planes;
    }

    static std::array<glm::vec3, 8> corners(const glm::mat4& viewProjection);

private:
    std::array<glm::vec4, 6> m_planes;
};
} // namespace vr
