#pragma once
#include "Scene.h"

namespace vr{
class Flashlight final{
public:
    // Attaches a warm spotlight to the scene and restores its enabled state.
    Flashlight(Scene& scene, bool enabled);

    // Follows the live camera position and viewing direction.
    void update(const Camera& camera);

    // Switches the beam without changing the scene's other light slots.
    void setEnabled(bool enabled){
        m_light->setEnabled(enabled);
    }

    // Returns the flashlight's current switch state.
    bool getEnabled() const{
        return m_light->getEnabled();
    }

private:
    std::shared_ptr<Light> m_light;
};
} // namespace vr
