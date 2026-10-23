#include <stdexcept>
#include <vr/Scene.h>

namespace vr{
// Creates one graph root and attaches its camera node.
Scene::Scene()
    : m_root(std::make_shared<Group>("root")), m_camera(std::make_shared<Camera>("camera")){
    m_root->addChild(m_camera);
}

// Registers a named transform and rejects ambiguous duplicate names.
void Scene::registerTransform(const std::shared_ptr<Transform>& n){
    if (!m_transforms.emplace(n->getName(), n).second){
        throw std::runtime_error("Duplicate transform: " + n->getName());
    }
}

// Registers a named light and rejects ambiguous duplicate names.
void Scene::registerLight(const std::shared_ptr<Light>& n){
    if (!m_lights.emplace(n->getName(), n).second){
        throw std::runtime_error("Duplicate light: " + n->getName());
    }
}

// Finds a registered transform by name, or returns an empty pointer.
std::shared_ptr<Transform> Scene::findTransform(const std::string& name) const{
    auto i = m_transforms.find(name);
    return i == m_transforms.end() ? nullptr : i->second;
}

// Finds a registered light by name, or returns an empty pointer.
std::shared_ptr<Light> Scene::findLight(const std::string& name) const{
    auto i = m_lights.find(name);
    return i == m_lights.end() ? nullptr : i->second;
}
} // namespace vr
