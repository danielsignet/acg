#include <vr/ShadowRenderer.h>
#include <vr/ShadowCameras.h>
#include <vr/SceneGraph.h>
#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace{
constexpr int width = 640, height = 480;
unsigned checks = 0;

void check(bool value, const char* message){
    ++checks;
    if (!value){
        throw std::runtime_error(message);
    }
}

std::vector<unsigned char> pixels(){
    std::vector<unsigned char> data(width * height * 3);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, data.data());
    return data;
}

unsigned changed(const std::vector<unsigned char>& a, const std::vector<unsigned char>& b){
    unsigned count = 0;
    for (std::size_t i = 0; i < a.size(); ++i){
        if (std::abs(static_cast<int>(a[i]) - static_cast<int>(b[i])) > 2){
            ++count;
        }
    }
    return count;
}

std::shared_ptr<vr::Transform> place(const std::shared_ptr<vr::Group>& root,
                                     const std::shared_ptr<vr::Node>& asset, glm::vec3 position,
                                     glm::vec3 scale){
    auto node = std::make_shared<vr::Transform>();
    node->setMatrix(glm::scale(glm::translate(glm::mat4(1), position), scale));
    node->addChild(asset);
    root->addChild(node);
    return node;
}

void run(const std::filesystem::path& path){
    auto program = std::make_shared<vr::Program>(path / "shaders/phong-shading.vs",
                                                 path / "shaders/phong-shading.fs");
    vr::ShadowRenderer shadows(path, 512, 256);
    vr::RenderVisitor renderer(program);
    vr::Scene scene;
    shadows.attach(scene);
    check(scene.getRenderTargets().size() == 2,
          "Both render-to-texture resources attached to scene");
    check(scene.getRenderTargets()[0]->getCameras().size() == 1 &&
              scene.getRenderTargets()[1]->getCameras().size() == 6,
          "Seven independent light cameras attached");
    auto root = scene.getRoot();
    auto state = std::make_shared<vr::State>();
    state->setUniform("uFog", false);
    root->setState(state);
    auto material = std::make_shared<vr::Material>();
    material->setDiffuse({0.8f, 0.8f, 0.8f});
    material->setAmbient({0.07f, 0.07f, 0.07f});
    material->setSpecular({0, 0, 0});
    state->setMaterial(material);
    auto sun = std::make_shared<vr::Light>();
    sun->setPosition({-1, 2, 1, 0});
    sun->setDiffuse({1, 1, 1});
    sun->setAmbient({0.01f, 0.01f, 0.01f});
    root->addChild(sun);
    auto point = std::make_shared<vr::Light>();
    point->setPosition({4, 6, 3, 1});
    point->setDiffuse({1.3f, 1.1f, 0.9f});
    point->setAmbient({0.01f, 0.01f, 0.01f});
    point->setAttenuation({1, 0, 0});
    root->addChild(point);
    auto floor = place(root, vr::GeometryFactory::createGround(),{0, 0, 0},{1, 1, 1});
    auto box = place(root, vr::GeometryFactory::createBox(),{0, 1, 0},{2, 2, 2});
    auto camera = scene.getCamera();
    camera->setPerspective(45, static_cast<float>(width) / height, 0.1f, 90);
    camera->setLookAt({0, 6, 9},{0, 0, 0});
    const auto render = [&](bool enabled, bool culling, const vr::PortalResult* portals = nullptr,
                            bool filtered = false){
        glViewport(0, 0, width, height);
        glEnable(GL_DEPTH_TEST);
        glDepthMask(GL_TRUE);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glDisable(GL_DITHER);
        glDisable(GL_BLEND);
        glDisable(GL_SCISSOR_TEST);
        glCullFace(GL_BACK);
        glFrontFace(GL_CCW);
        glClearColor(0.035f, 0.052f, 0.078f, 1);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        shadows.render(scene, camera->getPosition(), enabled, filtered);
        renderer.setCulling(culling);
        renderer.render(*root, *camera, nullptr, portals);
        check(glGetError() == GL_NO_ERROR, "Shadow and main passes have no GL errors");
        return pixels();
    };
    box->setVisible(false);
    for (unsigned selected = 0; selected < 2; ++selected){
        for (bool filtered :{false, true}){
            state->setLightEnabled(0, selected == 0);
            state->setLightEnabled(1, selected == 1);
            const auto barePlane = render(false, false);
            const auto shadowedPlane = render(true, false, nullptr, filtered);
            check(changed(barePlane, shadowedPlane) == 0,
                  "A bare plane must not develop false self-shadowing, with either light or filter "
                  "mode");
        }
    }
    box->setVisible(true);
    for (unsigned selected = 0; selected < 3; ++selected){
        state->setLightEnabled(0, selected != 1);
        state->setLightEnabled(1, selected != 0);
        const auto off = render(false, false);
        check(shadows.getStats().passes == 0, "Disabled shadows skip depth passes");
        const auto on = render(true, false);
        check(shadows.getStats().passes == (selected == 0   ? 1u
                                            : selected == 1 ? 6u
                                                            : 7u),
              "Correct depth-pass count for each light");
        check(changed(off, on) > 200, "Each light independently casts a visible shadow");
        unsigned brighter = 0;
        for (std::size_t i = 0; i < on.size(); ++i){
            if (static_cast<int>(on[i]) > static_cast<int>(off[i]) + 1){
                ++brighter;
            }
        }
        check(brighter == 0, "Shadows only remove direct illumination");
        const auto filtered = render(true, false, nullptr, true);
        check(changed(filtered, off) > 200, "Filtered depth comparisons preserve visible shadows");
    }
    const auto sunDepth = shadows.getSunTarget()->readDepth(0);
    check(*std::min_element(sunDepth.begin(), sunDepth.end()) < 0.99f &&
              *std::max_element(sunDepth.begin(), sunDepth.end()) > 0.999f,
          "Depth image contains geometry and clear background");
    const auto withoutCulling = render(true, false);
    const auto before = shadows.getSunTarget()->readDepth(0);
    const auto withCulling = render(true, true);
    check(withoutCulling == withCulling, "VFC leaves the shaded image unchanged");
    check(before == shadows.getSunTarget()->readDepth(0),
          "Observer VFC cannot change light depth image");
    box->setMatrix(glm::scale(glm::translate(glm::mat4(1),{2, 1, 0}), glm::vec3(2)));
    render(true, false);
    check(before != shadows.getSunTarget()->readDepth(0),
          "Moving object updates its depth projection");
    // Transparent debug appearance must keep physical geometry in depth passes.
    const auto solidDepth = shadows.getSunTarget()->readDepth(0);
    auto transparentState = std::make_shared<vr::State>();
    box->setState(transparentState);
    transparentState->setBlend(true);
    transparentState->setUniform("uOpacity", 0.12f);
    render(true, false);
    check(solidDepth == shadows.getSunTarget()->readDepth(0),
          "Transparent wall appearance cannot change the physical shadow map");
    transparentState->setBlend(false);
    transparentState->setUniform("uOpacity", 1.0f);
    // Put an occluder outside the camera, but on a light ray reaching the visible floor.
    box->setMatrix(glm::scale(glm::translate(glm::mat4(1),{10, 4, 0}), glm::vec3(2)));
    box->setRoomId("hidden-room");
    sun->setPosition({20, 8, 0, 0});
    state->setLightEnabled(0, true);
    state->setLightEnabled(1, false);
    const auto uncull = render(true, false);
    const auto allDraws = renderer.getStats().draws;
    const auto cull = render(true, true);
    check(renderer.getStats().draws < allDraws,
          "Fixture occluder is genuinely outside camera frustum");
    check(uncull == cull && shadows.getCasters().size() == 2,
          "Off-camera occluder remains in the independent shadow list");
    vr::PortalResult portals;
    portals.active = true;
    portals.origin = "camera-room";
    const auto portalImage = render(true, false, &portals);
    check(portalImage == uncull,
          "Portal rejection cannot erase an off-camera caster's visible shadow");
    auto boxState = std::make_shared<vr::State>();
    box->setState(boxState);
    boxState->setCastShadows(false);
    const auto noCaster = render(true, true);
    check(changed(noCaster, cull) > 100,
          "Removing off-camera caster changes a visible floor shadow");
    boxState->setCastShadows(true);
    box->setMatrix(glm::translate(glm::mat4(1),{0, 1, 0}));
    sun->setPosition({-1, 2, 1, 0});
    auto panel = place(root, vr::GeometryFactory::createAlphaTestPanel(),{0, 0, -3},{2, 2, 2});
    auto panelState = std::make_shared<vr::State>();
    panel->setState(panelState);
    std::vector<unsigned char> rgba(8 * 8 * 4, 255);
    for (int y = 0; y < 8; ++y){
        for (int x = 0; x < 4; ++x){
            rgba[(y * 8 + x) * 4 + 3] = 0;
        }
    }
    panelState->setTexture(0, std::make_shared<vr::Texture>(8, 8, rgba, false));
    panelState->setUniform("uAlphaCutoff", 0.0f);
    render(true, false);
    const auto solid = shadows.getSunTarget()->readDepth(0);
    panelState->setUniform("uAlphaCutoff", 0.5f);
    render(true, false);
    const auto cutout = shadows.getSunTarget()->readDepth(0);
    unsigned holes = 0;
    for (std::size_t i = 0; i < solid.size(); ++i){
        if (cutout[i] > solid[i] + 1e-5f){
            ++holes;
        }
    }
    check(holes > 10, "Alpha-discarded texels also make holes in the shadow map");
    panel->setVisible(false);
    auto floorState = std::make_shared<vr::State>();
    floorState->setReceiveShadows(false);
    floor->setState(floorState);
    boxState->setReceiveShadows(false);
    auto receiver = place(root, vr::GeometryFactory::createSphere(32, 20),{0, 2, 0},{2, 2, 2});
    auto receiverState = std::make_shared<vr::State>();
    receiver->setState(receiverState);
    box->setMatrix(glm::scale(glm::translate(glm::mat4(1),{4, 6, 0}), glm::vec3(1.8f)));
    point->setPosition({8, 10, 0, 1});
    state->setLightEnabled(0, false);
    state->setLightEnabled(1, true);
    const auto curvedShadow = render(true, false);
    receiverState->setReceiveShadows(false);
    const auto curvedUnshadowed = render(true, false);
    check(changed(curvedShadow, curvedUnshadowed) > 80,
          "Curved 3D receiver gets shadows independently of floor and caster shading");
    const auto oldPoint = shadows.getPointTarget()->readDepth(1);
    point->setPosition({-8, 10, 0, 1});
    render(true, false);
    check(oldPoint != shadows.getPointTarget()->readDepth(1),
          "Moving point light updates cubemap depth");
    check(glm::length(shadows.getPointTarget()->getCameras()[0]->getPosition() -
                      glm::vec3(-8, 10, 0)) < 1e-5f,
          "Shadow camera follows the movable light source");
    // Six objects around a point light exercise every cubemap camera and face orientation.
    vr::Scene cubeScene;
    shadows.attach(cubeScene);
    auto cubeRoot = cubeScene.getRoot();
    cubeRoot->setState(std::make_shared<vr::State>());
    auto cubeLight = std::make_shared<vr::Light>();
    cubeLight->setPosition({0, 0, 0, 1});
    cubeRoot->addChild(cubeLight);
    for (auto axis :{glm::vec3(1, 0, 0), glm::vec3(-1, 0, 0), glm::vec3(0, 1, 0),
                      glm::vec3(0, -1, 0), glm::vec3(0, 0, 1), glm::vec3(0, 0, -1)}){
        place(cubeRoot, vr::GeometryFactory::createBox(), axis * 3.0f,{1, 1, 1});
    }
    shadows.render(cubeScene,{0, 0, 10}, true, false);
    const float farPlane =
        std::get<float>(cubeRoot->getState()->getUniforms().at("uPointShadowFar"));
    for (unsigned face = 0; face < 6; ++face){
        const auto data = shadows.getPointTarget()->readDepth(face);
        const float distance = data[128 * 256 + 128] * farPlane;
        check(std::abs(distance - 2.5f) < 0.03f,
              "All six cubemap faces store radial distance to their own occluder");
    }
    // The reusable RTT attachment restores caller state even if drawing throws.
    glViewport(11, 13, 201, 177);
    glColorMask(GL_FALSE, GL_TRUE, GL_FALSE, GL_TRUE);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glEnable(GL_SCISSOR_TEST);
    glDisable(GL_DEPTH_TEST);
    bool caught = false;
    try{
        shadows.getSunTarget()->render(
            0, [](const vr::Camera&){ throw std::runtime_error("draw failure"); });
    } catch (const std::runtime_error&){
        caught = true;
    }
    GLint viewport[4]{}, framebuffer = -1;
    GLboolean colors[4]{}, depthMask = GL_TRUE;
    glGetIntegerv(GL_VIEWPORT, viewport);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &framebuffer);
    glGetBooleanv(GL_COLOR_WRITEMASK, colors);
    glGetBooleanv(GL_DEPTH_WRITEMASK, &depthMask);
    check(caught && framebuffer == 0 && viewport[0] == 11 && viewport[1] == 13 &&
              viewport[2] == 201 && viewport[3] == 177,
          "RTT restores framebuffer and viewport after exceptions");
    check(!colors[0] && colors[1] && !colors[2] && colors[3] && !depthMask &&
              glIsEnabled(GL_BLEND) && glIsEnabled(GL_SCISSOR_TEST) && !glIsEnabled(GL_DEPTH_TEST),
          "RTT restores color/depth masks and enable state");
    check(glGetError() == GL_NO_ERROR, "Final shadow test GL state valid");
    std::cout << checks
              << " shadow rendering checks passed: both lights, six cube faces, off-camera/portal "
                 "casters, alpha holes, RTT state\n";
}
} // namespace

int main(int argc, char** argv){
    if (argc != 2){
        return 1;
    }
    if (!glfwInit()){
        return 1;
    }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    auto* window = glfwCreateWindow(width, height, "Shadow tests", nullptr, nullptr);
    if (!window){
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(0);
    if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress))){
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }
    int result = 0;
    try{
        run(argv[1]);
    } catch (const std::exception& e){
        std::cerr << "FAILED: " << e.what() << '\n';
        result = 1;
    }
    glfwDestroyWindow(window);
    glfwTerminate();
    return result;
}
