#include <vr/DeferredRenderer.h>
#include <vr/SceneGraph.h>
#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace{
constexpr int width = 320, height = 240;

// Reports a failed rendering contract to CTest.
void check(bool value, const char* message){
    if (!value){
        throw std::runtime_error(message);
    }
}

// Adds an opaque box with a known world-space size and position.
std::shared_ptr<vr::Transform> box(vr::Group& root, glm::vec3 position, glm::vec3 scale){
    auto transform = std::make_shared<vr::Transform>();
    transform->setMatrix(glm::scale(glm::translate(glm::mat4(1), position), scale));
    transform->addChild(vr::GeometryFactory::createBox());
    root.addChild(transform);
    return transform;
}

// Reads the actual resolved floating-point ambient/AO attachment.
std::vector<float> ambient(const vr::DeferredRenderer& deferred){
    const auto& texture = deferred.getAmbientBuffer();
    texture->apply(0);
    std::vector<float> values(static_cast<std::size_t>(texture->getWidth()) * texture->getHeight() *
                              4);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_FLOAT, values.data());
    return values;
}

// Verifies contact occlusion, flat surfaces, backgrounds and lighting separation on the GPU.
void run(const std::filesystem::path& path){
    auto program = std::make_shared<vr::Program>(path / "shaders/phong-shading.vs",
                                                 path / "shaders/phong-shading.fs");
    vr::Scene scene;
    const auto root = scene.getRoot();
    auto state = std::make_shared<vr::State>();
    auto material = std::make_shared<vr::Material>();
    material->setAmbient({0.7f, 0.6f, 0.5f});
    material->setDiffuse({0.8f, 0.7f, 0.6f});
    material->setSpecular({0, 0, 0});
    state->setMaterial(material);
    state->setUniform("uFog", false);
    root->setState(state);
    box(*root,{0, -0.1f, 0},{10, 0.2f, 10});
    auto object = box(*root,{0, 1, 0},{2, 2, 2});
    const auto camera = scene.getCamera();
    camera->setPerspective(50, static_cast<float>(width) / height, 0.1f, 60);
    camera->setLookAt({4, 3, 5},{0, 0.7f, 0});
    vr::RenderVisitor renderer(program);
    renderer.setCulling(false);
    vr::DeferredRenderer deferred(path);
    deferred.resize(width, height);
    deferred.setPreviews(false);
    const auto render = [&]{
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, width, height);
        renderer.render(*root, *camera, nullptr, nullptr, &deferred);
        std::vector<unsigned char> pixels(width * height * 4);
        glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        check(glGetError() == GL_NO_ERROR, "AO passes retain valid OpenGL state");
        return pixels;
    };
    const auto original = render();
    check(deferred.getScreenDraws() == 3, "Disabled AO issues no sampling or resolve draws");
    deferred.setAmbientOcclusion(true);
    const auto occluded = render();
    const auto values = ambient(deferred);
    const auto positions = deferred.getGBuffer().read(vr::GBuffer::Position);
    unsigned darkened = 0;
    float minimum = 1;
    for (std::size_t i = 0; i < values.size(); i += 4){
        const float visibility = values[i + 3];
        check(std::isfinite(visibility) && visibility >= 0 && visibility <= 1,
              "AO visibility is finite and normalized");
        if (positions[i + 3] == 0){
            check(visibility == 1, "Background remains unoccluded after filtering");
        } else{
            minimum = std::min(minimum, visibility);
            check(std::abs(values[i] - 0.7f) < 0.002f,
                  "AO resolve preserves the material's original ambient RGB");
        }
        darkened += static_cast<int>(original[i]) - occluded[i] > 2;
    }
    check(minimum < 0.9f && darkened > 30,
          "A box resting on a floor produces visible contact occlusion");
    check(deferred.getScreenDraws() == 5, "Enabled AO uses one sampling and one resolve pass");
    deferred.setView(vr::BufferView::Occlusion);
    const auto debug = render();
    check(debug != occluded, "AO has its own independently rendered diagnostic view");
    for (std::size_t i = 0; i < debug.size(); i += 4){
        if (positions[i + 3] > 0){
            check(debug[i] == debug[i + 1] && debug[i] == debug[i + 2],
                  "The AO view displays the visibility factor in grayscale");
        }
    }
    deferred.setView(vr::BufferView::Lit);
    object->setVisible(false);
    render();
    const auto flat = ambient(deferred);
    for (std::size_t i = 3; i < flat.size(); i += 4){
        check(flat[i] > 0.98f, "An isolated flat floor does not occlude itself");
    }
    object->setVisible(true);
    state->setLighting(false);
    deferred.setAmbientOcclusion(false);
    const auto unlit = render();
    deferred.setAmbientOcclusion(true);
    check(render() == unlit, "AO leaves unlit surfaces unchanged");
    state->setLighting(true);
    material->setAmbient({0, 0, 0});
    state->setUniform("uEmissive", glm::vec3(0.4f, 0.3f, 0.2f));
    auto light = std::make_shared<vr::Light>();
    light->setPosition({3, 5, 4, 1});
    light->setAmbient({0, 0, 0});
    light->setDiffuse({1, 1, 1});
    root->addChild(light);
    deferred.setAmbientOcclusion(false);
    const auto direct = render();
    deferred.setAmbientOcclusion(true);
    check(render() == direct, "AO does not darken direct light or emissive radiance");
    deferred.resize(321, 241);
    render();
    check(deferred.getAmbientBuffer()->getWidth() == 321 &&
              deferred.getAmbientBuffer()->getHeight() == 241,
          "Odd viewport dimensions resize AO and resolve attachments together");
    for (float value : ambient(deferred)){
        check(std::isfinite(value), "Odd-sized AO resolves contain no invalid values");
    }
    std::cout << "Ambient occlusion rendering passed\n";
}
} // namespace

// Owns the hidden context until all rendering resources have been released.
int main(int argc, char** argv){
    if (argc != 2 || !glfwInit()){
        return 1;
    }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    auto* window = glfwCreateWindow(width, height, "Ambient occlusion tests", nullptr, nullptr);
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
            run(argv[1]);
        } catch (const std::exception& error){
            std::cerr << "FAILED: " << error.what() << '\n';
            result = 1;
        }
    }
    glfwDestroyWindow(window);
    glfwTerminate();
    return result;
}
