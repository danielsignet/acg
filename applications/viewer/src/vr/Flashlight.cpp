#include <vr/Flashlight.h>

namespace vr{
// Attaches a warm spotlight to the scene and restores its enabled state.
Flashlight::Flashlight(Scene& scene, bool enabled)
    : m_light(std::make_shared<Light>("flashlight")){
    m_light->setAmbient({0, 0, 0});
    m_light->setDiffuse({4, 3.8f, 3.4f});
    m_light->setSpecular({1.2f, 1.1f, 1});
    m_light->setAttenuation({1, 0.025f, 0.008f});
    m_light->setRange(35);
    m_light->setSpot({0, 0, -1}, 14, 20);
    m_light->setEnabled(enabled);
    scene.registerLight(m_light);
    scene.getRoot()->addChild(m_light);
    update(*scene.getCamera());
}

// Follows the live camera position and viewing direction.
void Flashlight::update(const Camera& camera){
    const auto direction = -glm::vec3(glm::inverse(camera.getView())[2]);
    m_light->setPosition(glm::vec4(camera.getPosition() + direction * 0.12f, 1));
    m_light->setSpot(direction, 14, 20);
}
} // namespace vr
