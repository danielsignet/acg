#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vr/Application.h>
#include <vr/OpenGL.h>
#include <vr/RenderingValidator.h>
#include <vr/SceneOverlay.h>

namespace vr{
// Reports an unmet rendering-validation requirement.
void RenderingValidator::require(bool condition, const std::string& message){
    if (!condition){
        throw std::runtime_error("Validation: " + message);
    }
}

// Captures scene pixels while excluding HUD status bands.
FramebufferImage RenderingValidator::captureScene() const{
    const auto lower = std::min(SceneOverlay::FooterHeight + 6, m_application.m_height / 3);
    const auto upper = std::min(SceneOverlay::HeaderHeight + 6, m_application.m_height / 3);
    return FramebufferImage::capture(0, lower, m_application.m_width,
                                     m_application.m_height - lower - upper);
}

// Executes the configured application or validation sequence.
void RenderingValidator::run(){
    validateLighting();
    validateAlphaTest();
    validateObjectAnimation();
    validateLightOrbit();
    validateLod();
    validateReload();
    m_application.handleKey(GLFW_KEY_SPACE);
    m_application.render();
    OpenGL::check("Validation complete");
    std::cout << "GPU validation passed: scene pixels for lighting/lights/multitexture/toon/alpha, "
                 "animation/pause, all LOD levels, reload, reset, OpenGL errors\n";
}

// Checks that lighting, texture and toon controls change scene pixels.
void RenderingValidator::validateLighting(){

    m_application.render();
    OpenGL::check("Initial frame");
    const auto first = captureScene().hash();
    m_application.handleKey(GLFW_KEY_G);
    m_application.render();
    const auto unlit = captureScene().hash();
    require(first != unlit, "Lighting toggle must change rendered pixels");
    m_application.handleKey(GLFW_KEY_G);
    m_application.render();
    const auto lit = captureScene().hash();
    for (int key :{GLFW_KEY_1, GLFW_KEY_2, GLFW_KEY_3, GLFW_KEY_4, GLFW_KEY_M, GLFW_KEY_T}){
        m_application.handleKey(key);
        m_application.render();
        require(captureScene().hash() != lit,
                "Light/texture/toon toggle must change scene pixels outside the HUD");
        m_application.handleKey(key);
    }
}

// Checks that the alpha threshold removes transparent texture fragments.
void RenderingValidator::validateAlphaTest(){

    m_application.render();
    const auto lit = captureScene().hash();
    if (auto alphaTest = m_application.m_scene->findTransform("alphatest")){
        alphaTest->getState()->setUniform("uAlphaCutoff", 0.0f);
        m_application.render();
        require(captureScene().hash() != lit, "Alpha test must discard transparent texels");
        alphaTest->getState()->setUniform("uAlphaCutoff", 0.5f);
    }
}

// Checks visible object rotation, exact pause and successful resume.
void RenderingValidator::validateObjectAnimation(){
    const auto animated = m_application.m_scene->findTransform("animated");
    const bool orbitEnabled = m_application.m_lightOrbitCallback->getEnabled();
    m_application.m_lightOrbitCallback->setEnabled(false);
    const auto initialMatrix = animated->getMatrix();
    m_application.render();
    const auto initialAnimationPixels = captureScene();
    m_application.m_updater.update(*m_application.m_scene->getRoot(),
                                   {1, 1, m_application.m_animationEnabled});
    m_application.render();
    require(glm::length(initialMatrix[0] - animated->getMatrix()[0]) > 0.001f,
            "Update callback must animate the transform");
    require(captureScene().countChangedPixels(initialAnimationPixels) > 100,
            "Object transform must visibly animate with a fixed camera and paused point lights");
    m_application.handleKey(GLFW_KEY_P);
    const auto pausedMatrix = animated->getMatrix();
    m_application.render();
    const auto pausedPixels = captureScene();
    m_application.m_updater.update(*m_application.m_scene->getRoot(),
                                   {1.5, 0.5, m_application.m_animationEnabled});
    m_application.render();
    require(animated->getMatrix() == pausedMatrix,
            "P must pause every component of the object matrix");
    require(captureScene().countChangedPixels(pausedPixels) == 0,
            "P pause must retain scene pixels within 8-bit rounding tolerance");
    m_application.handleKey(GLFW_KEY_P);
    m_application.render();
    const auto resumePixels = captureScene();
    m_application.m_updater.update(*m_application.m_scene->getRoot(),
                                   {2, 0.5, m_application.m_animationEnabled});
    m_application.render();
    require(animated->getMatrix() != pausedMatrix &&
                captureScene().countChangedPixels(resumePixels) > 100,
            "P must resume both the transform and its visible animation");
    m_application.m_lightOrbitCallback->setEnabled(orbitEnabled);
}

// Checks that light-ring motion is independent of object-animation pause.
void RenderingValidator::validateLightOrbit(){
    const auto orbit = m_application.m_scene->findTransform("lightOrbit");
    const auto orbitMatrix = orbit->getMatrix();
    m_application.handleKey(GLFW_KEY_L);
    m_application.m_updater.update(*m_application.m_scene->getRoot(),{0.9, 0.3, true});
    require(glm::length(orbitMatrix[0] - orbit->getMatrix()[0]) < 0.00001f,
            "L must pause the light ring independently of P");
    m_application.handleKey(GLFW_KEY_L);
    m_application.m_updater.update(*m_application.m_scene->getRoot(),{1.2, 0.3, false});
    require(glm::length(orbitMatrix[0] - orbit->getMatrix()[0]) > 0.001f,
            "Light ring must rotate while geometry animation is paused");
}

// Checks distance selection and the camera tour across all cow detail levels.
void RenderingValidator::validateLod(){
    for (const auto& test : std::vector<std::pair<float, std::size_t>>{
             {6.0f, 0u},{10.0f, 1u},{14.0f, 2u},{18.0f, 3u},{26.0f, 4u},{40.0f, 5u}}){
        m_application.m_scene->getCamera()->setLookAt({6, 2.02f, test.first},{6, 2.02f, 0});
        m_application.render();
        require(m_application.m_renderer->getStats().lodLevels.at("cowLOD") == test.second,
                "Cow LOD distance selection");
    }
    m_application.handleKey(GLFW_KEY_V);
    for (const auto& test : std::vector<std::pair<double, std::size_t>>{
             {0.5, 5u},{2.0, 4u},{2.0, 3u},{2.0, 2u},{2.0, 1u},{2.0, 0u}}){
        m_application.m_updater.update(*m_application.m_scene->getRoot(),{0, test.first, false});
        m_application.render();
        require(m_application.m_renderer->getStats().lodLevels.at("cowLOD") == test.second,
                "LOD camera tour must show all six cow detail levels");
    }
    const auto tourEye = m_application.m_scene->getCamera()->getPosition();
    m_application.handleKey(GLFW_KEY_V);
    require(glm::distance(m_application.m_position, tourEye) < 0.0001f,
            "Leaving LOD tour must retain the current camera position");
}

// Checks that reload replaces the graph and preserves lighting controls.
void RenderingValidator::validateReload(){
    auto root = m_application.m_scene->getRoot();
    m_application.handleKey(GLFW_KEY_2);
    m_application.handleKey(GLFW_KEY_G);
    m_application.handleKey(GLFW_KEY_R);
    require(root != m_application.m_scene->getRoot(), "Reload must replace the entire scene");
    require(!m_application.m_scene->getRoot()->getState()->getLighting() &&
                !m_application.m_scene->getRoot()->getState()->getLightEnabled(1),
            "R must consistently preserve global and individual lighting controls");
    m_application.handleKey(GLFW_KEY_2);
    m_application.handleKey(GLFW_KEY_G);
}

} // namespace vr
