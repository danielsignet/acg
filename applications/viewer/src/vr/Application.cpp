#include <algorithm>
#include <cmath>
#include <cstdint>
#include <glm/gtc/matrix_transform.hpp>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <vr/Application.h>
#include <vr/DotVisitor.h>
#include <vr/OpenGL.h>
#include <vr/RenderingValidator.h>
#include <vr/SceneOverlay.h>

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
    auto lightOrbit = createLightOrbit(*scene);
    auto lodDemo = createLodDemo(*scene);
    auto renderer = std::make_unique<RenderVisitor>(program);
    m_renderer = std::move(renderer);
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
    if (m_lodDemoCallback){
        m_lodDemoCallback->setEnabled(false);
    }
    m_position = m_scene->getInitialEye();
    m_front = glm::normalize(m_scene->getInitialTarget() - m_position);
    m_pitch = glm::degrees(std::asin(m_front.y));
    m_yaw = glm::degrees(std::atan2(m_front.z, m_front.x));
    m_scene->getCamera()->setLookAt(m_position, m_position + m_front);
    m_status = "Camera reset";
}

// Stops the camera tour while retaining its current position and direction.
void Application::stopLodDemo(){
    if (!m_lodDemoCallback || !m_lodDemoCallback->getEnabled()){
        return;
    }
    m_lodDemoCallback->setEnabled(false);
    m_position = m_scene->getCamera()->getPosition();
    m_front = glm::normalize(-glm::vec3(glm::inverse(m_scene->getCamera()->getView())[2]));
    m_pitch = glm::degrees(std::asin(m_front.y));
    m_yaw = glm::degrees(std::atan2(m_front.z, m_front.x));
    m_status = "Manual camera";
}

// Updates the viewport and camera projection for a nonempty framebuffer.
void Application::resize(int width, int height){
    m_width = width;
    m_height = height;
    if (width > 0 && height > 0){
        glViewport(0, 0, width, height);
        m_scene->getCamera()->setPerspective(48, static_cast<float>(width) / height, 0.1f, 150);
    }
}

// Applies the command bound to a pressed key.
void Application::handleKey(int key){
    const auto* binding = Controls::find(key);
    if (!binding){
        return;
    }
    const auto action = binding->action;
    using Action = Controls::Action;
    if (action == Action::Exit){
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
        m_lightOrbitCallback->setEnabled(!m_lightOrbitCallback->getEnabled());
    } else if (action == Action::LodTour && m_lodDemoCallback){
        if (m_lodDemoCallback->getEnabled()){
            stopLodDemo();
        } else{
            m_lodDemoCallback->setEnabled(true);
            m_status =
                "LOD tour: all six cow levels. V or camera controls to stop; Space for overview.";
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
        exportGraph(m_dotFile.empty() ? m_root / "verification/scenegraph.dot" : m_dotFile);
    }
}

// Returns and clears the pending screenshot request.
bool Application::consumeScreenshotRequest(){
    const bool value = m_screenshotRequested;
    m_screenshotRequested = false;
    return value;
}

// Updates camera movement and interactive light-ring movement.
void Application::input(float delta){
    updateCameraOrientation();
    moveCamera(delta);
    moveLightRing(delta);
    m_scene->getCamera()->setLookAt(m_position, m_position + m_front);
}

// Processes input, callbacks and frame-rate statistics.
void Application::update(double time, double delta){
    input(static_cast<float>(delta));
    m_updater.update(*m_scene->getRoot(),{time, delta, m_animationEnabled});
    ++m_frameCount;
    m_frameTimeSeconds += delta;
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
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glFrontFace(GL_CCW);
    glCullFace(GL_BACK);
    glClearColor(0.035f, 0.052f, 0.078f, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    m_renderer->render(*m_scene->getRoot(), *m_scene->getCamera());
    overlay();
    OpenGL::check("Frame");
}

// Draws the application HUD after rendering the 3D scene.
void Application::overlay(){
    m_overlay->draw(*this);
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
    reader.registerNode("torus", GeometryFactory::createTorus(64, 24));
    reader.registerNode("alphatest-panel", GeometryFactory::createAlphaTestPanel());
}

// Restores the user's rendering toggles on a loaded scene.
void Application::applyRenderSettings(Scene& scene) const{
    if (!scene.getRoot()->getState()){
        scene.getRoot()->setState(std::make_shared<State>());
    }
    scene.getRoot()->getState()->setLighting(m_lightingEnabled);
    for (unsigned i = 0; i < m_pointLights.size(); ++i){
        scene.getRoot()->getState()->setLightEnabled(i, m_pointLights[i]);
    }
    scene.getRoot()->getState()->setCullFace(m_cullFaceEnabled);
    scene.getRoot()->getState()->setUniform("uMultiTexture", m_multitextureEnabled);
    if (auto toon = scene.findTransform("toon")){
        if (toon->getState()){
            toon->getState()->setUniform("uToon", m_toonEnabled);
        }
    }
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

// Moves the entire light ring using the arrow keys.
void Application::moveLightRing(float delta){
    const float speed = movementSpeed(delta);
    if (m_lightOrbitCallback){
        glm::vec3 shift(0);
        if (glfwGetKey(m_window, GLFW_KEY_LEFT) == GLFW_PRESS){
            shift.x -= speed;
        }
        if (glfwGetKey(m_window, GLFW_KEY_RIGHT) == GLFW_PRESS){
            shift.x += speed;
        }
        if (glfwGetKey(m_window, GLFW_KEY_UP) == GLFW_PRESS){
            shift.z -= speed;
        }
        if (glfwGetKey(m_window, GLFW_KEY_DOWN) == GLFW_PRESS){
            shift.z += speed;
        }
        m_lightOrbitCallback->moveBy(shift);
    }
}

// Returns the frame-scaled normal or fast movement distance.
float Application::movementSpeed(float delta) const{
    return (glfwGetKey(m_window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS ? 24.0f : 10.0f) * delta;
}

} // namespace vr
