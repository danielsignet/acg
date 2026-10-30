#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <glm/gtc/matrix_transform.hpp>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <vr/Application.h>
#include <vr/DotVisitor.h>
#include <vr/GLState.h>
#include <vr/GpuSkinning.h>
#include <vr/OpenGL.h>
#include <vr/RenderingValidator.h>
#include <vr/ResourceLocator.h>
#include <vr/SceneOverlay.h>
#include <vr/SkeletalAnimation.h>

namespace vr{

// Creates the overlay and loads the initial demonstration scene.
Application::Application(GLFWwindow* window, std::filesystem::path root,
                         std::filesystem::path scene, std::filesystem::path dotFile)
    : m_window(window), m_root(std::move(root)), m_sceneFile(std::move(scene)),
      m_dotFile(std::move(dotFile)){
    m_overlay = std::make_unique<SceneOverlay>(m_root);
    reload();
}

// Builds a replacement scene completely before releasing the current scene.
void Application::reload(){

    // Construct the complete replacement before releasing the working scene.
    Reader reader(m_root);
    registerGeometry(reader);
    auto program = std::make_shared<Program>(m_root / "shaders/phong-shading.vs",
                                             m_root / "shaders/phong-shading.fs");
    auto scene = reader.readScene(m_sceneFile);
    applyRenderSettings(*scene);
    auto flashlight =
        std::make_unique<Flashlight>(*scene, m_flashlight && m_flashlight->getEnabled());
    auto lightOrbit = createLightOrbit(*scene);
    auto lodDemo = createLodDemo(*scene);
    auto renderer = std::make_unique<RenderVisitor>(program);
    renderer->setProfiler(&m_profiler);
    renderer->setStateCaching(m_sceneCaching);
    renderer->setCulling(m_frustumCulling);
    renderer->setShadingMode(m_shadingMode);
    auto deferred = std::make_unique<DeferredRenderer>(m_root);
    auto environment = std::make_unique<EnvironmentMap>(
        m_root, "textures/sky/kloofendal_48d_partly_cloudy_puresky_2k.hdr");
    environment->configureRooms(m_root,scene->getEnvironments());
    environment->setReflectionMode(m_reflectionMode);
    renderer->setEnvironment(environment.get());
    deferred->setEnvironment(environment.get());
    if (auto daylight = scene->findLight("houseDaylight")){
        daylight->setPosition(glm::vec4(environment->getSunDirection(), 0));
    }
    auto reflection = std::make_unique<PlanarReflection>(program);
    reflection->setStateCaching(m_sceneCaching);
    renderer->setReflection(reflection.get());
    deferred->setProfiler(&m_profiler);
    deferred->resize(m_width, m_height);
    deferred->setAntialiasing(m_antialiasingMode);
    deferred->setView(m_bufferView);
    deferred->setBloom(m_bloomEnabled);
    deferred->setAmbientOcclusion(m_ambientOcclusionEnabled);
    deferred->setDepthOfField(m_depthOfFieldEnabled);
    auto gpuSkinning =
        m_gpuSkinningEnabled ? std::make_shared<GpuSkinning>(m_root, &m_profiler) : nullptr;
    auto shadows = std::make_unique<ShadowRenderer>(m_root);
    shadows->setCaching(m_shadowCaching);
    shadows->setStateCaching(m_sceneCaching);
    shadows->attach(*scene);
    auto debugLines = std::make_unique<DebugLines>(m_root);
    m_gpuSkinning = std::move(gpuSkinning);
    m_shadowRenderer = std::move(shadows);
    m_flashlight = std::move(flashlight);
    m_target.reset();
    m_debugLines = std::move(debugLines);
    m_renderer = std::move(renderer);
    m_deferred = std::move(deferred);
    m_environment = std::move(environment);
    m_reflection = std::move(reflection);
    m_reflectionProbes.reset();
    m_scene = std::move(scene);
    m_program = std::move(program);
    m_lightOrbitCallback = std::move(lightOrbit);
    m_lodDemoCallback = std::move(lodDemo);
    resetView();
    resize(m_width, m_height);
    m_status = "Scene and shaders reloaded";
    std::cout << "Scene ready: " << m_sceneFile.generic_string() << "\n";
    if (!m_dotFile.empty()){
        exportGraph(m_dotFile);
    }
}

// Exports a debug graph and keeps the scene usable if writing fails.
void Application::exportGraph(const std::filesystem::path& path){
    try{
        DotVisitor::save(*m_scene->getRoot(), path);
        m_status = "Scene graph saved: " + path.string();
        std::cout << m_status << '\n';
    } catch (const std::exception& error){
        // A failed debug export must not prevent rendering or discard a loaded scene.
        m_status = std::string("DOT export failed: ") + error.what();
        std::cerr << m_status << '\n';
    }
}

// Stops the camera tour and restores the scene's initial camera pose.
void Application::resetView(){
    m_shaderMenuOpen = false;
    freezeLightOrbit(false);
    m_frozenFrustum.reset();
    m_frozenEye.reset();
    m_debugBounds = false;
    m_scene->getRoot()->getState()->setUniform("uFog", true);
    m_houseView = false;
    if (m_lodDemoCallback){
        m_lodDemoCallback->setEnabled(false);
    }
    lookAt(m_scene->getInitialEye(), m_scene->getInitialTarget());
    m_status = "Camera reset";
}

// Synchronizes the camera pose used by reset, tours and observation views.
void Application::lookAt(const glm::vec3& eye, const glm::vec3& target){
    m_position = eye;
    m_front = glm::normalize(target - m_position);
    m_pitch = glm::degrees(std::asin(m_front.y));
    m_yaw = glm::degrees(std::atan2(m_front.z, m_front.x));
    m_scene->getCamera()->setLookAt(m_position, m_position + m_front);
}

// Stops the camera tour while retaining its current position and direction.
void Application::stopLodDemo(){
    if (!m_lodDemoCallback || !m_lodDemoCallback->getEnabled()){
        return;
    }
    m_lodDemoCallback->setEnabled(false);
    const auto eye = m_scene->getCamera()->getPosition();
    lookAt(eye, eye - glm::vec3(glm::inverse(m_scene->getCamera()->getView())[2]));
    m_status = "Manual camera";
}

// Temporarily pauses the light ring while preserving its user's animation preference.
void Application::freezeLightOrbit(bool frozen){
    if (frozen){
        if (m_lightOrbitCallback && !m_frozenLightOrbitEnabled.has_value()){
            m_frozenLightOrbitEnabled = m_lightOrbitCallback->getEnabled();
            m_lightOrbitCallback->setEnabled(false);
        }
    } else{
        if (m_lightOrbitCallback && m_frozenLightOrbitEnabled.has_value()){
            m_lightOrbitCallback->setEnabled(*m_frozenLightOrbitEnabled);
        }
        m_frozenLightOrbitEnabled.reset();
    }
}

// Updates the viewport and camera projection for a nonempty framebuffer.
void Application::resize(int width, int height){
    m_width = width;
    m_height = height;
    if (width > 0 && height > 0){
        m_deferred->resize(width, height);
        GLState::viewport(0, 0, width, height);
        m_scene->getCamera()->setPerspective(48, static_cast<float>(width) / height, 0.1f, 150);
    }
}

// Applies the command bound to a pressed key.
void Application::handleKey(int key){
    if (key == GLFW_KEY_ESCAPE && m_profiler.getEnabled()){
        m_profiler.setEnabled(false);
        return;
    }
    if (m_shaderMenuOpen){
        if (key == GLFW_KEY_ESCAPE){
            m_shaderMenuOpen = false;
            return;
        }
        if (const auto* option = Controls::findShader(key)){
            execute(option->action);
            return;
        }
    }
    const auto* binding = Controls::find(key);
    if (!binding){
        return;
    }
    execute(binding->action);
}

// Dispatches a toolbar click expressed in framebuffer coordinates.
void Application::handleClick(float x, float y){
    if (!m_mouseLookActive){
        m_overlay->click(*this, x, y);
    }
}

// Executes the shared action used by keyboard, toolbar and validation paths.
void Application::execute(Controls::Action action){
    using Action = Controls::Action;
    if (m_reflectionProbes &&
        (action == Action::Subdivision || action == Action::CarHeadlights ||
         action == Action::Shadows || action == Action::Flashlight ||
         action == Action::GlobalLighting || Controls::pointLightIndex(action) ||
         action == Action::FreezeFrustum || action == Action::PhongShader ||
         action == Action::BlinnPhongShader || action == Action::CookTorranceShader ||
         action == Action::UnmappedPhongShader || action == Action::ToonShader ||
         action == Action::Roof || action == Action::BumpMapping)){
        m_reflectionProbes->invalidate();
    }
    if (action == Action::Renderer){
        m_deferredEnabled = !m_deferredEnabled;
        m_bufferView = BufferView::Lit;
        m_deferred->setView(m_bufferView);
        m_shaderMenuOpen = false;
        m_status = m_deferredEnabled
                       ? "Deferred rendering"
                       : "Forward rendering; AO, G-buffer views and post effects use Deferred [F2]";
    } else if (action == Action::BumpMapping){
        m_bumpMappingEnabled = !m_bumpMappingEnabled;
        m_scene->getRoot()->getState()->setUniform("uBumpMapping", m_bumpMappingEnabled);
        m_profiler.resetAverages();
        m_status = m_bumpMappingEnabled ? "Height-map bump mapping on; averages reset"
                                        : "Height-map bump mapping off; averages reset";
    } else if (action == Action::Reflections){
        m_reflectionMode = nextReflection(m_reflectionMode);
        m_environment->setReflectionMode(m_reflectionMode);
        m_profiler.resetAverages();
        m_status =
            std::string("Reflections: ") + reflectionName(m_reflectionMode) + "; averages reset";
    } else if (action == Action::Antialiasing){
        try{
            const auto next = nextAntialiasing(m_antialiasingMode);
            m_deferred->setAntialiasing(next);
            m_antialiasingMode = next;
            m_profiler.resetAverages();
            m_status = std::string("Antialiasing: ") + antialiasingName(next) +
                       "; averages reset. Cycle: Off > FXAA > MSAA 4x";
        } catch (const std::exception& error){
            m_status = error.what();
        }
    } else if (action == Action::ModelAnimation){
        if (m_target){
            if (const auto animation = m_target->instance->getAnimation()){
                animation->setClip((animation->getClip() + 1) % animation->getClips().size());
                m_status = "Animation: " + animation->getClips()[animation->getClip()].name;
            }
        }
    } else if (action == Action::CarHeadlights){
        if (!carTargeted()){
            return;
        }
        m_carHeadlightsEnabled = !m_carHeadlightsEnabled;
        applyCarHeadlights(*m_scene);
        m_profiler.resetAverages();
        m_status = m_carHeadlightsEnabled ? "Car headlights on; averages reset"
                                          : "Car headlights off; averages reset";
    } else if (action == Action::Subdivision){
        if (!m_target){
            m_status = "No target";
            return;
        }
        try{
            auto& target = *m_target->instance;
            const auto start = std::chrono::steady_clock::now();
            {
                FrameProfiler::Scope subdivision(&m_profiler, FrameProfiler::Stage::Subdivision);
                target.setSubdivided(!target.getSubdivided());
            }
            m_lastSubdivisionMs =
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
                    .count();
            m_lastSubdivision = target.getName() + (target.getSubdivided() ? " ON" : " OFF");
            m_status =
                target.getName() + (target.getSubdivided() ? ": subdivision ON (cached in memory)"
                                                           : ": original mesh");
        } catch (const std::exception& error){
            m_status = error.what();
        }
    } else if ((action == Action::AmbientOcclusion || action == Action::Bloom ||
                action == Action::DepthOfField) &&
               !m_deferredEnabled){
        m_status = "Switch to Deferred [F2] to use AO, G-buffer views and post effects";
    } else if (action == Action::AmbientOcclusion){
        m_ambientOcclusionEnabled = !m_ambientOcclusionEnabled;
        m_deferred->setAmbientOcclusion(m_ambientOcclusionEnabled);
    } else if (action == Action::Performance){
        m_profiler.setEnabled(!m_profiler.getEnabled());
        m_shaderMenuOpen = false;
    } else if (action == Action::ResetAverages){
        m_profiler.resetAverages();
        m_status = "Performance averages reset";
    } else if (action == Action::Bloom){
        m_bloomEnabled = !m_bloomEnabled;
        m_deferred->setBloom(m_bloomEnabled);
    } else if (action == Action::DepthOfField){
        m_depthOfFieldEnabled = !m_depthOfFieldEnabled;
        m_deferred->setDepthOfField(m_depthOfFieldEnabled);
    } else if (action == Action::BufferPreviews){
        const bool visible =
            m_showMenus && m_deferredEnabled && m_bufferPreviews && !m_profiler.getEnabled();
        m_bufferPreviews = !visible;
        m_showMenus = true;
        m_deferredEnabled = true;
        m_profiler.setEnabled(false);
        m_shaderMenuOpen = false;
    } else if (action >= Action::PositionBuffer && action <= Action::OcclusionBuffer){
        m_deferredEnabled = true;
        if (action == Action::OcclusionBuffer){
            m_ambientOcclusionEnabled = true;
            m_deferred->setAmbientOcclusion(true);
        }
        m_bufferView = static_cast<BufferView>(static_cast<unsigned>(action) -
                                               static_cast<unsigned>(Action::PositionBuffer) + 1);
        m_deferred->setView(m_bufferView);
        m_shaderMenuOpen = false;
    } else if (action == Action::Flashlight){
        m_flashlight->setEnabled(!m_flashlight->getEnabled());
        m_status = m_flashlight->getEnabled() ? "Flashlight on" : "Flashlight off";
    } else if (action == Action::Shadows){
        m_shadowsEnabled = !m_shadowsEnabled;
        m_status = m_shadowsEnabled ? "Shadows enabled" : "Shadows disabled";
    } else if (action == Action::ShaderMenu){
        m_profiler.setEnabled(false);
        m_showMenus = true;
        m_shaderMenuOpen = !m_shaderMenuOpen;
    } else if (action == Action::PhongShader || action == Action::ToonShader ||
               action == Action::UnmappedPhongShader || action == Action::BlinnPhongShader ||
               action == Action::CookTorranceShader){
        m_shadingMode = action == Action::PhongShader          ? ShadingMode::Phong
                        : action == Action::ToonShader         ? ShadingMode::Toon
                        : action == Action::BlinnPhongShader   ? ShadingMode::BlinnPhong
                        : action == Action::CookTorranceShader ? ShadingMode::CookTorrance
                                                               : ShadingMode::UnmappedPhong;
        m_renderer->setShadingMode(m_shadingMode);
        m_bufferView = BufferView::Lit;
        m_deferred->setView(m_bufferView);
        m_shaderMenuOpen = false;
    } else if (action == Action::Culling){
        const bool enabled = !(m_frustumCulling || m_portalCulling);
        m_frustumCulling = enabled;
        m_portalCulling = enabled;
        m_renderer->setCulling(enabled);
    } else if (action == Action::Exit){
        glfwSetWindowShouldClose(m_window, GLFW_TRUE);
    } else if (action == Action::ResetView){
        resetView();
    } else if (action == Action::ReloadScene){
        try{
            reload();
        } catch (const std::exception& e){
            m_status = std::string("Reload failed: ") + e.what();
            std::cerr << m_status << '\n';
        }
    } else if (action == Action::ObjectAnimation){
        m_animationEnabled = !m_animationEnabled;
        m_status = m_animationEnabled ? "Object transform resumed" : "Object transform paused";
    } else if (action == Action::PointOrbit && m_lightOrbitCallback){
        if (m_frozenLightOrbitEnabled.has_value()){
            m_frozenLightOrbitEnabled = !*m_frozenLightOrbitEnabled;
        } else{
            m_lightOrbitCallback->setEnabled(!m_lightOrbitCallback->getEnabled());
        }
    } else if (action == Action::LodTour && m_lodDemoCallback){
        if (m_lodDemoCallback->getEnabled()){
            stopLodDemo();
        } else{
            m_lodDemoCallback->setEnabled(true);
            m_status = "LOD inspection: use the camera or Space to return to manual navigation.";
        }
    } else if (action == Action::GlobalLighting){
        m_lightingEnabled = !m_lightingEnabled;
        m_scene->getRoot()->getState()->setLighting(m_lightingEnabled);
    } else if (action == Action::BackFaceCulling){
        m_cullFaceEnabled = !m_cullFaceEnabled;
        m_scene->getRoot()->getState()->setCullFace(m_cullFaceEnabled);
    } else if (action == Action::GroundMultitexture){
        m_multitextureEnabled = !m_multitextureEnabled;
        m_scene->getRoot()->getState()->setUniform("uMultiTexture", m_multitextureEnabled);
    } else if (action == Action::MonkeyToon){
        m_toonEnabled = !m_toonEnabled;
        if (auto node = m_scene->findTransform("toon")){
            if (node->getState()){
                node->getState()->setUniform("uToon", m_toonEnabled);
            }
        }
    } else if (const auto index = Controls::pointLightIndex(action)){
        auto state = m_scene->getRoot()->getState();
        m_pointLights[*index] = !m_pointLights[*index];
        state->setLightEnabled(*index, m_pointLights[*index]);
    } else if (action == Action::Capture){
        m_screenshotRequested = true;
    } else if (action == Action::ExportGraph){
        exportGraph(m_dotFile.empty() ? ResourceLocator::temporaryOutput("scenegraph.dot")
                                      : m_dotFile);
    } else if (action == Action::FrustumCulling){
        m_frustumCulling = !m_frustumCulling;
        m_renderer->setCulling(m_frustumCulling);
    } else if (action == Action::FreezeFrustum){
        if (m_frozenFrustum){
            m_frozenFrustum.reset();
            m_frozenEye.reset();
        } else{
            const auto& camera = *m_scene->getCamera();
            m_frozenFrustum = camera.getProjection() * camera.getView();
            m_frozenEye = camera.getPosition();
        }
        freezeLightOrbit(m_frozenFrustum.has_value());
        m_debugBounds = m_frozenFrustum.has_value();
        m_status = m_frozenFrustum ? "View and scene animation frozen: move to inspect; F resumes."
                                   : "Culling follows the live camera.";
    } else if (action == Action::DebugBounds){
        m_debugBounds = !m_debugBounds;
    } else if (action == Action::Roof){
        m_roofEnabled = !m_roofEnabled;
        if (auto roof = m_scene->findTransform("roof")){
            roof->setVisible(m_roofEnabled);
        }
    } else if (action == Action::Hud){
        m_showMenus = !m_showMenus;
        if (!m_showMenus){
            m_shaderMenuOpen = false;
        }
    } else if (action == Action::PortalCulling){
        m_portalCulling = !m_portalCulling;
    } else if (action == Action::TransparentWalls){
        m_transparentWalls = !m_transparentWalls;
        applyWallAppearance(*m_scene);
    } else if (action == Action::CullingDemo){
        showCullingDemo();
    } else if (action == Action::HouseView){
        if (const auto room = m_scene->findTransform("roomA")){
            stopLodDemo();
            m_houseView = !m_houseView;
            m_position = m_houseView ? glm::vec3(room->getMatrix() * glm::vec4(0, 2, 5, 1))
                                     : m_scene->getInitialEye();
            m_front = m_houseView ? glm::vec3(0, 0, -1)
                                  : glm::normalize(m_scene->getInitialTarget() - m_position);
            lookAt(m_position, m_position + m_front);
            m_status = m_houseView ? "Inside room A" : "Patio overview";
        }
    }
}

// Returns and clears the pending screenshot request.
bool Application::consumeScreenshotRequest(){
    const bool value = m_screenshotRequested;
    m_screenshotRequested = false;
    return value;
}

// Updates camera orientation and movement from the current input state.
void Application::input(float delta){
    updateCameraOrientation();
    moveCamera(delta);
    moveLights(delta);
    m_scene->getCamera()->setLookAt(m_position, m_position + m_front);
}

// Processes input, callbacks and frame-rate statistics.
void Application::update(double time, double delta){
    {
        FrameProfiler::Scope inputScope(&m_profiler, FrameProfiler::Stage::Input);
        input(static_cast<float>(delta));
    }
    if (m_gpuSkinning){
        m_gpuSkinning->beginFrame();
    }
    m_updater.update(*m_scene->getRoot(),
                     {time, delta, m_animationEnabled && !m_frozenFrustum.has_value(), &m_profiler,
                      m_gpuSkinning});
    ++m_frameCount;
    m_frameTimeSeconds += m_lastFrameTime > 0 ? time - m_lastFrameTime : 0;
    m_lastFrameTime = time;
    if (m_frameTimeSeconds >= 1){
        m_framesPerSecond = m_frameCount / m_frameTimeSeconds;
        m_frameCount = 0;
        m_frameTimeSeconds = 0;
        std::ostringstream title;
        title << WindowTitle << " | " << std::fixed << std::setprecision(0) << m_framesPerSecond
              << " FPS";
        glfwSetWindowTitle(m_window, title.str().c_str());
    }
}

// Draws the 3D scene and its overlay into the current framebuffer.
void Application::render(){
    if (m_width <= 0 || m_height <= 0){
        return;
    }
    FrameProfiler::Scope render(&m_profiler, FrameProfiler::Stage::Render);
    GLState::enable(GL_DEPTH_TEST);
    GLState::depthMask(GL_TRUE);
    GLState::disable(GL_BLEND);
    GLState::frontFace(GL_CCW);
    GLState::disable(GL_MULTISAMPLE);
    GLState::disable(GL_SAMPLE_SHADING);
    glCullFace(GL_BACK);
    const auto& camera = *m_scene->getCamera();
    const auto vp = camera.getProjection() * camera.getView();
    const auto* frozen = m_frozenFrustum ? &*m_frozenFrustum : nullptr;
    m_flashlight->update(camera);
    {
        FrameProfiler::Scope portals(&m_profiler, FrameProfiler::Stage::Portals);
        updatePortalVisibility(camera, vp);
    }
    // Plan auxiliary observers before shadow maps, so their animations and
    // deformations use the same pose in every pass of this frame.
    {
        FrameProfiler::Scope preparation(&m_profiler, FrameProfiler::Stage::Submission);
        m_reflection->setEnabled(m_reflectionMode != ReflectionMode::Off);
        m_reflection->resize(m_width, m_height, m_antialiasingMode);
        if (const auto reflected = m_reflection->plan(*m_scene, camera))
            m_renderer->prepareFrameView(*m_scene->getRoot(), *reflected, true);
        if (m_reflectionMode == ReflectionMode::Cubemaps){
            std::string room;
            for (const auto& region : m_scene->getEnvironments().getRegions())
                if (region.contains(camera.getPosition())) {room = region.room; break;}
            if (!m_reflectionProbes || room != m_reflectionProbeRoom){
                m_reflectionProbes = std::make_unique<ReflectionProbes>(*m_scene, *m_environment, m_program);
                m_reflectionProbeRoom = room;
                m_reflectionProbes->setStateCaching(m_sceneCaching);
            }
            const bool dynamic = !m_frozenEye.has_value() &&
                (m_updater.hasDynamicUpdates() || m_renderer->dynamicEnvironment(*m_scene->getRoot()) ||
                 m_flashlight->getEnabled());
            const auto capture = m_reflectionProbes->plan(camera, m_portalResult, dynamic,
                [&](const std::string& id){ return m_renderer->roomVisible(*m_scene->getRoot(), id, camera); });
            if (capture) m_renderer->prepareFrameView(*m_scene->getRoot(), *capture);
        }
    }
    if (m_gpuSkinning) m_gpuSkinning->prepare();
    {
        FrameProfiler::Scope shadows(&m_profiler, FrameProfiler::Stage::Shadows);
        m_shadowRenderer->render(*m_scene, camera.getPosition(), m_shadowsEnabled, true);
    }
    m_deferred->setPreviews(m_deferredEnabled && m_showHud && m_showMenus && m_bufferPreviews &&
                            !m_profiler.getEnabled());
    {
        FrameProfiler::Scope pass(&m_profiler, FrameProfiler::Stage::Cubemaps);
        if (m_reflectionMode == ReflectionMode::Cubemaps){
            m_reflectionProbes->update(*m_scene, camera, *m_environment, m_shadingMode,
                                       m_portalResult, m_updater.hasDynamicUpdates());
        } else if (m_reflectionProbes){
            m_reflectionProbes->idle();
        }
    }
    {
        FrameProfiler::Scope pass(&m_profiler, FrameProfiler::Stage::Reflection);
        m_reflection->setEnabled(m_reflectionMode != ReflectionMode::Off);
        m_reflection->resize(m_width, m_height, m_antialiasingMode);
        m_reflection->render(*m_scene, camera, m_environment.get(), m_shadingMode, m_frustumCulling,
                             m_portalCulling);
    }
    const auto draw = [&]{
        m_renderer->render(
            *m_scene->getRoot(), camera, frozen, &m_portalResult,
            m_deferredEnabled ? m_deferred.get() : nullptr, [&]{
                FrameProfiler::Scope picking(&m_profiler, FrameProfiler::Stage::Picking);
                float surfaceDistance=0;
                m_target =
                    pickModel(m_renderer->getRenderList(), camera.getPosition(),
                              glm::normalize(glm::vec3(glm::inverse(camera.getView())[2]) * -1.0f),&surfaceDistance);
                const auto& projection=camera.getProjection();
                const float farPlane=std::abs(projection[3][2]/(projection[2][2]+1));
                m_deferred->setFocusDistance(std::isfinite(surfaceDistance)?surfaceDistance:farPlane);
            });
    };
    if (m_deferredEnabled){
        draw();
    } else{
        // Deferred and AA passes overwrite the final color and depth. Touching the window
        // buffer before offscreen work can serialize submission with the previous swap.
        if (m_antialiasingMode == AntialiasingMode::Off){
            glClearColor(0.035f, 0.052f, 0.078f, 1);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        }
        m_deferred->renderForward(draw);
    }
    {
        FrameProfiler::Scope debug(&m_profiler, FrameProfiler::Stage::Debug);
        if (m_target && m_showHud && !m_profiler.getEnabled()){
            m_debugLines->box(m_target->instance->getBound().transformed(m_target->model), vp,
                              {1, 0.75f, 0.18f, 1});
        }
        drawVisibilityDebug(camera, vp);
    }
    if (m_showHud || m_profiler.getEnabled()){
        FrameProfiler::Scope hud(&m_profiler, FrameProfiler::Stage::Overlay);
        if (m_profiler.getEnabled()){
            m_profiler.recordCounters(performanceCounters());
        }
        overlay();
    }
    if (m_gpuSkinning){
        m_gpuSkinning->endFrame();
    }
    OpenGL::check("Frame");
}

// Updates portal visibility from the live or frozen eye independently of the observer.
void Application::updatePortalVisibility(const Camera& camera, const glm::mat4& viewProjection){
    const auto portalStart = std::chrono::steady_clock::now();
    m_portalResult ={};
    if (m_portalCulling){
        const auto eye = m_frozenEye.value_or(camera.getPosition());
        const auto& graph = m_scene->getPortalGraph();
        const bool aboveOpenRoof =
            !m_roofEnabled &&
            std::any_of(graph.getCells().begin(), graph.getCells().end(), [&](const auto& cell){
                const auto& maximum = cell.bounds.getMaximum();
                return eye.y >= maximum.y;
            });
        if (aboveOpenRoof){
            m_portalResult.origin = "open roof fallback";
        } else{
            m_portalResult = graph.collect(
                eye, m_frustumCulling ? (m_frozenFrustum ? &*m_frozenFrustum : &viewProjection)
                                      : nullptr);
        }
    } else{
        m_portalResult.origin = "disabled";
    }
    m_portalCpuMs =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - portalStart)
            .count();
}

// Draws doorway, eye and bounding-volume references from the observer camera.
void Application::drawVisibilityDebug(const Camera& camera,
                                      const glm::mat4& observerViewProjection){
    if (m_frozenEye && !m_frustumCulling){
        const auto& portals = m_scene->getPortalGraph().getPortals();
        for (std::size_t i = 0; i < portals.size(); i += 2){
            BoundingBox opening;
            for (const auto& corner : portals[i].corners){
                opening.expandBy(corner);
            }
            m_debugLines->box(opening, observerViewProjection,{0, 0.8f, 1, 1});
        }
        m_debugLines->box({*m_frozenEye - glm::vec3(0.2f), *m_frozenEye + glm::vec3(0.2f)},
                          observerViewProjection,{1, 0.8f, 0.1f, 1});
    }
    if (m_debugBounds){
        CullVisitor all;
        all.collect(*m_scene->getRoot(), observerViewProjection, camera.getPosition(), false);
        const Frustum frustum(m_frozenFrustum.value_or(observerViewProjection));
        for (const auto& item : all.getRenderList()){
            const auto bound = item.geometry->getBound().transformed(item.model);
            const auto visible = frustum.classify(bound);
            const glm::vec4 color = visible == Visibility::Outside  ? glm::vec4(1, 0.25f, 0.2f, 1)
                                    : visible == Visibility::Inside ? glm::vec4(0.2f, 1, 0.5f, 1)
                                                                    : glm::vec4(1, 0.8f, 0.2f, 1);
            m_debugLines->box(bound, observerViewProjection, color);
        }
        if (m_frozenFrustum){
            m_debugLines->frustum(*m_frozenFrustum, observerViewProjection);
        }
    }
}

// Draws the application HUD after rendering the 3D scene.
void Application::overlay(){
    m_overlay->draw(*this);
}

// Collects submission, culling and attachment counters for this scene frame.
FrameProfiler::Counters Application::performanceCounters() const{
    using Counter = FrameProfiler::Counter;
    FrameProfiler::Counters counters{};
    const auto set = [&](Counter counter, double value){
        counters[static_cast<std::size_t>(counter)] = value;
    };
    const auto& scene = m_renderer->getStats();
    const auto& shadows = m_shadowRenderer->getStats();
    const auto& reflected = m_reflection->getStats();
    const RenderStats probes = m_reflectionProbes ? m_reflectionProbes->getStats() : RenderStats{};
    const unsigned filterDraws = m_reflectionProbes ? m_reflectionProbes->getFilterDraws() : 0;
    const unsigned mirrorDraws = m_reflection->getRendered() ? reflected.draws : 0;
    const auto mirrorTriangles = m_reflection->getRendered() ? reflected.triangles : 0;
    set(Counter::MirrorDraws, mirrorDraws);
    set(Counter::ProbeDraws, probes.draws);
    set(Counter::ProbeFaces, m_reflectionProbes ? m_reflectionProbes->getFaces() : 0);
    set(Counter::ReadyProbes, m_reflectionProbes ? m_reflectionProbes->getReadyCount() : 0);
    set(Counter::ScreenCopies, scene.screenCopies +
                                   (m_reflection->getRendered() ? reflected.screenCopies : 0) +
                                   probes.screenCopies);
    set(Counter::ColorDraws, scene.draws + mirrorDraws + probes.draws);
    set(Counter::ColorTriangles,
        static_cast<double>(scene.triangles + mirrorTriangles + probes.triangles));
    set(Counter::TransparentDraws,
        scene.transparentDraws + (m_reflection->getRendered() ? reflected.transparentDraws : 0) +
            probes.transparentDraws);
    set(Counter::ShadowDraws, shadows.draws);
    set(Counter::ShadowTriangles, static_cast<double>(shadows.triangles));
    set(Counter::ShadowPasses, shadows.passes);
    set(Counter::ShadowCacheHits, shadows.cacheHits);
    set(Counter::ShadowReusedFaces, shadows.reusedFaces);
    set(Counter::ShadowCacheRebuilds, shadows.staticRebuilds);
    set(Counter::TotalDraws, scene.draws + mirrorDraws + shadows.draws + probes.draws);
    set(Counter::TotalTriangles, static_cast<double>(scene.triangles + mirrorTriangles +
                                                     shadows.triangles + probes.triangles));
    set(Counter::ScreenDraws, m_deferred->getScreenDraws() + scene.screenDraws +
                                  (m_reflection->getRendered() ? reflected.screenDraws : 0) +
                                  probes.screenDraws + filterDraws);
    set(Counter::Lights, scene.lights);
    set(Counter::SkinnedVertices, static_cast<double>(m_updater.getSkinnedVertices()));
    set(Counter::SkinInfluences, static_cast<double>(m_updater.getSkinInfluences()));
    if (m_gpuSkinning){
        const auto& skin = m_gpuSkinning->getStats();
        set(Counter::GpuSkinnedVertices, static_cast<double>(skin.vertices));
        set(Counter::GpuSkinningDraws, static_cast<double>(skin.draws));
        set(Counter::CpuSkinPositions, static_cast<double>(skin.cpuPositions));
    }
    set(Counter::VisitedNodes, scene.visitedNodes);
    set(Counter::CameraBounds, scene.boundsTested);
    set(Counter::FrustumRejected, scene.subtreesRejected);
    set(Counter::PortalRejected, scene.portalRejected);
    set(Counter::ShadowBounds, shadows.boundsTested);
    set(Counter::ShadowRejected, shadows.rejected);
    const auto& cells = m_scene->getPortalGraph().getCells();
    set(Counter::VisibleRooms,
        static_cast<double>(std::count_if(cells.begin(), cells.end(), [&](const auto& cell){
            return m_portalResult.visible(cell.id);
        })));
    set(Counter::PortalsTested, m_portalResult.portalsTested);
    const int surfaceCopies = m_antialiasingMode == AntialiasingMode::Msaa ? 5 : 1;
    set(Counter::GBufferMiB,
        static_cast<double>(m_width) * m_height * 64 * surfaceCopies / (1024 * 1024));
    set(Counter::LodFadeDraws,
        static_cast<double>(std::count_if(m_renderer->getRenderList().begin(),
                                          m_renderer->getRenderList().end(), [](const auto& item){
                                              const auto range =
                                                  item.state.getUniforms().find("uLodRange");
                                              return range != item.state.getUniforms().end() &&
                                                     std::get<glm::vec2>(range->second) !=
                                                         glm::vec2(0, 1);
                                          })));
    return counters;
}

// Runs the isolated framebuffer and scene-behavior regression checks.
void Application::validate(){
    RenderingValidator(*this).run();
}

// Registers the procedural meshes referenced by the demonstration scene.
void Application::registerGeometry(Reader& reader){
    reader.registerNode("ground", GeometryFactory::createGround());
    reader.registerNode("box", GeometryFactory::createBox());
    reader.registerNode("sphere-low", GeometryFactory::createSphere(8, 6));
    reader.registerNode("material-sphere", GeometryFactory::createSphere(64, 48));
    reader.registerNode("torus", GeometryFactory::createTorus(64, 24));
    reader.registerNode("alphatest-panel", GeometryFactory::createAlphaTestPanel());
}

// Restores the user's rendering toggles on a loaded scene.
void Application::applyRenderSettings(Scene& scene) const{
    applyCarHeadlights(scene);
    applyWallAppearance(scene);
    if (!scene.getRoot()->getState()){
        scene.getRoot()->setState(std::make_shared<State>());
    }
    scene.getRoot()->getState()->setLighting(m_lightingEnabled);
    for (unsigned i = 0; i < m_pointLights.size(); ++i){
        scene.getRoot()->getState()->setLightEnabled(i, m_pointLights[i]);
    }
    scene.getRoot()->getState()->setCullFace(m_cullFaceEnabled);
    scene.getRoot()->getState()->setUniform("uMultiTexture", m_multitextureEnabled);
    scene.getRoot()->getState()->setUniform("uBumpMapping", m_bumpMappingEnabled);
    if (auto roof = scene.findTransform("roof")){
        roof->setVisible(m_roofEnabled);
    }
    if (auto toon = scene.findTransform("toon")){
        if (toon->getState()){
            toon->getState()->setUniform("uToon", m_toonEnabled);
        }
    }
}

// Reports whether the selected instance belongs to the car exhibit.
bool Application::carTargeted() const{
    const auto car = m_scene->findTransform("carExhibit");
    return car && m_target &&
           std::any_of(car->getChildren().begin(), car->getChildren().end(),
                       [&](const auto& child){ return child.get() == m_target->instance; });
}

// Keeps the car's beams and emissive headlamp surfaces on the same switch.
void Application::applyCarHeadlights(Scene& scene) const{
    for (const auto name :{"carHeadlightLeft", "carHeadlightRight"}){
        if (const auto light = scene.findLight(name)){
            light->setEnabled(m_carHeadlightsEnabled);
        }
    }
    if (const auto car = scene.findTransform("carExhibit")){
        if (!car->getState()){
            car->setState(std::make_shared<State>());
        }
        car->getState()->setUniform("uEmissionEnabled", m_carHeadlightsEnabled);
    }
}

// Changes wall appearance while retaining its solid portal boundaries.
void Application::applyWallAppearance(Scene& scene) const{
    if (const auto walls = scene.findTransform("walls")){
        const auto& state = walls->getState();
        state->setBlend(m_transparentWalls);
        state->setCullFace(true);
        state->setUniform("uOpacity", m_transparentWalls ? 0.12f : 1.0f);
    }
}

// Starts the overhead inspection preset with visibility frozen inside room A.
void Application::showCullingDemo(){
    const auto room = m_scene->findTransform("roomA");
    if (!room){
        return;
    }
    stopLodDemo();
    const auto eye = glm::vec3(room->getMatrix() * glm::vec4(0, 2, 5, 1));
    const auto& camera = m_scene->getCamera();
    camera->setLookAt(eye, eye + glm::vec3(0, 0, -1));
    m_frozenFrustum = camera->getProjection() * camera->getView();
    m_frozenEye = eye;
    m_scene->getRoot()->getState()->setUniform("uFog", false);
    m_portalCulling = true;
    m_frustumCulling = false;
    m_renderer->setCulling(false);
    m_transparentWalls = true;
    applyWallAppearance(*m_scene);
    m_roofEnabled = false;
    if (const auto roof = m_scene->findTransform("roof")){
        roof->setVisible(false);
    }
    m_animationEnabled = false;
    freezeLightOrbit(true);
    lookAt({28, 32, 16},{0, 0, -26});
    m_status = "Observation view: visibility frozen inside A. Doorways are cyan; walls remain "
               "solid to culling.";
}

// Attaches the light-ring animation while preserving its user settings.
std::shared_ptr<OrbitCallback> Application::createLightOrbit(Scene& scene) const{
    std::shared_ptr<OrbitCallback> lightOrbit;
    if (auto orbit = scene.findTransform("lightOrbit")){
        lightOrbit = std::make_shared<OrbitCallback>(orbit, 38.0f);
        if (m_lightOrbitCallback){
            lightOrbit->setEnabled(m_lightOrbitCallback->getEnabled());
            lightOrbit->setOffset(m_lightOrbitCallback->getOffset());
        }
        orbit->setUpdateCallback(lightOrbit);
    }
    return lightOrbit;
}

// Attaches the camera tour around the cow's world-space center.
std::shared_ptr<LodDemoCallback> Application::createLodDemo(Scene& scene) const{
    std::shared_ptr<LodDemoCallback> lodDemo;
    if (auto cow = scene.findTransform("cowDisplay")){
        const auto center = glm::vec3(cow->getMatrix() * glm::vec4(0, 140, 0, 1));
        lodDemo = std::make_shared<LodDemoCallback>(scene.getCamera(), center);
        scene.getCamera()->setUpdateCallback(lodDemo);
    }
    return lodDemo;
}

// Updates mouse look and hands camera-tour control back to the user.
void Application::updateCameraOrientation(){

    const bool look = glfwGetMouseButton(m_window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
    if (look){
        m_shaderMenuOpen = false;
        stopLodDemo();
    }
    for (int key :{GLFW_KEY_W, GLFW_KEY_A, GLFW_KEY_S, GLFW_KEY_D, GLFW_KEY_Q, GLFW_KEY_E}){
        if (glfwGetKey(m_window, key) == GLFW_PRESS){
            stopLodDemo();
        }
    }
    double x;
    double y;
    glfwGetCursorPos(m_window, &x, &y);
    if (look && m_mouseLookActive){
        m_yaw += static_cast<float>(x - m_mouseX) * 0.12f;
        m_pitch = std::clamp(m_pitch - static_cast<float>(y - m_mouseY) * 0.12f, -85.0f, 85.0f);
    }
    if (look != m_mouseLookActive){
        glfwSetInputMode(m_window, GLFW_CURSOR, look ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
    }
    m_mouseLookActive = look;
    m_mouseX = x;
    m_mouseY = y;
    m_front =
        glm::normalize(glm::vec3(std::cos(glm::radians(m_pitch)) * std::cos(glm::radians(m_yaw)),
                                 std::sin(glm::radians(m_pitch)),
                                 std::cos(glm::radians(m_pitch)) * std::sin(glm::radians(m_yaw))));
}

// Moves the camera along its local axes using the current keyboard state.
void Application::moveCamera(float delta){
    if (!m_mouseLookActive){
        return;
    }
    const auto right = glm::normalize(glm::cross(m_front, glm::vec3(0, 1, 0)));
    const float speed = movementSpeed(delta);
    if (glfwGetKey(m_window, GLFW_KEY_W) == GLFW_PRESS){
        m_position += m_front * speed;
    }
    if (glfwGetKey(m_window, GLFW_KEY_S) == GLFW_PRESS){
        m_position -= m_front * speed;
    }
    if (glfwGetKey(m_window, GLFW_KEY_A) == GLFW_PRESS){
        m_position -= right * speed;
    }
    if (glfwGetKey(m_window, GLFW_KEY_D) == GLFW_PRESS){
        m_position += right * speed;
    }
    if (glfwGetKey(m_window, GLFW_KEY_Q) == GLFW_PRESS){
        m_position.y -= speed;
    }
    if (glfwGetKey(m_window, GLFW_KEY_E) == GLFW_PRESS){
        m_position.y += speed;
    }
}

// Moves the existing light ring along world axes using arrows and Page Up/Down.
void Application::moveLights(float delta){
    if (!m_lightOrbitCallback || m_frozenFrustum){
        return;
    }
    glm::vec3 displacement(0);
    if (glfwGetKey(m_window, GLFW_KEY_LEFT) == GLFW_PRESS){
        displacement.x -= 1;
    }
    if (glfwGetKey(m_window, GLFW_KEY_RIGHT) == GLFW_PRESS){
        displacement.x += 1;
    }
    if (glfwGetKey(m_window, GLFW_KEY_UP) == GLFW_PRESS){
        displacement.z -= 1;
    }
    if (glfwGetKey(m_window, GLFW_KEY_DOWN) == GLFW_PRESS){
        displacement.z += 1;
    }
    if (glfwGetKey(m_window, GLFW_KEY_PAGE_UP) == GLFW_PRESS){
        displacement.y += 1;
    }
    if (glfwGetKey(m_window, GLFW_KEY_PAGE_DOWN) == GLFW_PRESS){
        displacement.y -= 1;
    }
    m_lightOrbitCallback->moveBy(displacement * movementSpeed(delta));
}

// Returns the frame-scaled normal or fast movement distance.
float Application::movementSpeed(float delta) const{
    return (glfwGetKey(m_window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS ? 24.0f : 10.0f) * delta;
}

} // namespace vr
