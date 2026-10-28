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
    // Checks material-gallery controls, reflected geometry and persistent BRDF selection.
    void validateMaterialGallery();
    // Checks reflection-source controls, local captures, disabled passes and persistence.
    void validateReflections();
    // Checks the shared AA button, average reset and persistence across renderer and scene changes.
    void validateAntialiasing();
    // Checks normal-map comparison, shared menu controls and reload persistence.
    void validateNormalMapping();

    // Checks performance controls, counters and overlay isolation from the scene.
    void validatePerformance();

    // Reports an unmet rendering-validation requirement.
    static void require(bool condition, const std::string& message);

    // Captures scene pixels while excluding HUD status bands.
    FramebufferImage captureScene() const;
    // Clicks a target action using the same crosshair-relative bounds as the overlay.
    void clickTargetAction(unsigned row);

    // Checks AO, its diagnostic view and the independent forward/deferred switch.
    void validateAmbientOcclusion();

    // Checks public buffer controls, post effects, resize and reload persistence.
    void validateDeferred();

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

    // Checks flashlight illumination, camera tracking, shared controls and reload persistence.
    void validateFlashlight();

    void validateControls();
    // Checks reticle targeting, per-instance subdivision and exact restoration through the UI.
    void validateModelTargeting();
    Application& m_application;
};
} // namespace vr
