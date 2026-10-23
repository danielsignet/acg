#pragma once
#include "Camera.h"
#include "Group.h"
#include "Light.h"
#include "Transform.h"

namespace vr{
class Scene{
public:
    // Creates one graph root and attaches its camera node.
    Scene();

    // Returns the root owned by this scene or resource reader.
    const std::shared_ptr<Group>& getRoot() const{
        return m_root;
    }

    // Returns the scene camera shared with the rendering and update visitors.
    const std::shared_ptr<Camera>& getCamera() const{
        return m_camera;
    }

    // Registers a named transform and rejects ambiguous duplicate names.
    void registerTransform(const std::shared_ptr<Transform>& node);

    // Registers a named light and rejects ambiguous duplicate names.
    void registerLight(const std::shared_ptr<Light>& node);

    // Finds a registered transform by name, or returns an empty pointer.
    std::shared_ptr<Transform> findTransform(const std::string& name) const;

    // Finds a registered light by name, or returns an empty pointer.
    std::shared_ptr<Light> findLight(const std::string& name) const;

    // Sets the position restored when the camera is reset.
    void setInitialEye(const glm::vec3& eye){
        m_initialEye = eye;
    }

    // Sets the look-at target restored when the camera is reset.
    void setInitialTarget(const glm::vec3& target){
        m_initialTarget = target;
    }

    // Returns the initial camera position.
    const glm::vec3& getInitialEye() const{
        return m_initialEye;
    }

    // Returns the initial camera look-at target.
    const glm::vec3& getInitialTarget() const{
        return m_initialTarget;
    }

private:
    std::shared_ptr<Group> m_root;
    std::shared_ptr<Camera> m_camera;
    std::map<std::string, std::shared_ptr<Transform>> m_transforms;
    std::map<std::string, std::shared_ptr<Light>> m_lights;
    glm::vec3 m_initialEye{16, 11, 22};
    glm::vec3 m_initialTarget{0, 1, 0};
};
} // namespace vr
