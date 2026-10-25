#include <vr/ResourceLocator.h>
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
    validateControls();
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
    OpenGL::check("Validation complete");
    std::cout << "GPU validation passed: scene pixels for lighting/lights/multitexture/toon/alpha, "
                 "animation/pause, all LOD levels, reload, reset, frustum culling, OpenGL errors\n";
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
            "P must pause every component of the object matrix");
    require(captureScene().countChangedPixels(pausedPixels) == 0,
            "P pause must retain scene pixels within 8-bit rounding tolerance");
    m_application.execute(Controls::Action::ObjectAnimation);
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
    m_application.execute(Controls::Action::PointOrbit);
    m_application.m_updater.update(*m_application.m_scene->getRoot(),{0.9, 0.3, true});
    require(glm::length(orbitMatrix[0] - orbit->getMatrix()[0]) < 0.00001f,
            "L must pause the light ring independently of P");
    m_application.execute(Controls::Action::PointOrbit);
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
    app.execute(Controls::Action::FreezeFrustum);
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
    app.execute(Controls::Action::FreezeFrustum);
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
    require(allObjects == 96 && objectDraws < allObjects,
            "U must prune house objects behind solid walls with a fixed observer");
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

void RenderingValidator::validateControls(){
    auto& app = m_application;
    using Action = Controls::Action;
    app.execute(Action::PhongShader);
    app.render();
    const auto phong = captureScene().hash();
    const auto lights = app.m_pointLights;
    app.handleKey(GLFW_KEY_Z);
    require(app.m_shaderMenuOpen, "Z opens the shading menu");
    app.handleClick(440, static_cast<float>(app.m_height - SceneOverlay::FooterHeight - 24));
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
    app.execute(Action::ReloadScene);
    require(app.m_renderer->getShadingMode() == ShadingMode::Toon,
            "Reload preserves the selected shading mode");
    app.execute(Action::PhongShader);
    std::cout << "Viewport controls passed: shader menu, five clickable lights, shared shortcuts, "
                 "culling, reload\n";
}

} // namespace vr
