#include <vr/ResourceLocator.h>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vr/Application.h>
#include <vr/OpenGL.h>
#include <vr/RenderingValidator.h>
#include <vr/SceneOverlay.h>
#include <vr/LightCollector.h>

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

// Clicks a target action using the same crosshair-relative bounds as the overlay.
void RenderingValidator::clickTargetAction(unsigned row){
    const auto rect = SceneOverlay::targetActionRectangle(m_application, row);
    m_application.handleClick(rect.x + 8, rect.y + 8);
}

// Executes the configured application or validation sequence.
void RenderingValidator::run(){
    const bool previews = m_application.m_bufferPreviews;
    m_application.m_bufferPreviews = false;
    validateNormalMapping();
    validateMaterialGallery();
    validateReflections();
    validateAmbientOcclusion();
    validateDeferred();
    validatePerformance();
    validateAntialiasing();
    validateShadows();
    validateControls();
    validateModelTargeting();
    validateFlashlight();
    validateLighting();
    validateAlphaTest();
    validateObjectAnimation();
    validateLightOrbit();
    validateLod();
    validateReload();
    validateCulling();
    validatePortals();
    m_application.execute(Controls::Action::ResetView);
    m_application.render();
    m_application.m_bufferPreviews = previews;
    OpenGL::check("Validation complete");
    std::cout << "GPU validation passed: scene pixels for lighting/lights/multitexture/toon/alpha, "
                 "animation/pause, all LOD levels, reload, reset, frustum culling, OpenGL errors\n";
}

// Checks normal-map comparison, shared menu controls and reload persistence.
void RenderingValidator::validateNormalMapping(){
    auto& app = m_application;
    using Action = Controls::Action;
    const auto view = app.m_scene->getCamera()->getView();
    const auto lights = app.m_pointLights;
    app.m_scene->getCamera()->setLookAt({-1.8f, 2.9f, -0.5f},{-6, 0.4f, -5});
    app.render();
    const auto mapped = captureScene();
    app.handleKey(GLFW_KEY_Z);
    app.handleKey(GLFW_KEY_N);
    app.render();
    require(app.m_shadingMode == ShadingMode::UnmappedPhong && app.m_pointLights == lights &&
                mapped.countChangedPixels(captureScene()) > 100,
            "Z then N shows the same scene without normal maps and leaves the lights unchanged");
    app.handleKey(GLFW_KEY_Z);
    app.handleKey(GLFW_KEY_4);
    app.m_bufferPreviews = true;
    const auto preview = app.m_deferred->previewRectangle(1);
    app.handleClick(static_cast<float>(preview.x + 8), static_cast<float>(preview.y + 8));
    require(app.m_bufferView == BufferView::Lit && app.m_shadingMode == ShadingMode::UnmappedPhong,
            "Closing a normal-buffer preview returns to the selected plain shader");
    app.m_bufferPreviews = false;
    app.reload();
    require(app.m_shadingMode == ShadingMode::UnmappedPhong,
            "Reload retains the normal-map comparison mode");
    app.handleKey(GLFW_KEY_F2);
    app.handleKey(GLFW_KEY_Z);
    app.handleKey(GLFW_KEY_1);
    require(!app.m_deferredEnabled && app.m_shadingMode == ShadingMode::Phong,
            "The same normal-map menu works in the forward renderer");
    app.execute(Action::Renderer);
    app.m_scene->getCamera()->setView(view);
}

// Checks material-gallery controls, reflected geometry and persistent BRDF selection.
void RenderingValidator::validateMaterialGallery(){
    auto& app = m_application;
    if (!app.m_scene->findTransform("materialMirror")){
        return;
    }
    const auto view = app.m_scene->getCamera()->getView();
    const auto lights = app.m_pointLights;
    app.m_scene->getCamera()->setLookAt({23, 7, -63},{0, 2.3f, -44.18f});
    app.handleKey(GLFW_KEY_Z);
    app.handleKey(GLFW_KEY_P);
    app.render();
    const auto cook = captureScene();
    require(app.m_shadingMode == ShadingMode::CookTorrance && app.m_reflection->getRendered() &&
                app.m_reflection->getStats().draws > 8,
            "Cook-Torrance renders the rear patio and its independently culled mirror view");
    app.handleKey(GLFW_KEY_Z);
    app.handleKey(GLFW_KEY_H);
    app.render();
    require(app.m_shadingMode == ShadingMode::BlinnPhong &&
                cook.countChangedPixels(captureScene()) > 200 && app.m_pointLights == lights,
            "The shared shader menu switches BRDF lighting without changing the light controls");
    app.reload();
    require(app.m_shadingMode == ShadingMode::BlinnPhong &&
                app.m_renderer->getShadingMode() == ShadingMode::BlinnPhong && app.m_environment,
            "Reload preserves the selected BRDF and reconstructs the HDR environment");
    const auto gold = app.m_scene->findTransform("goldMetal")->getState()->getMaterial();
    require(gold && gold->getMetallic() == 1 && std::abs(gold->getRoughness() - 0.16f) < 1e-6f,
            "The scene reader retains the gallery's explicit physical material parameters");
    if (auto left = app.m_scene->findLight("carHeadlightLeft")){
        const auto right = app.m_scene->findLight("carHeadlightRight");
        require(right && left->getSpot() && right->getSpot() && left->getRange() > 0,
                "Both car headlights are finite spotlight beams");
        require(!left->getEnabled() && !right->getEnabled() && !app.m_carHeadlightsEnabled,
                "The car's headlamps start switched off");
        app.m_scene->getCamera()->setLookAt({24, 3.5f, 2},{18, 1.1f, -7});
        app.render();
        require(app.carTargeted(), "The car exposes its contextual headlights button");
        for (const bool deferred :{true, false}){
            if (app.m_deferredEnabled != deferred){
                app.execute(Controls::Action::Renderer);
            }
            app.render();
            const auto unlitCar = captureScene();
            const auto headlightPasses = app.m_shadowRenderer->getStats().passes;
            clickTargetAction(1);
            app.render();
            require(left->getEnabled() && right->getEnabled() && app.m_carHeadlightsEnabled &&
                        captureScene().countChangedPixels(unlitCar) > 150 &&
                        app.m_shadowRenderer->getStats().passes == headlightPasses + 12,
                    "The car button enables visible beams and shadows in both renderers");
            clickTargetAction(1);
            app.render();
            require(!left->getEnabled() && !right->getEnabled() &&
                        captureScene().hash() == unlitCar.hash() &&
                        app.m_shadowRenderer->getStats().passes == headlightPasses,
                    "Switching headlights off restores illumination and skips their shadows");
        }
        app.execute(Controls::Action::Renderer);
        app.m_scene->getCamera()->setLookAt({0, 8, 12},{0, 30, 13});
        app.render();
        clickTargetAction(1);
        require(!app.carTargeted() && !app.m_carHeadlightsEnabled,
                "The headlights button is inactive without the car under the crosshair");
        app.m_scene->getCamera()->setLookAt({24, 3.5f, 2},{18, 1.1f, -7});
        app.render();
        bool paint = false, windows = false, rubber = false;
        for (const auto& item : app.m_renderer->getRenderList()){
            if (item.instance && item.instance->getName() == "porsche"){
                const auto& material = *item.state.getMaterial();
                paint |= material.getMetallic() == 0.88f;
                windows |= material.getMetallic() == 1 && material.getRoughness() < 0.03f;
                rubber |= material.getMetallic() == 0 && material.getRoughness() > 0.8f;
            }
        }
        require(paint && windows && rubber,
                "The car retains separate metallic paint, mirror windows and rough nonmetals");
    }
    if (app.m_scene->findTransform("clearGlass")){
        app.m_scene->getCamera()->setLookAt({8, 4.2f, -44.8f},{0, 1.99f, -48});
        app.render();
        require(app.m_target && app.m_target->instance->getName() == "Clear glass" &&
                    app.m_renderer->getStats().screenCopies == 1,
                "Glass is selectable and captures its own background");
        app.m_scene->getCamera()->setLookAt({0, 4, -61},{0, 2.3f, -44.18f});
        app.render();
        require(app.m_reflection->getRendered() && app.m_reflection->getStats().screenCopies == 1,
                "Reflected glass captures the independently visible scene in the mirror");
    }
    for (const auto& eye :{glm::vec3(24, 3.5f, 2), glm::vec3(0, 2, -37)}){
        app.m_scene->getCamera()->setLookAt(eye,{0, 2.3f, -44.18f});
        app.render();
        require(!app.m_reflection->getRendered() && !app.m_reflection->getTexture() &&
                    app.performanceCounters()[static_cast<std::size_t>(
                        FrameProfiler::Counter::MirrorDraws)] == 0,
                "The wall-mounted mirror submits no reflection draws from the patio or house");
    }
    app.m_scene->getCamera()->setLookAt({0, 3, -55},{0, 3, -65});
    app.render();
    require(!app.m_reflection->getRendered(),
            "Looking away from the gallery mirror skips its reflection pass");
    app.execute(Controls::Action::PhongShader);
    app.m_scene->getCamera()->setView(view);
}

// Checks reflection-source controls, local captures, disabled passes and persistence.
void RenderingValidator::validateReflections(){
    auto& app = m_application;
    if (!app.m_scene->findTransform("carExhibit")){
        return;
    }
    const auto view = app.m_scene->getCamera()->getView();
    const auto shading = app.m_shadingMode;
    app.execute(Controls::Action::CookTorranceShader);
    app.m_scene->getCamera()->setLookAt({24, 3.5f, 2},{18, 1.1f, -7});
    app.render();
    const auto original = captureScene();
    const auto albedo = app.m_deferred->getGBuffer().read(GBuffer::Albedo);
    app.handleClick(460, 76);
    require(app.m_reflectionMode == ReflectionMode::Cubemaps,
            "The reflection button advances Environment to Cubemaps");
    for (unsigned frame = 0; frame < 90; ++frame){
        app.render();
        require(app.m_reflectionProbes && app.m_reflectionProbes->getFaces() <= 1,
                "Local reflections respect the one-cube-face-per-frame capture budget");
    }
    require(app.m_reflectionProbes->getReadyCount() > 0 &&
                captureScene().countChangedPixels(original) > 1000 &&
                app.m_deferred->getGBuffer().read(GBuffer::Albedo) == albedo,
            "Local cubemaps change reflected radiance without changing material albedo");
    app.execute(Controls::Action::Renderer);
    app.render();
    require(app.m_reflectionMode == ReflectionMode::Cubemaps,
            "Forward rendering retains the selected local reflection source");
    app.execute(Controls::Action::Renderer);
    app.handleKey(GLFW_KEY_F5);
    app.render();
    require(app.m_reflectionMode == ReflectionMode::Off && app.m_reflectionProbes->getFaces() == 0,
            "F5 switches Cubemaps off and stops auxiliary cube captures");
    app.m_scene->getCamera()->setLookAt({0, 4, -61},{0, 2.3f, -44.18f});
    app.render();
    require(!app.m_reflection->getRendered(),
            "Reflection Off also skips the visible planar mirror");
    app.reload();
    require(app.m_reflectionMode == ReflectionMode::Off,
            "Scene reload preserves the reflection comparison mode");
    app.handleKey(GLFW_KEY_F5);
    require(app.m_reflectionMode == ReflectionMode::Environment,
            "F5 completes the Off, Environment, Cubemaps cycle");
    app.m_shadingMode = shading;
    app.m_renderer->setShadingMode(shading);
    app.m_scene->getCamera()->setView(view);
    app.render();
}

// Checks reticle targeting, per-instance subdivision and exact restoration through the UI.
void RenderingValidator::validateModelTargeting(){
    auto& app = m_application;
    const auto camera = app.m_scene->getCamera();
    const auto originalView = camera->getView();
    camera->setLookAt({-3.6f, 2.6f, 1.5f},{-4.8f, 2.4f, 0});
    app.render();
    require(app.m_target && app.m_target->instance->getName() == "monkey",
            "The center camera ray targets the visible model instance");
    require(app.m_deferred->getFocusDistance() == std::optional<float>(app.m_target->distance),
            "The highlighted surface supplies this frame's exact ray-hit focus distance");
    auto* target = app.m_target->instance;
    const auto original = captureScene();
    app.handleKey(GLFW_KEY_F1);
    app.m_profiler.beginFrame(0.02);
    {
        FrameProfiler::Scope events(&app.m_profiler, FrameProfiler::Stage::Events);
        clickTargetAction(0);
    }
    app.render();
    app.m_profiler.endFrame();
    const auto measured = app.m_profiler.summarize();
    const auto cpu = [&](FrameProfiler::Stage stage){
        return measured.cpu[static_cast<std::size_t>(stage)];
    };
    require(cpu(FrameProfiler::Stage::Events) >= cpu(FrameProfiler::Stage::Subdivision) &&
                cpu(FrameProfiler::Stage::Subdivision) > 0 &&
                cpu(FrameProfiler::Stage::Picking) > 0 && app.m_lastSubdivisionMs > 0 &&
                app.m_lastSubdivision == "monkey ON",
            "F1 includes targeted subdivision, event handling and model raycasting");
    app.handleKey(GLFW_KEY_F1);
    app.render();
    require(target->getSubdivided() && captureScene().countChangedPixels(original) > 100,
            "The generic subdivision button changes the targeted model's rendered geometry");
    app.handleKey(GLFW_KEY_F4);
    app.render();
    require(!target->getSubdivided() && captureScene().hash() == original.hash(),
            "F4 restores the original scene pixels after clicking the subdivision button");
    camera->setLookAt({0, 8, 12},{0, 30, 13});
    app.render();
    require(!app.m_target, "Aiming into empty space clears the highlighted model");
    require(!app.m_deferred->getFocusDistance(),
            "Empty-space targeting clears the previous DoF focus");
    const auto status = app.m_status;
    clickTargetAction(0);
    require(!target->getSubdivided() && app.m_status == status,
            "The hidden context button cannot subdivide a model without a target");
    app.handleKey(GLFW_KEY_F4);
    require(!target->getSubdivided() && app.m_status == "No target",
            "F4 handles an absent target in the same way as the button");
    CullVisitor objects;
    objects.collect(*app.m_scene->getRoot(), glm::mat4(1),{}, false);
    for (const auto& name :{"Red point light", "Green point light", "Blue point light",
                             "White point light", "Leaf", "torus"}){
        const auto found = std::find_if(
            objects.getRenderList().begin(), objects.getRenderList().end(),
            [&](const auto& item){ return item.instance && item.instance->getName() == name; });
        require(found != objects.getRenderList().end(), "Selectable scene object exists");
        const bool torus = std::string(name) == "torus", leaf = std::string(name) == "Leaf";
        const glm::vec3 point =
            torus ? glm::vec3(1.3f, 0.38f, 0) : found->geometry->getBound().getCenter();
        const glm::vec3 direction = torus ? glm::vec3(0, 1, 0) : glm::vec3(0, 0, 1);
        const auto center = glm::vec3(found->model * glm::vec4(point, 1));
        const auto normal = glm::normalize(glm::mat3(found->model) * direction);
        camera->setLookAt(center + normal * 2.0f, center);
        app.render();
        require(app.m_target && app.m_target->instance == found->instance,
                std::string("The crosshair selects ") + name);
        require(app.m_deferred->getFocusDistance() == std::optional<float>(app.m_target->distance),
                "Changing target updates its surface focus distance in the same frame");
        clickTargetAction(0);
        app.render();
        require(found->instance->getSubdivided(),
                std::string("The targeted procedural mesh subdivides: ") + name);
        clickTargetAction(0);
        require(!found->instance->getSubdivided(), "Procedural meshes restore their original");
        if (leaf){
            const auto corner = glm::vec3(found->model * glm::vec4(-0.95f, 0.05f, 0, 1));
            camera->setLookAt(corner + normal * 2.0f, corner);
            app.render();
            require(
                !app.m_target || app.m_target->instance != found->instance,
                "Leaf targeting passes through transparent pixels instead of selecting its panel");
        }
    }
    camera->setLookAt({30, 4, 10},{30, 0, 9.9f});
    app.render();
    require(!app.m_target, "The outdoor floor remains an unselectable ray occluder");
    require(!app.m_deferred->getFocusDistance(),
            "An unselectable floor cannot become a DoF target");
    camera->setView(originalView);
}

// Checks performance controls, counters and overlay isolation from the scene.
void RenderingValidator::validatePerformance(){
    auto& app = m_application;
    const auto lights = app.m_pointLights;
    const bool culling = app.m_frustumCulling, previews = app.m_bufferPreviews;
    const int width = app.m_width, height = app.m_height;
    app.render();
    const auto image = captureScene();
    const auto reticle = FramebufferImage::capture(width / 2 - 12, height / 2 - 12, 24, 24);
    app.handleKey(GLFW_KEY_F1);
    app.m_profiler.beginFrame(0.08);
    app.render();
    app.m_profiler.endFrame();
    require(app.m_profiler.getEnabled() && captureScene().countChangedPixels(image) > 1000,
            "F1 opens the measured performance view");
    require(FramebufferImage::capture(width / 2 - 12, height / 2 - 12, 24, 24).hash() ==
                reticle.hash(),
            "F1 preserves the visible crosshair at the optical center");
    require(app.m_pointLights == lights && app.m_frustumCulling == culling &&
                app.m_bufferPreviews == previews && !app.m_frozenEye,
            "Performance inspection preserves lights, culling, previews and the live camera");
    const auto& shadows = app.m_shadowRenderer->getStats();
    const auto unculled = std::count_if(app.m_renderer->getRenderList().begin(),
                                        app.m_renderer->getRenderList().end(),
                                        [](const auto& item){ return !item.cullingEnabled; });
    require(unculled == 8 && shadows.unculled == unculled * shadows.passes,
            "All eight floors bypass camera, portal and per-light shadow volume rejection");
    const auto averages = app.m_profiler.summarize();
    require(averages.counterSamples == 1 &&
                averages.counters[static_cast<std::size_t>(FrameProfiler::Counter::ColorDraws)] ==
                    app.m_renderer->getStats().draws +
                        (app.m_reflection->getRendered() ? app.m_reflection->getStats().draws : 0),
            "The performance view averages the actual rendered scene counters");
    require(shadows.boundsTested + shadows.unculled == shadows.instances + shadows.rejected &&
                app.m_deferred->getScreenDraws() == 8 &&
                app.m_renderer->getStats().visitedNodes > 0,
            "Profiler counters describe actual shadow rejection and screen submissions");
    const auto header = FramebufferImage::capture(0, height - SceneOverlay::HeaderHeight, width,
                                                  SceneOverlay::HeaderHeight);
    app.handleKey(GLFW_KEY_TAB);
    app.render();
    require(!app.m_showMenus && app.m_profiler.getEnabled() &&
                FramebufferImage::capture(width / 2 - 12, height / 2 - 12, 24, 24).hash() ==
                    reticle.hash() &&
                FramebufferImage::capture(0, height - SceneOverlay::HeaderHeight, width,
                                          SceneOverlay::HeaderHeight)
                        .hash() != header.hash(),
            "Tab hides both menu bars while retaining F1 and the crosshair");
    const auto aa = app.m_antialiasingMode;
    app.handleClick(50, 76);
    require(app.m_antialiasingMode == aa, "Hidden menus do not receive viewport clicks");
    app.handleKey(GLFW_KEY_TAB);
    const auto hide = SceneOverlay::menuToggleRectangle(app);
    app.handleClick(hide.x + 8, hide.y + 8);
    require(!app.m_showMenus, "The menu-hide button and Tab share the same action");
    app.handleKey(GLFW_KEY_TAB);
    app.render();
    app.handleKey(GLFW_KEY_J);
    app.handleClick(450, 126);
    require(app.m_profiler.getEnabled() && app.m_profiler.getHistory().empty() &&
                !app.m_shadowsEnabled && app.m_pointLights == lights &&
                app.m_frustumCulling == culling,
            "Reset averages button clears measurements without changing render settings");
    app.m_profiler.beginFrame(0.02);
    app.render();
    app.m_profiler.endFrame();
    const auto withoutShadows = app.m_profiler.summarize();
    require(
        withoutShadows.samples == 1 &&
            withoutShadows
                    .counters[static_cast<std::size_t>(FrameProfiler::Counter::ShadowDraws)] == 0 &&
            withoutShadows
                    .counters[static_cast<std::size_t>(FrameProfiler::Counter::ShadowPasses)] == 0,
        "Fresh averages reflect shadows disabled without mixing the previous configuration");
    app.handleKey(GLFW_KEY_J);
    app.handleClick(450, 126);
    app.resize(960, 600);
    app.render();
    app.reload();
    require(app.m_profiler.getEnabled(), "Reload preserves the open performance view");
    app.handleKey(GLFW_KEY_F1);
    require(!app.m_profiler.getEnabled(), "F1 also closes the performance view");
    app.handleKey(GLFW_KEY_F1);
    app.handleKey(GLFW_KEY_ESCAPE);
    require(!app.m_profiler.getEnabled() && !glfwWindowShouldClose(app.m_window),
            "Esc closes performance inspection before closing the viewer");
    app.resize(width, height);
    app.render();
    OpenGL::check("Performance controls");
}

// Checks AO, its diagnostic view and the independent forward/deferred switch.
void RenderingValidator::validateAmbientOcclusion(){
    auto& app = m_application;
    using Action = Controls::Action;
    const auto lights = app.m_pointLights;
    const bool ao = app.m_ambientOcclusionEnabled;
    app.render();
    const auto enabled = captureScene();
    app.handleKey(GLFW_KEY_O);
    app.render();
    require(!app.m_deferred->getAmbientOcclusion() &&
                captureScene().countChangedPixels(enabled, 0) > 100,
            "O toggles visible ambient occlusion independently of light switches");
    app.handleClick(690, 28);
    require(app.m_ambientOcclusionEnabled == ao, "AO button and O share the same switch");
    app.handleKey(GLFW_KEY_Z);
    app.handleKey(GLFW_KEY_A);
    app.render();
    require(app.m_bufferView == BufferView::Occlusion && captureScene().hash() != enabled.hash(),
            "AO is available as a separate shader view");
    app.handleKey(GLFW_KEY_F2);
    app.render();
    require(!app.m_deferredEnabled && app.m_bufferView == BufferView::Lit &&
                !app.m_deferred->getPreviews() && app.m_renderer->getStats().draws > 0 &&
                app.m_deferred->getScreenDraws() == 0 &&
                app.performanceCounters()[static_cast<std::size_t>(
                    FrameProfiler::Counter::ScreenDraws)] ==
                    app.m_renderer->getStats().screenDraws +
                        (app.m_reflection->getRendered() ? app.m_reflection->getStats().screenDraws
                                                         : 0),
            "F2 uses the existing forward path without running deferred screen passes");
    const bool bloom = app.m_bloomEnabled;
    app.handleKey(GLFW_KEY_B);
    require(app.m_bloomEnabled == bloom,
            "Inactive deferred effects preserve their settings in forward mode");
    app.reload();
    require(!app.m_deferredEnabled && app.m_ambientOcclusionEnabled == ao,
            "Reload preserves renderer and AO selections");
    app.handleClick(110, 28);
    app.render();
    require(app.m_deferredEnabled && app.m_deferred->getAmbientOcclusion() == ao &&
                app.m_pointLights == lights,
            "Renderer button restores deferred rendering and its settings");
    std::cout
        << "Ambient occlusion controls passed: toggle, isolated view, renderer switch and reload\n";
}

// Checks public buffer controls, post effects, resize and reload persistence.
void RenderingValidator::validateDeferred(){
    auto& app = m_application;
    using Action = Controls::Action;
    app.resetView();
    app.render();
    const auto lit = captureScene();
    const auto lights = app.m_pointLights;
    for (int key = GLFW_KEY_3; key <= GLFW_KEY_9; ++key){
        app.handleKey(GLFW_KEY_Z);
        app.handleKey(key);
        app.render();
        require(app.m_bufferView != BufferView::Lit &&
                    app.m_deferred->getView() == app.m_bufferView && app.m_pointLights == lights &&
                    captureScene().hash() != lit.hash(),
                "Shader menu exposes independent G-buffer views without toggling lights");
    }
    app.handleKey(GLFW_KEY_Z);
    app.handleKey(GLFW_KEY_0);
    require(app.m_bufferView == BufferView::Plane,
            "The geometric normal attachment has its own view");
    app.execute(Action::PhongShader);
    app.handleClick(830, 28);
    app.render();
    require(app.m_bufferPreviews && app.m_deferred->getPreviews() &&
                app.m_deferred->getScreenDraws() == 8 + DeferredRenderer::PreviewCount,
            "The Multiview button displays every channel and counts its presentation draws");
    const auto preview = app.m_deferred->previewRectangle(2);
    app.handleClick(static_cast<float>(preview.x + 8), static_cast<float>(preview.y + 8));
    require(app.m_bufferView == BufferView::Albedo,
            "Clicking a thumbnail expands its actual G-buffer attachment");
    app.handleClick(static_cast<float>(preview.x + 8), static_cast<float>(preview.y + 8));
    require(app.m_bufferView == BufferView::Lit,
            "Clicking the selected thumbnail returns to the lit image");
    app.handleKey(GLFW_KEY_G);
    app.render();
    require(!app.m_bufferPreviews && !app.m_deferred->getPreviews(),
            "G hides the same multiview controlled by the header button");
    app.execute(Action::Renderer);
    app.handleClick(830, 28);
    require(app.m_deferredEnabled && app.m_bufferPreviews,
            "Multiview is available directly from forward rendering");
    app.execute(Action::Performance);
    app.handleClick(830, 28);
    app.render();
    require(!app.m_profiler.getEnabled() && app.m_deferred->getPreviews(),
            "The Multiview button opens visible channel previews from the performance view");
    app.handleClick(830, 28);
    app.handleKey(GLFW_KEY_B);
    app.render();
    require(app.m_deferred->getBloom() && captureScene().countChangedPixels(lit) > 100,
            "B enables visible bloom outside the overlay");
    app.handleClick(530, 28);
    require(!app.m_bloomEnabled && !app.m_deferred->getBloom(),
            "Bloom button and shortcut share the same toggle");
    const auto focusCamera = app.m_scene->getCamera();
    const auto unfocusedView = focusCamera->getView();
    focusCamera->setLookAt({-3.6f, 2.6f, 1.5f},{-4.8f, 2.4f, 0});
    app.render();
    const auto sharpTarget = captureScene();
    app.handleKey(GLFW_KEY_V);
    app.render();
    require(app.m_deferred->getDepthOfField() && app.m_target &&
                app.m_deferred->getFocusDistance() &&
                captureScene().countChangedPixels(sharpTarget) > 100,
            "V enables visible depth of field around the highlighted model");
    focusCamera->setView(unfocusedView);
    app.execute(Action::Bloom);
    app.execute(Action::SpecularBuffer);
    app.reload();
    require(app.m_bufferView == BufferView::Specular &&
                app.m_deferred->getView() == BufferView::Specular && app.m_deferred->getBloom() &&
                app.m_deferred->getDepthOfField(),
            "Reload preserves the selected buffer and each independent post effect");
    app.handleClick(580, 28);
    app.execute(Action::Bloom);
    app.execute(Action::PhongShader);
    const int width = app.m_width, height = app.m_height;
    app.resize(1007, 641);
    app.render();
    require(app.m_deferred->getGBuffer().get(GBuffer::Position)->getWidth() == 1007 &&
                app.m_deferred->getGBuffer().getDepth()->getHeight() == 641,
            "Viewport resize updates the G-buffer and post-processing targets");
    app.resize(width, height);
    app.render();
    std::cout << "Deferred controls passed: all buffer views, thumbnails, bloom, depth of field, "
                 "resize and reload\n";
}

// Checks that lighting, texture and toon controls change scene pixels.
void RenderingValidator::validateLighting(){

    m_application.render();
    OpenGL::check("Initial frame");
    const auto first = captureScene().hash();
    m_application.execute(Controls::Action::GlobalLighting);
    m_application.render();
    const auto unlit = captureScene().hash();
    require(first != unlit, "Lighting toggle must change rendered pixels");
    m_application.execute(Controls::Action::GlobalLighting);
    m_application.render();
    const auto lit = captureScene().hash();
    for (auto action :
         {Controls::Action::PointRed, Controls::Action::PointGreen, Controls::Action::PointBlue,
          Controls::Action::PointWhite, Controls::Action::GroundMultitexture}){
        m_application.execute(action);
        m_application.render();
        require(captureScene().hash() != lit,
                "Light/texture/toon toggle must change scene pixels outside the HUD");
        m_application.execute(action);
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
    m_application.execute(Controls::Action::ObjectAnimation);
    const auto pausedMatrix = animated->getMatrix();
    m_application.render();
    const auto pausedPixels = captureScene();
    m_application.m_updater.update(*m_application.m_scene->getRoot(),
                                   {1.5, 0.5, m_application.m_animationEnabled});
    m_application.render();
    require(animated->getMatrix() == pausedMatrix,
            "Pausing object animation retains every component of the object matrix");
    require(captureScene().countChangedPixels(pausedPixels) == 0,
            "Pausing object animation retains scene pixels within 8-bit rounding tolerance");
    m_application.execute(Controls::Action::ObjectAnimation);
    m_application.render();
    const auto resumePixels = captureScene();
    m_application.m_updater.update(*m_application.m_scene->getRoot(),
                                   {2, 0.5, m_application.m_animationEnabled});
    m_application.render();
    require(animated->getMatrix() != pausedMatrix &&
                captureScene().countChangedPixels(resumePixels) > 100,
            "Resuming object animation updates the transform and its visible pixels");
    auto& app = m_application;
    const bool animationEnabled = app.m_animationEnabled;
    for (const bool enabled :{true, false}){
        app.m_animationEnabled = enabled;
        const auto frozen = animated->getMatrix();
        app.handleKey(GLFW_KEY_F);
        for (unsigned frame = 0; frame < 3; ++frame){
            app.update(glfwGetTime(), 0.5);
        }
        require(app.m_frozenFrustum && animated->getMatrix() == frozen,
                "F keeps the doughnut's complete pose fixed across actual application updates");
        app.handleKey(GLFW_KEY_F);
        require(app.m_animationEnabled == enabled && animated->getMatrix() == frozen,
                "Unfreezing retains the animation preference and does not jump to a new pose");
        app.update(glfwGetTime(), 0.25);
        require((animated->getMatrix() != frozen) == enabled,
                "Only a doughnut that was animating before F resumes its rotation");
    }
    app.m_animationEnabled = true;
    app.handleKey(GLFW_KEY_F);
    const auto frozen = animated->getMatrix();
    app.execute(Controls::Action::ObjectAnimation);
    app.update(glfwGetTime(), 0.5);
    require(animated->getMatrix() == frozen,
            "Changing the animation preference cannot move the doughnut while frozen");
    app.handleKey(GLFW_KEY_F);
    app.update(glfwGetTime(), 0.25);
    require(animated->getMatrix() == frozen,
            "Unfreezing honors an animation pause requested during inspection");
    app.m_animationEnabled = animationEnabled;
    m_application.m_lightOrbitCallback->setEnabled(orbitEnabled);
}

// Checks that light-ring motion is independent of object-animation pause.
void RenderingValidator::validateLightOrbit(){
    auto& app = m_application;
    const auto orbit = m_application.m_scene->findTransform("lightOrbit");
    const auto orbitMatrix = orbit->getMatrix();
    m_application.execute(Controls::Action::PointOrbit);
    m_application.m_updater.update(*m_application.m_scene->getRoot(),{0.9, 0.3, true});
    require(glm::length(orbitMatrix[0] - orbit->getMatrix()[0]) < 0.00001f,
            "Explicit orbit pause retains the light ring's transform");
    m_application.execute(Controls::Action::PointOrbit);
    m_application.m_updater.update(*m_application.m_scene->getRoot(),{1.2, 0.3, false});
    require(glm::length(orbitMatrix[0] - orbit->getMatrix()[0]) > 0.001f,
            "Light ring must rotate while geometry animation is paused");
    const bool originalEnabled = app.m_lightOrbitCallback->getEnabled();
    for (const bool enabled :{true, false}){
        app.m_lightOrbitCallback->setEnabled(enabled);
        const auto frozen = orbit->getMatrix();
        app.handleKey(GLFW_KEY_F);
        require(app.m_frozenFrustum && !app.m_lightOrbitCallback->getEnabled(),
                "F freezes the culling camera and pauses the moving lights together");
        app.m_updater.update(*app.m_scene->getRoot(),{10, 2, false});
        require(orbit->getMatrix() == frozen,
                "Frozen lights and their markers retain their world-space pose as time advances");
        app.handleKey(GLFW_KEY_F);
        require(app.m_lightOrbitCallback->getEnabled() == enabled && orbit->getMatrix() == frozen,
                "Unfreezing restores the previous orbit mode without changing the frozen pose");
        app.m_updater.update(*app.m_scene->getRoot(),{10.1, 0.1, false});
        require((glm::length(frozen[0] - orbit->getMatrix()[0]) > 0.001f) == enabled,
                "Only lights that were moving before F resume their orbit");
    }
    app.m_lightOrbitCallback->setEnabled(true);
    app.handleKey(GLFW_KEY_F);
    app.execute(Controls::Action::PointOrbit);
    require(!app.m_lightOrbitCallback->getEnabled(),
            "Changing the desired orbit mode cannot start the lights while frozen");
    app.handleKey(GLFW_KEY_F);
    require(!app.m_lightOrbitCallback->getEnabled(),
            "Unfreezing honors orbit preferences changed during inspection");
    app.m_lightOrbitCallback->setEnabled(true);
    app.handleKey(GLFW_KEY_F);
    app.resetView();
    require(!app.m_frozenFrustum && app.m_lightOrbitCallback->getEnabled(),
            "Reset exits inspection and restores the previous light animation mode");
    app.handleKey(GLFW_KEY_F);
    app.reload();
    require(!app.m_frozenFrustum && !app.m_frozenLightOrbitEnabled &&
                app.m_lightOrbitCallback->getEnabled(),
            "Reload exits inspection without leaving the replacement light ring paused");
    app.m_lightOrbitCallback->setEnabled(originalEnabled);
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
    m_application.m_scene->getCamera()->setLookAt({6, 2.02f, 8},{6, 2.02f, 0});
    m_application.render();
    const auto fading = m_application.performanceCounters();
    require(fading[static_cast<std::size_t>(FrameProfiler::Counter::LodFadeDraws)] == 2,
            "F1 counts both cow detail draws during a crossfade");
    m_application.m_scene->getCamera()->setLookAt({6, 2.02f, 10},{6, 2.02f, 0});
    m_application.render();
    require(m_application.performanceCounters()[static_cast<std::size_t>(
                FrameProfiler::Counter::LodFadeDraws)] == 0,
            "F1 clears transition draws outside the LOD fade band");
    m_application.execute(Controls::Action::LodTour);
    for (const auto& test : std::vector<std::pair<double, std::size_t>>{
             {0.5, 5u},{2.0, 4u},{2.0, 3u},{2.0, 2u},{2.0, 1u},{2.0, 0u}}){
        m_application.m_updater.update(*m_application.m_scene->getRoot(),{0, test.first, false});
        m_application.render();
        require(m_application.m_renderer->getStats().lodLevels.at("cowLOD") == test.second,
                "LOD camera tour must show all six cow detail levels");
    }
    const auto tourEye = m_application.m_scene->getCamera()->getPosition();
    m_application.execute(Controls::Action::LodTour);
    require(glm::distance(m_application.m_position, tourEye) < 0.0001f,
            "Leaving LOD tour must retain the current camera position");
}

// Checks that reload replaces the graph and preserves lighting controls.
void RenderingValidator::validateReload(){
    auto root = m_application.m_scene->getRoot();
    m_application.execute(Controls::Action::PointGreen);
    m_application.execute(Controls::Action::GlobalLighting);
    m_application.execute(Controls::Action::ReloadScene);
    require(root != m_application.m_scene->getRoot(), "Reload must replace the entire scene");
    require(!m_application.m_scene->getRoot()->getState()->getLighting() &&
                !m_application.m_scene->getRoot()->getState()->getLightEnabled(1),
            "R must consistently preserve global and individual lighting controls");
    m_application.execute(Controls::Action::PointGreen);
    m_application.execute(Controls::Action::GlobalLighting);
}

void RenderingValidator::validateCulling(){
    auto& app = m_application;
    const auto camera = app.m_scene->getCamera();
    const auto originalView = camera->getView();
    const bool originalCulling = app.m_frustumCulling;
    const bool originalHud = app.m_showHud;
    const bool originalDebug = app.m_debugBounds;
    const bool originalRoof = app.m_roofEnabled;
    const bool originalWalls = app.m_transparentWalls;
    const bool originalPortals = app.m_portalCulling;
    app.m_portalCulling = false;
    app.m_showHud = false;
    app.m_debugBounds = false;
    app.m_frozenFrustum.reset();
    unsigned reducedViews = 0;
    unsigned roomDrawsOff = 0, roomDrawsOn = 0;
    std::size_t maxRoundingPixels = 0;
    const std::vector<std::pair<glm::vec3, glm::vec3>> views{
        {{16, 12, 23},{0, 1, -1}},{{-14, 2, -18},{-14, 2, -40}},
        {{0, 2, -23},{14, 2, -23}},{{14, 2, -37},{-14, 2, -37}},
        {{0, 2, -15.9f},{0, 2, -30}},{{0, 18, -30},{0, 0, -30.1f}},
        {{0, 2, -50},{0, 2, -30}},{{0, 2, 8},{0, 2, 40}}};
    for (const bool transparent :{false, true}){
        app.m_transparentWalls = transparent;
        app.applyWallAppearance(*app.m_scene);
        for (const bool roof :{true, false}){
            if (app.m_roofEnabled != roof){
                app.execute(Controls::Action::Roof);
            }
            for (const auto& view : views){
                camera->setLookAt(view.first, view.second);
                if (app.m_frustumCulling){
                    app.execute(Controls::Action::FrustumCulling);
                }
                app.render();
                const auto reference = FramebufferImage::capture(0, 0, app.m_width, app.m_height);
                const auto allDraws = app.m_renderer->getStats().draws;
                app.execute(Controls::Action::FrustumCulling);
                app.render();
                const auto culledImage = FramebufferImage::capture(0, 0, app.m_width, app.m_height);
                const auto changed = culledImage.countChangedPixels(reference, 0);
                const bool preserved =
                    transparent
                        ? culledImage.countChangedPixels(reference, 1) == 0 &&
                              changed <= static_cast<std::size_t>(app.m_width) * app.m_height / 1000
                        : changed == 0;
                if (transparent){
                    maxRoundingPixels = std::max(maxRoundingPixels, changed);
                }
                if (!preserved){
                    reference.save(ResourceLocator::temporaryOutput("vfc-reference.png"));
                    culledImage.save(ResourceLocator::temporaryOutput("vfc-culled.png"));
                    std::cout << "VFC difference at eye " << view.first.x << ',' << view.first.y
                              << ',' << view.first.z << " roof " << roof << ": "
                              << culledImage.countChangedPixels(reference, 0) << " pixels\n";
                }
                require(
                    preserved,
                    "VFC must preserve scene pixels through doors, inside and outside the house");
                require(app.m_renderer->getStats().draws <= allDraws,
                        "VFC must never add geometry draws");
                if (app.m_renderer->getStats().draws < allDraws){
                    ++reducedViews;
                }
                if (roof && view.first == glm::vec3(-14, 2, -18)){
                    roomDrawsOff = allDraws;
                    roomDrawsOn = app.m_renderer->getStats().draws;
                    require(app.m_renderer->getStats().roomDraws.count("C") == 0,
                            "A room wholly outside the frustum must issue zero geometry draws");
                }
            }
        }
    }
    require(reducedViews > 0, "VFC must reduce actual draws in the house/patio scene");
    camera->setLookAt({-14, 2, -18},{-14, 2, -40});
    app.handleKey(GLFW_KEY_F);
    require(app.m_frozenFrustum.has_value() && app.m_debugBounds,
            "F exposes independent culling-camera inspection and its volume visualization");
    const auto frozen = *app.m_frozenFrustum;
    app.execute(Controls::Action::HouseView);
    require(app.m_frozenFrustum && *app.m_frozenFrustum == frozen,
            "Switching viewpoints preserves the frozen culling volume");
    app.execute(Controls::Action::HouseView);
    camera->setLookAt({14, 15, 0},{0, 0, -30});
    const int originalWidth = app.m_width, originalHeight = app.m_height;
    app.resize(1024, 600);
    require(*app.m_frozenFrustum == frozen,
            "Resizing the live projection preserves the frozen view and projection");
    app.render();
    CullVisitor expected;
    expected.collect(*app.m_scene->getRoot(), frozen, camera->getPosition());
    const auto& actual = app.m_renderer->getRenderList();
    require(actual.size() == expected.getRenderList().size(), "Frozen frustum retains its volume");
    for (std::size_t i = 0; i < actual.size(); ++i){
        require(actual[i].geometry == expected.getRenderList()[i].geometry &&
                    actual[i].model == expected.getRenderList()[i].model,
                "Frozen culling uses the original view while LOD uses the live observer");
    }
    app.execute(Controls::Action::DebugBounds);
    app.render();
    OpenGL::check("Bounding boxes and frozen frustum");
    app.execute(Controls::Action::DebugBounds);
    app.handleKey(GLFW_KEY_F);
    require(!app.m_frozenFrustum && !app.m_frozenEye && !app.m_debugBounds,
            "F resumes live culling and clears the frozen inspection overlays");
    app.resize(originalWidth, originalHeight);
    app.execute(Controls::Action::Roof);
    require(app.m_roofEnabled, "H restores the physical roof");
    if (app.m_frustumCulling){
        app.execute(Controls::Action::FrustumCulling);
    }
    app.reload();
    require(!app.m_renderer->getCulling(), "Reload preserves the VFC toggle");
    app.m_frustumCulling = originalCulling;
    app.m_renderer->setCulling(originalCulling);
    app.m_showHud = originalHud;
    app.m_debugBounds = originalDebug;
    if (app.m_roofEnabled != originalRoof){
        app.execute(Controls::Action::Roof);
    }
    app.m_scene->getCamera()->setView(originalView);
    app.m_portalCulling = originalPortals;
    app.m_transparentWalls = originalWalls;
    app.applyWallAppearance(*app.m_scene);
    std::cout << "Room A draws: " << roomDrawsOff << " without VFC -> " << roomDrawsOn
              << " with VFC\n";
    std::cout << "VFC validation: 16 exact opaque pairs, 16 transparent pairs (max "
              << maxRoundingPixels << " pixels differ by at most 1/255); " << reducedViews
              << " views reduced draws; freeze/debug/reload passed\n";
}

void RenderingValidator::validatePortals(){
    auto& app = m_application;
    if (!app.m_scene->findTransform("roomA")){
        return;
    }
    require(app.m_scene->getPortalGraph().getCells().size() == 6 &&
                app.m_scene->getPortalGraph().getPortals().size() == 20,
            "House visibility layout must include six rooms, internal doors and patio entrances");
    const bool originalCulling = app.m_frustumCulling, originalPortals = app.m_portalCulling;
    const bool originalWalls = app.m_transparentWalls, originalRoof = app.m_roofEnabled;
    const bool originalHud = app.m_showHud, originalAnimation = app.m_animationEnabled;
    const bool originalOrbit = app.m_lightOrbitCallback->getEnabled();
    app.m_showHud = false;
    app.m_debugBounds = false;
    app.m_frozenFrustum.reset();
    app.m_frozenEye.reset();
    app.m_transparentWalls = false;
    app.applyWallAppearance(*app.m_scene);
    if (!app.m_roofEnabled){
        app.execute(Controls::Action::Roof);
    }
    unsigned pairs = 0, reduced = 0;
    const std::vector<std::pair<glm::vec3, glm::vec3>> views{
        {{-14, 2, -18},{-14, 2, -40}},{{0, 2, -23},{14, 2, -23}},{{14, 2, -37},{-14, 2, -37}},
        {{-14, 2, -28},{0, 2, -37}},{{0, 2, -15},{0, 2, -30}},{{0, 2, -50},{0, 2, -30}},
        {{32, 2, -30},{0, 2, -30}},{{16, 12, 23},{0, 1, -1}},{{0, 18, -30},{0, 0, -30.1f}},
        {{-14, 2, -16},{-14, 2, -30}}};
    for (const bool frustum :{false, true}){
        app.m_frustumCulling = frustum;
        app.m_renderer->setCulling(frustum);
        for (const auto& view : views){
            app.m_scene->getCamera()->setLookAt(view.first, view.second);
            app.m_portalCulling = false;
            app.render();
            const auto reference = FramebufferImage::capture(0, 0, app.m_width, app.m_height);
            const auto unculled = app.m_renderer->getStats().draws;
            app.execute(Controls::Action::PortalCulling);
            app.render();
            const auto portalImage = FramebufferImage::capture(0, 0, app.m_width, app.m_height);
            if (portalImage.countChangedPixels(reference, 0)){
                reference.save(ResourceLocator::temporaryOutput("portal-reference.png"));
                portalImage.save(ResourceLocator::temporaryOutput("portal-culled.png"));
                std::cout << "Portal difference at eye " << view.first.x << ',' << view.first.y
                          << ',' << view.first.z << " frustum " << frustum << ": "
                          << portalImage.countChangedPixels(reference, 0) << " pixels\n";
            }
            require(portalImage.countChangedPixels(reference, 0) == 0,
                    "Portal culling must preserve exact pixels with physically solid walls");
            if (app.m_renderer->getStats().draws < unculled){
                ++reduced;
            }
            ++pairs;
        }
    }
    require(reduced > 0, "Solid walls must reject actual scene draws");
    app.execute(Controls::Action::Roof);
    for (unsigned index :{7u, 8u}){
        app.m_scene->getCamera()->setLookAt(views[index].first, views[index].second);
        app.m_portalCulling = false;
        app.render();
        const auto reference = FramebufferImage::capture(0, 0, app.m_width, app.m_height);
        app.execute(Controls::Action::PortalCulling);
        app.render();
        require(!app.m_portalResult.active && app.m_portalResult.origin == "open roof fallback",
                "Removing the physical roof retains overhead visibility");
        require(FramebufferImage::capture(0, 0, app.m_width, app.m_height)
                        .countChangedPixels(reference, 0) == 0,
                "Open-roof fallback preserves exact pixels");
        ++pairs;
    }
    app.showCullingDemo();
    app.m_showHud = false;
    app.render();
    require(app.m_portalResult.active && app.m_portalResult.origin == "A",
            "Overhead demo must retain the frozen visibility origin inside A");
    require(app.m_renderer->getStats().roomDraws.count("C") == 0,
            "A solid wall blocks C even with frustum culling disabled");
    const auto visibleRooms = app.m_renderer->getStats().roomDraws;
    const auto list = app.m_renderer->getRenderList();
    unsigned objectDraws = 0;
    for (const auto& entry : visibleRooms){
        if (entry.first != "patio"){
            objectDraws += entry.second;
        }
    }
    const auto transparentImage = FramebufferImage::capture(0, 0, app.m_width, app.m_height);
    require(app.m_renderer->getStats().transparentDraws > 0, "Walls must use a transparent pass");
    app.execute(Controls::Action::TransparentWalls);
    app.render();
    require(app.m_renderer->getStats().roomDraws == visibleRooms,
            "Transparent and solid wall appearances must retain identical visibility");
    const auto& solidList = app.m_renderer->getRenderList();
    require(solidList.size() == list.size(), "Wall transparency cannot change the render list");
    for (std::size_t i = 0; i < list.size(); ++i){
        require(list[i].geometry == solidList[i].geometry && list[i].model == solidList[i].model,
                "Every visible instance must stay identical across wall appearances");
    }
    require(app.m_renderer->getStats().transparentDraws == 0, "O restores solid rasterization");
    app.execute(Controls::Action::TransparentWalls);
    app.execute(Controls::Action::PortalCulling);
    app.render();
    unsigned allObjects = 0;
    for (const auto& entry : app.m_renderer->getStats().roomDraws){
        if (entry.first != "patio"){
            allObjects += entry.second;
        }
    }
    require(allObjects > 0 && objectDraws < allObjects,
            "Portal volumes must prune house exhibits behind solid walls with a fixed observer");
    require(FramebufferImage::capture(0, 0, app.m_width, app.m_height)
                    .countChangedPixels(transparentImage, 0) > 100,
            "Transparent observation view must visibly demonstrate rejected objects");
    app.execute(Controls::Action::PortalCulling);
    app.m_scene->getCamera()->setLookAt({-30, 25, -40},{0, 0, -30});
    app.render();
    require(app.m_renderer->getStats().roomDraws == visibleRooms,
            "Moving the observer keeps frozen portal visibility inside the original room");
    app.m_portalCulling = false;
    app.m_transparentWalls = false;
    app.reload();
    require(!app.m_portalCulling && !app.m_scene->findTransform("walls")->getState()->getBlend(),
            "Reload preserves portal and wall appearance controls");
    app.m_frustumCulling = originalCulling;
    app.m_renderer->setCulling(originalCulling);
    app.m_portalCulling = originalPortals;
    app.m_transparentWalls = originalWalls;
    app.m_roofEnabled = originalRoof;
    app.m_showHud = originalHud;
    app.m_animationEnabled = originalAnimation;
    app.m_lightOrbitCallback->setEnabled(originalOrbit);
    app.applyRenderSettings(*app.m_scene);
    app.resetView();
    std::cout << "Portal validation: " << pairs << " exact solid-wall image pairs; " << reduced
              << " reduced views; demo house object draws " << allObjects << " -> " << objectDraws
              << "; transparency/frozen eye/reload passed\n";
}

// Checks the shadow toggle and the complete lamp-displacement path into the depth map.
void RenderingValidator::validateShadows(){
    auto& app = m_application;
    app.render();
    const auto shadowed = captureScene();
    const auto shadowPasses = app.m_shadowRenderer->getStats().passes;
    unsigned expectedPasses = 0;
    bool directional = false;
    const auto activeLights =
        collectLights(*app.m_scene->getRoot(), app.m_scene->getCamera()->getPosition());
    for (unsigned i = 0; i < activeLights.size(); ++i){
        const auto& light = activeLights[i];
        if (!light.node->getEnabled() || !app.m_scene->getRoot()->getState()->getLightEnabled(i)){
            continue;
        }
        if (light.node->getPosition().w != 0){
            expectedPasses += 6;
        } else if (!directional){
            ++expectedPasses;
            directional = true;
        }
    }
    const auto originalShadows = app.m_shadowsEnabled;
    app.handleKey(GLFW_KEY_J);
    app.render();
    require(app.m_shadowsEnabled != originalShadows &&
                app.m_shadowRenderer->getStats().passes == 0 &&
                captureScene().countChangedPixels(shadowed) > 100,
            "J disables depth passes and visibly removes shadows");
    app.handleClick(360, 28);
    app.render();
    require(app.m_shadowsEnabled == originalShadows && shadowPasses == expectedPasses &&
                app.m_shadowRenderer->getStats().passes == shadowPasses,
            "The shadow button and J share the same action for every enabled scene light");
    // The existing displacement callback is the path used by arrow/Page Up/Down input.
    const auto lightOffset = app.m_lightOrbitCallback->getOffset();
    const bool orbitEnabled = app.m_lightOrbitCallback->getEnabled();
    app.m_lightOrbitCallback->setEnabled(false);
    app.m_updater.update(*app.m_scene->getRoot(),{0, 0, false});
    app.render();
    const auto pointBefore = app.m_shadowRenderer->getPointTarget()->getCameras()[0]->getPosition();
    const auto depthBefore = app.m_shadowRenderer->getPointTarget()->readDepth(0);
    const glm::vec3 lightDisplacement(1.25f, 0.5f, 0.75f);
    app.m_lightOrbitCallback->moveBy(lightDisplacement);
    app.m_updater.update(*app.m_scene->getRoot(),{0, 0, false});
    app.render();
    const auto pointAfter = app.m_shadowRenderer->getPointTarget()->getCameras()[0]->getPosition();
    require(glm::length(pointAfter - pointBefore - lightDisplacement) < 0.0001f &&
                depthBefore != app.m_shadowRenderer->getPointTarget()->readDepth(0),
            "Interactive lamp displacement moves the shadow camera and changes its real depth map");
    app.m_lightOrbitCallback->setOffset(lightOffset);
    app.m_lightOrbitCallback->setEnabled(orbitEnabled);
    app.m_updater.update(*app.m_scene->getRoot(),{0, 0, false});
}

// Checks flashlight illumination, camera tracking, shared controls and reload persistence.
void RenderingValidator::validateFlashlight(){
    auto& app = m_application;
    const auto lights = app.m_pointLights;
    const bool enabled = app.m_flashlight->getEnabled();
    for (unsigned i = 0; i < lights.size(); ++i){
        if (app.m_pointLights[i]){
            app.execute(static_cast<Controls::Action>(
                static_cast<unsigned>(Controls::Action::PointRed) + i));
        }
    }
    app.m_flashlight->setEnabled(false);
    app.lookAt({-14, 2, -20},{-14, 0.3f, -27});
    app.render();
    const auto dark = captureScene();
    const auto baselinePasses = app.m_shadowRenderer->getStats().passes;
    app.handleKey(GLFW_KEY_L);
    app.render();
    require(app.m_flashlight->getEnabled() &&
                app.m_shadowRenderer->getStats().passes == baselinePasses + 6 &&
                captureScene().countChangedPixels(dark) > 100,
            "L illuminates the room with an independently shadowed flashlight");
    const auto beam = app.m_scene->findLight("flashlight");
    const auto position = glm::vec3(beam->getPosition());
    app.lookAt({-13, 2, -20},{-13, 0.3f, -27});
    app.render();
    require(glm::length(glm::vec3(beam->getPosition()) - position - glm::vec3(1, 0, 0)) < 0.0001f &&
                glm::length(beam->getSpotDirection() - app.m_front) < 0.0001f &&
                glm::length(app.m_shadowRenderer->getPointTarget(baselinePasses / 6)
                                ->getCameras()[0]
                                ->getPosition() -
                            glm::vec3(beam->getPosition())) < 0.0001f,
            "Flashlight position, beam and shadow camera follow the live camera");
    app.handleClick(200, 28);
    require(!app.m_flashlight->getEnabled(), "Flashlight button and L use the same action");
    app.handleKey(GLFW_KEY_L);
    app.reload();
    require(app.m_flashlight->getEnabled() && app.m_scene->findLight("flashlight")->getEnabled(),
            "Reload preserves the flashlight switch");
    app.m_flashlight->setEnabled(enabled);
    for (unsigned i = 0; i < lights.size(); ++i){
        if (app.m_pointLights[i] != lights[i]){
            app.execute(static_cast<Controls::Action>(
                static_cast<unsigned>(Controls::Action::PointRed) + i));
        }
    }
    app.resetView();
}

// Checks the shared AA button, average reset and persistence across renderer and scene changes.
void RenderingValidator::validateAntialiasing(){
    auto& app = m_application;
    const bool profiled = app.m_profiler.getEnabled();
    const int width = app.m_width, height = app.m_height;
    const bool deferred = app.m_deferredEnabled;
    const auto initial = app.m_antialiasingMode;
    app.m_profiler.setEnabled(true);
    app.m_profiler.beginFrame(0.016);
    app.m_profiler.endFrame(0.016);
    require(app.m_profiler.summarize().samples > 0, "AA reset starts with existing averages");
    app.handleClick(50, 76);
    require(app.m_antialiasingMode == nextAntialiasing(initial) &&
                app.m_deferred->getAntialiasing() == app.m_antialiasingMode &&
                app.m_profiler.summarize().samples == 0,
            "The AA button advances the technique and clears averages");
    app.render();
    app.handleKey(GLFW_KEY_F3);
    require(app.m_antialiasingMode == nextAntialiasing(nextAntialiasing(initial)),
            "F3 advances AA in the same cycle as the button");
    const auto mode = app.m_antialiasingMode;
    app.execute(Controls::Action::Renderer);
    app.render();
    require(app.m_antialiasingMode == mode, "Renderer switching retains the AA technique");
    app.reload();
    app.render();
    require(app.m_deferred->getAntialiasing() == mode,
            "Scene reload retains AA resources and mode");
    app.resize(961, 601);
    app.render();
    app.resize(width, height);
    app.handleClick(50, 76);
    require(app.m_antialiasingMode == initial, "AA cycles Off, FXAA, MSAA 4x, then Off");
    if (app.m_deferredEnabled != deferred){
        app.execute(Controls::Action::Renderer);
    }
    app.m_profiler.setEnabled(profiled);
}

// Checks that toolbar clicks and compact shortcuts share the same application actions.
void RenderingValidator::validateControls(){
    auto& app = m_application;
    using Action = Controls::Action;
    app.execute(Action::PhongShader);
    app.render();
    const auto phong = captureScene().hash();
    const auto lights = app.m_pointLights;
    app.handleKey(GLFW_KEY_Z);
    require(app.m_shaderMenuOpen, "Z opens the shading menu");
    app.handleClick(
        440, static_cast<float>(app.m_height - SceneOverlay::FooterHeight - 8 -
                                28 * static_cast<int>(Controls::getShaderOptions().size()) + 42));
    app.render();
    require(app.m_shadingMode == ShadingMode::Toon && !app.m_shaderMenuOpen &&
                app.m_renderer->getShadingMode() == ShadingMode::Toon &&
                captureScene().hash() != phong,
            "Shader menu click selects the existing Toon mode for the whole scene");
    require(app.m_pointLights == lights, "Choosing a shader cannot toggle a light");
    app.handleKey(GLFW_KEY_Z);
    app.handleKey(GLFW_KEY_1);
    require(app.m_shadingMode == ShadingMode::Phong && app.m_pointLights == lights,
            "Menu number selection is separate from viewport light shortcuts");
    const float toolbarY = static_cast<float>(app.m_height - SceneOverlay::FooterHeight + 28);
    for (unsigned i = 0; i < app.m_pointLights.size(); ++i){
        app.render();
        const auto lit = captureScene().hash();
        app.handleClick(83 + 64.0f * i, toolbarY);
        app.render();
        require(app.m_pointLights[i] != lights[i] && captureScene().hash() != lit,
                "Every light button must change both light state and rendered illumination");
        app.handleKey(GLFW_KEY_1 + static_cast<int>(i));
        require(app.m_pointLights[i] == lights[i],
                "Light buttons and 1-5 shortcuts share the same action");
    }
    app.handleKey(GLFW_KEY_C);
    require(!app.m_frustumCulling && !app.m_portalCulling, "C disables both visibility stages");
    app.handleClick(610, toolbarY);
    require(app.m_frustumCulling && app.m_portalCulling,
            "Culling button restores both visibility stages");
    app.handleClick(430, toolbarY);
    require(app.m_shaderMenuOpen, "Shader toolbar button opens the same menu as Z");
    app.handleKey(GLFW_KEY_ESCAPE);
    require(!app.m_shaderMenuOpen && !glfwWindowShouldClose(app.m_window),
            "Escape closes a menu before closing the viewer");
    app.execute(Action::ToonShader);
    app.execute(Action::Shadows);
    app.execute(Action::ReloadScene);
    require(!app.m_shadowsEnabled && app.m_scene->getRenderTargets().size() == 2,
            "Reload preserves disabled shadows and recreates scene depth attachments");
    app.execute(Action::Shadows);
    require(app.m_renderer->getShadingMode() == ShadingMode::Toon,
            "Reload preserves the selected shading mode");
    app.execute(Action::PhongShader);
    std::cout << "Viewport controls passed: shader menu, five clickable lights, shared shortcuts, "
                 "culling, shadows, reload\n";
}

} // namespace vr
