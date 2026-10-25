#pragma once
#include "Node.h"
#include "NodeVisitor.h"

namespace vr{
class Camera : public Node{
public:
    using Node::Node;

    // Dispatches this concrete node type to the visitor.
    void accept(NodeVisitor& visitor) override{
        visitor.apply(*this);
    }

    // Updates the view matrix after rejecting a camera aimed at its own position.
    void setLookAt(const glm::vec3& eye, const glm::vec3& target, const glm::vec3& up ={0, 1, 0});

    // Validates and sets the perspective projection parameters.
    void setPerspective(float fov, float aspect, float nearPlane, float farPlane);

    // Validates and sets the orthographic projection used by directional light cameras.
    void setOrthographic(float left, float right, float bottom, float top, float nearPlane,
                         float farPlane);

    // Replaces the camera view matrix.
    void setView(const glm::mat4& matrix){
        m_view = matrix;
    }

    // Returns the world-to-camera view matrix.
    const glm::mat4& getView() const{
        return m_view;
    }

    // Returns the camera projection matrix.
    const glm::mat4& getProjection() const{
        return m_projection;
    }

    // Returns the current position in the coordinate system used by this object.
    glm::vec3 getPosition() const;

private:
    glm::mat4 m_view{1};
    glm::mat4 m_projection{1};
};
} // namespace vr
