#include <vr/DeferredRenderer.h>
#include <vr/FramebufferState.h>
#include <vr/SceneGraph.h>
#include <GLFW/glfw3.h>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace{
constexpr int width = 191, height = 143;
using Mode = vr::AntialiasingMode;

// Reports a failed numerical image or resource-lifetime check to CTest.
void check(bool value, const char* message){
    if (!value){
        throw std::runtime_error(message);
    }
}

// Exercises actual sample coverage, display filtering, diagnostic coherence and restoration.
void rendering(const std::filesystem::path& root){
    auto program = std::make_shared<vr::Program>(root / "shaders/phong-shading.vs",
                                                 root / "shaders/phong-shading.fs");
    vr::Scene scene;
    auto state = std::make_shared<vr::State>();
    state->setLighting(false);
    state->setCullFace(false);
    state->setUniform("uFog", false);
    auto material = std::make_shared<vr::Material>();
    material->setDiffuse({0.9f, 0.9f, 0.9f});
    state->setMaterial(material);
    scene.getRoot()->setState(state);
    auto triangle = std::make_shared<vr::Geometry>();
    triangle->setData({{-1.35f, -1.0f, 0},{1.3f, -0.83f, 0},{-0.83f, 1.07f, 0}},{0, 1, 2},
                      {{0, 0, 1},{0, 0, 1},{0, 0, 1}});
    scene.getRoot()->addChild(triangle);
    auto camera = scene.getCamera();
    camera->setPerspective(50, static_cast<float>(width) / height, 0.1f, 40);
    camera->setLookAt({0, 0, 3},{0, 0, 0});
    vr::RenderVisitor renderer(program);
    renderer.setCulling(false);
    vr::DeferredRenderer deferred(root);
    deferred.resize(width, height);
    deferred.setPreviews(false);
    const auto render = [&](Mode mode, bool forward = false){
        deferred.setAntialiasing(mode);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, width, height);
        glDisable(GL_SCISSOR_TEST);
        glDisable(GL_MULTISAMPLE);
        glDisable(GL_SAMPLE_SHADING);
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);
        glDepthMask(GL_TRUE);
        glClearColor(0.035f, 0.052f, 0.078f, 1);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        if (forward){
            deferred.renderForward([&]{ renderer.render(*scene.getRoot(), *camera); });
        } else{
            renderer.render(*scene.getRoot(), *camera, nullptr, nullptr, &deferred);
        }
        check(glGetError() == GL_NO_ERROR, "AA passes leave no OpenGL errors");
        check(!glIsEnabled(GL_MULTISAMPLE) && !glIsEnabled(GL_SAMPLE_SHADING),
              "Multisample state is restored after every rendering path");
        std::vector<unsigned char> pixels(width * height * 4);
        glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        return pixels;
    };
    for (bool forward :{false, true}){
        const auto off = render(Mode::Off, forward);
        const auto fxaa = render(Mode::Fxaa, forward);
        const auto msaa = render(Mode::Msaa, forward);
        unsigned filtered = 0, coverage = 0, unchanged = 0;
        for (std::size_t i = 0; i < off.size(); i += 4){
            filtered += std::abs(static_cast<int>(off[i]) - fxaa[i]) > 4;
            coverage += std::abs(static_cast<int>(off[i]) - msaa[i]) > 4;
            unchanged += off[i] == fxaa[i] && off[i + 1] == fxaa[i + 1];
        }
        check(filtered > 100 && coverage > 100, "FXAA and real MSAA both smooth diagonal edges");
        check(unchanged > width * height * 9 / 10, "FXAA keeps flat surface interiors sharp");
        check(render(Mode::Off, forward) == off, "Off restores the original image exactly");
        check(vr::nextAntialiasing(vr::nextAntialiasing(vr::nextAntialiasing(Mode::Off))) ==
                  Mode::Off,
              "The three cost-ordered choices wrap back to Off");
    }
    render(Mode::Msaa);
    const auto positions = deferred.getGBuffer().read(vr::GBuffer::Position);
    const auto normals = deferred.getGBuffer().read(vr::GBuffer::Normal);
    for (std::size_t i = 0; i < positions.size(); i += 4){
        check(positions[i + 3] == 0 || positions[i + 3] == 1,
              "MSAA diagnostic positions are a coherent surface, never averaged coverage");
        if (positions[i + 3] == 1){
            check(std::abs(normals[i + 2] - 1) < 0.001f,
                  "Resolved normals retain unit length at silhouette edges");
        }
    }
    deferred.setView(vr::BufferView::Normal);
    const auto normalOff = render(Mode::Off);
    check(render(Mode::Fxaa) == normalOff, "FXAA does not filter individual diagnostic channels");
    deferred.setView(vr::BufferView::Lit);
    deferred.setAmbientOcclusion(true);
    deferred.setBloom(true);
    deferred.setDepthOfField(true);
    deferred.setFocusDistance(3.0f);
    render(Mode::Msaa);
    check(deferred.getScreenDraws() == 19,
          "MSAA surface resolve and AO transfer are included with existing postprocessing draws");
    deferred.resize(127, 91);
    render(Mode::Msaa);
    deferred.resize(width, height);
    render(Mode::Msaa);
    deferred.resize(0, 0);
    render(Mode::Fxaa);
    // Verify caller state beyond the default disabled sample-shading configuration.
    glEnable(GL_MULTISAMPLE);
    glEnable(GL_SAMPLE_SHADING);
    glMinSampleShading(0.5f);
    {
        vr::FramebufferState saved;
        glDisable(GL_MULTISAMPLE);
        glDisable(GL_SAMPLE_SHADING);
        glMinSampleShading(1.0f);
    }
    GLfloat fraction = 0;
    glGetFloatv(GL_MIN_SAMPLE_SHADING_VALUE, &fraction);
    check(glIsEnabled(GL_MULTISAMPLE) && glIsEnabled(GL_SAMPLE_SHADING) && fraction == 0.5f,
          "The framebuffer guard restores sample enables and the shading fraction");
}
} // namespace

// Creates an ordinary single-sample window so MSAA tests must exercise offscreen coverage.
int main(int argc, char** argv){
    if (argc != 2 || !glfwInit()){
        return 1;
    }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_SAMPLES, 0);
    auto* window = glfwCreateWindow(width, height, "Antialiasing tests", nullptr, nullptr);
    if (!window){
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(window);
    int result = 0;
    try{
        if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress))){
            throw std::runtime_error("Cannot load OpenGL");
        }
        rendering(argv[1]);
        std::cout << "Antialiasing coverage, filtering, buffers and state checks passed\n";
    } catch (const std::exception& error){
        std::cerr << "FAILED: " << error.what() << '\n';
        result = 1;
    }
    glfwDestroyWindow(window);
    glfwTerminate();
    return result;
}
