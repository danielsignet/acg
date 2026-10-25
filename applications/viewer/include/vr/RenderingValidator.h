#pragma once
#include "FramebufferImage.h"
#include <string>

namespace vr{
class Application;

class RenderingValidator final{
public:
    // Binds rendering checks to the application under validation.
    explicit RenderingValidator(Application& application) : m_application(application){
    }

    // Executes the configured application or validation sequence.
    void run();

private:
    // Reports an unmet rendering-validation requirement.
    static void require(bool condition, const std::string& message);

    // Captures scene pixels while excluding HUD status bands.
    FramebufferImage captureScene() const;

    // Checks that lighting, texture and toon controls change scene pixels.
    void validateLighting();

    // Checks that the alpha threshold removes transparent texture fragments.
    void validateAlphaTest();

    // Checks visible object rotation, exact pause and successful resume.
    void validateObjectAnimation();

    // Checks that light-ring motion is independent of object-animation pause.
    void validateLightOrbit();

    // Checks distance selection and the camera tour across all cow detail levels.
    void validateLod();

    // Checks that reload replaces the graph and preserves lighting controls.
    void validateReload();
    void validateCulling();
    void validatePortals();
    // Checks the shadow toggle and the complete lamp-displacement path into the depth map.
    void validateShadows();

    void validateControls();
    Application& m_application;
};
} // namespace vr
