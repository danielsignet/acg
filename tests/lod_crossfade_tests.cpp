#include <vr/DeferredRenderer.h>
#include <vr/SceneGraph.h>
#include <vr/ShadowRenderer.h>
#include <GLFW/glfw3.h>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace{
constexpr int width = 320, height = 240;

// Reports failed crossfade contracts to CTest.
void check(bool value, const char* message){
    if (!value){
        throw std::runtime_error(message);
    }
}

// Checks complementary coverage, pass parity, endpoint continuity and shadow depth.
void run(const std::filesystem::path& path, bool useDeferred){
    auto program = std::make_shared<vr::Program>(path / "shaders/phong-shading.vs",
                                                 path / "shaders/phong-shading.fs");
    vr::RenderVisitor renderer(program);
    vr::DeferredRenderer deferred(path);
    deferred.resize(width, height);
    deferred.setPreviews(false);
    vr::Scene scene;
    const auto root = scene.getRoot();
    auto state = std::make_shared<vr::State>();
    state->setLighting(false);
    state->setUniform("uFog", false);
    root->setState(state);
    auto lod = std::make_shared<vr::LOD>();
    const auto mesh = vr::GeometryFactory::createBox();
    for (auto color :{glm::vec3(1, 0, 0), glm::vec3(0, 0, 1)}){
        auto level = std::make_shared<vr::Group>();
        auto material = std::make_shared<vr::Material>();
        material->setDiffuse(color);
        auto local = std::make_shared<vr::State>();
        local->setMaterial(material);
        level->setState(local);
        level->addChild(mesh);
        lod->addLevel(color.r > 0 ? 8.0f : 100.0f, level);
    }
    root->addChild(lod);
    auto sun = std::make_shared<vr::Light>();
    sun->setPosition({-1, 2, 1, 0});
    root->addChild(sun);
    const auto camera = scene.getCamera();
    camera->setPerspective(40, static_cast<float>(width) / height, 0.1f, 40);
    camera->setLookAt({0, 0, 8},{0, 0, 0});
    vr::ShadowRenderer shadows(path, 128, 64);
    const auto render = [&](float center){
        lod->setCenter({0, 0, center});
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, width, height);
        glEnable(GL_DEPTH_TEST);
        glDepthMask(GL_TRUE);
        glClearColor(0, 0, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        shadows.render(scene, camera->getPosition(), true, true);
        renderer.render(*root, *camera, nullptr, nullptr, useDeferred ? &deferred : nullptr);
        std::vector<unsigned char> image(width * height * 4);
        glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, image.data());
        check(glGetError() == GL_NO_ERROR, "Crossfade keeps valid GL state");
        return image;
    };
    const auto near = render(2), far = render(-2);
    const auto referenceDepth = shadows.getSunTarget()->readDepth(0);
    lod->setFadeWidth(2);
    check(render(2) == near && render(-2) == far,
          "Outside transition zones only one full model is rendered");
    unsigned previous = 0;
    for (float center :{0.9f, 0.5f, 0.0f, -0.5f, -0.9f}){
        const auto image = render(center);
        unsigned blue = 0, covered = 0;
        for (std::size_t i = 0; i < image.size(); i += 4){
            if (near[i] < 80){
                continue;
            }
            const bool nearPixel = image[i] == near[i] && image[i + 2] == near[i + 2];
            const bool farPixel = image[i] == far[i] && image[i + 2] == far[i + 2];
            check(nearPixel || farPixel, "Overlapping levels have no holes or doubled lighting");
            blue += farPixel;
            ++covered;
        }
        const float expected = lod->select(8 - center).blend;
        check(covered > 100 && std::abs(static_cast<float>(blue) / covered - expected) < 0.04f,
              "Pixel coverage follows the continuous distance blend");
        check(blue > previous,
              "Approaching the far range increases far-model coverage monotonically");
        previous = blue;
        check(renderer.getStats().draws == 2 && shadows.getStats().instances == 2,
              "Color and shadow passes submit both models during the crossfade");
        check(shadows.getSunTarget()->readDepth(0) == referenceDepth,
              "Complementary shadow coverage retains a complete solid depth map");
    }
    check(render(0.9999f) == near && render(-0.9999f) == far,
          "Crossfade endpoints join the full-detail renders without a flash");
    std::cout << (useDeferred ? "Deferred" : "Forward") << " LOD crossfade passed\n";
}
} // namespace

// Owns the hidden GPU context until all render resources have been destroyed.
int main(int argc, char** argv){
    if (argc != 2 || !glfwInit()){
        return 1;
    }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    auto* window = glfwCreateWindow(width, height, "LOD crossfade tests", nullptr, nullptr);
    if (!window){
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(window);
    int result = 0;
    if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress))){
        result = 1;
    } else{
        try{
            run(argv[1], false);
            run(argv[1], true);
        } catch (const std::exception& error){
            std::cerr << error.what() << '\n';
            result = 1;
        }
    }
    glfwDestroyWindow(window);
    glfwTerminate();
    return result;
}
