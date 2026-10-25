#include <glm/gtc/matrix_transform.hpp>
#include <stdexcept>
#include <vr/Camera.h>

namespace vr{
// Updates the view matrix after rejecting a camera aimed at its own position.
void Camera::setLookAt(const glm::vec3& eye, const glm::vec3& target, const glm::vec3& up){
    if (glm::length(target - eye) < 0.0001f){
        throw std::invalid_argument("Camera target equals eye");
    }
    m_view = glm::lookAt(eye, target, up);
}

// Validates and sets the perspective projection parameters.
void Camera::setPerspective(float fov, float aspect, float nearPlane, float farPlane){
    if (!(fov > 1 && fov < 179 && aspect > 0 && nearPlane > 0 && farPlane > nearPlane)){
        throw std::invalid_argument("Invalid camera projection");
    }
    m_projection = glm::perspective(glm::radians(fov), aspect, nearPlane, farPlane);
}

// Returns the current position in the coordinate system used by this object.
glm::vec3 Camera::getPosition() const{
    return glm::vec3(glm::inverse(m_view)[3]);
}

// Validates and sets the orthographic projection used by directional light cameras.
void Camera::setOrthographic(float left, float right, float bottom, float top, float nearPlane,
                             float farPlane){
    if (!(right > left && top > bottom && nearPlane > 0 && farPlane > nearPlane)){
        throw std::invalid_argument("Invalid orthographic projection");
    }
    m_projection = glm::ortho(left, right, bottom, top, nearPlane, farPlane);
}
} // namespace vr
