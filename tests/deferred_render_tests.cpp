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
unsigned checks = 0;

// Reports a failed GPU behavior check to CTest.
void check(bool value, const char* message){
    ++checks;
    if (!value){
        throw std::runtime_error(message);
    }
}

// Reads the presented image without involving files or screenshots.
std::vector<unsigned char> pixels(){
    std::vector<unsigned char> image(width * height * 4);
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, image.data());
    return image;
}

// Counts visible differences beyond framebuffer quantization.
unsigned changed(const std::vector<unsigned char>& a, const std::vector<unsigned char>& b){
    unsigned count = 0;
    for (std::size_t i = 0; i < a.size(); i += 4){
        bool different = false;
        for (unsigned channel = 0; channel < 3; ++channel){
            different =
                different || std::abs(static_cast<int>(a[i + channel]) - b[i + channel]) > 2;
        }
        count += different;
    }
    return count;
}

// Attaches a transformed box with inherited scene state.
std::shared_ptr<vr::Transform> box(const std::shared_ptr<vr::Group>& root, glm::vec3 position,
                                   glm::vec3 scale){
    auto node = std::make_shared<vr::Transform>();
    node->setMatrix(glm::scale(glm::translate(glm::mat4(1), position), scale));
    node->addChild(vr::GeometryFactory::createBox());
    root->addChild(node);
    return node;
}

// Exercises G-buffer data, lighting parity, independent views, effects and target lifetime.
void run(const std::filesystem::path& path){
    auto program = std::make_shared<vr::Program>(path / "shaders/phong-shading.vs",
                                                 path / "shaders/phong-shading.fs");
    vr::RenderVisitor renderer(program);
    renderer.setCulling(false);
    vr::DeferredRenderer deferred(path);
    deferred.resize(width, height);
    deferred.setPreviews(false);
    vr::Scene scene;
    const auto root = scene.getRoot();
    auto state = std::make_shared<vr::State>();
    auto material = std::make_shared<vr::Material>();
    material->setAmbient({0.04f, 0.03f, 0.02f});
    material->setDiffuse({0.3f, 0.5f, 0.7f});
    material->setSpecular({0.2f, 0.4f, 0.6f});
    material->setShininess(73);
    state->setMaterial(material);
    state->setUniform("uFog", false);
    root->setState(state);
    for (unsigned i = 0; i < 4; ++i){
        auto light = std::make_shared<vr::Light>();
        light->setPosition({static_cast<float>(i) - 1.5f, 2, 4, 1});
        light->setAmbient({0, 0, 0});
        light->setDiffuse({0.2f + 0.1f * i, 0.4f - 0.05f * i, 0.3f});
        light->setAttenuation({1, 0.02f, 0.01f});
        root->addChild(light);
    }
    auto object = box(root,{0, 0, 0},{2, 2, 2});
    auto objectState = std::make_shared<vr::State>();
    object->setState(objectState);
    const auto camera = scene.getCamera();
    camera->setPerspective(50, static_cast<float>(width) / height, 0.1f, 40);
    camera->setLookAt({0, 0, 6},{0, 0, 0});
    const auto render = [&](bool useDeferred = true){
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, width, height);
        glEnable(GL_DEPTH_TEST);
        glDepthMask(GL_TRUE);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glDisable(GL_SCISSOR_TEST);
        glDisable(GL_BLEND);
        glClearColor(0.035f, 0.052f, 0.078f, 1);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        renderer.render(*root, *camera, nullptr, nullptr, useDeferred ? &deferred : nullptr);
        check(glGetError() == GL_NO_ERROR, "Every deferred/forward pass has valid OpenGL state");
        return pixels();
    };
    const auto forward = render(false);
    const auto lit = render();
    check(changed(forward, lit) < 80,
          "Deferred lighting matches forward lighting away from quantized silhouette edges");
    const auto sample = [&](unsigned attachment){
        const auto values = deferred.getGBuffer().read(attachment);
        const auto i = (height / 2 * width + width / 2) * 4;
        return glm::vec4(values[i], values[i + 1], values[i + 2], values[i + 3]);
    };
    const auto position = sample(vr::GBuffer::Position);
    check(glm::distance(glm::vec3(position), glm::vec3(0, 0, 1)) < 0.025f && position.w == 1,
          "Position attachment stores unencoded world coordinates, including values outside 0..1");
    check(glm::distance(glm::vec3(sample(vr::GBuffer::Normal)), glm::vec3(0, 0, 1)) < 0.001f,
          "Normal attachment stores normalized world-space surface normals");
    check(glm::distance(glm::vec3(sample(vr::GBuffer::Albedo)), material->getDiffuse()) < 0.005f,
          "Albedo attachment stores the material color without lighting");
    const auto specular = sample(vr::GBuffer::Specular);
    check(glm::distance(glm::vec3(specular), material->getSpecular()) < 0.001f && specular.w == 73,
          "Specular attachment retains RGB reflectance and the material shininess");
    const auto initialPositions = deferred.getGBuffer().read(vr::GBuffer::Position);
    for (unsigned i = 0; i < 4; ++i){
        state->setLightEnabled(i, false);
        const auto disabled = render();
        check(changed(lit, disabled) > 500,
              "Each of four simultaneous lights independently changes deferred illumination");
        check(initialPositions == deferred.getGBuffer().read(vr::GBuffer::Position),
              "Lighting changes do not change geometry buffer data");
        state->setLightEnabled(i, true);
    }
    for (auto mode :{vr::BufferView::Position, vr::BufferView::Normal, vr::BufferView::Albedo,
                      vr::BufferView::Specular, vr::BufferView::Depth}){
        deferred.setView(mode);
        check(changed(lit, render()) > 1000,
              "Each G-buffer channel can replace the lit image independently");
    }
    deferred.setView(vr::BufferView::Lit);
    objectState->setUniform("uProcedural", 1);
    check(changed(lit, render()) > 500,
          "Procedural brick texturing contributes visible albedo in the G-buffer");
    objectState->setUniform("uProcedural", 0);
    objectState->setTexture(
        0, std::make_shared<vr::Texture>(1, 1, std::vector<unsigned char>{255, 255, 255, 0}));
    objectState->setUniform("uAlphaCutoff", 0.5f);
    render();
    check(sample(vr::GBuffer::Position).w == 0,
          "Alpha-cutout fragments leave G-buffer coverage and opaque depth empty");
    objectState->setTexture(0,{});
    objectState->setUniform("uAlphaCutoff", 0.0f);
    auto glass = box(root,{0, 0, 2},{1.5f, 1.5f, 0.1f});
    auto glassState = std::make_shared<vr::State>();
    glassState->setBlend(true);
    glassState->setUniform("uOpacity", 0.3f);
    glassState->setUniform("uTint", glm::vec3(0.1f, 0.9f, 0.3f));
    glass->setState(glassState);
    check(changed(lit, render()) > 500 &&
              glm::distance(sample(vr::GBuffer::Position), position) < 0.001f,
          "Transparent geometry is composited after lighting while retaining the opaque G-buffer");
    glass->setVisible(false);
    object->setMatrix(
        glm::scale(glm::rotate(glm::mat4(1), glm::radians(25.0f),{0, 1, 0}),{2, 3, 1}));
    render();
    const auto transformedNormal = glm::normalize(glm::vec3(0.422618f, 0, 0.906308f));
    check(glm::distance(glm::vec3(sample(vr::GBuffer::Normal)), transformedNormal) < 0.002f,
          "Nonuniform scale and rotation use the inverse-transpose normal transform");
    object->setMatrix(glm::scale(glm::mat4(1),{2, 2, 2}));
    objectState->setUniform("uEmissive", glm::vec3(4, 2, 1));
    const auto bright = render();
    deferred.setBloom(true);
    const auto bloom = render();
    check(changed(bright, bloom) > 200,
          "Bloom extraction and separable blur alter the presented image");
    deferred.setBloom(false);
    check(changed(bright, render()) == 0,
          "Disabling bloom exactly restores the original scene image");
    box(root,{-2.7f, 0, -5},{3, 3, 2});
    const auto focused = render();
    deferred.setDepthOfField(true);
    const auto dof = render();
    check(changed(focused, dof) > 100,
          "Depth of field blurs surfaces at different camera distances");
    const auto center = (height / 2 * width + width / 2) * 4;
    check(std::abs(static_cast<int>(focused[center]) - dof[center]) <= 2,
          "The central focus surface remains sharp when depth of field is enabled");
    deferred.setBloom(true);
    check(changed(dof, render()) > 200,
          "Bloom and depth of field can be chained and enabled independently");
    deferred.setBloom(false);
    deferred.setDepthOfField(false);
    check(changed(focused, render()) == 0,
          "Disabling both post effects restores the unprocessed lighting result");
    deferred.resize(197, 113);
    check(deferred.getGBuffer().get(vr::GBuffer::Position)->getWidth() == 197 &&
              deferred.getGBuffer().getDepth()->getHeight() == 113 &&
              deferred.getGBuffer().read(vr::GBuffer::Normal).size() == 197 * 113 * 4,
          "Resizing recreates all color and depth attachments at the requested resolution");
    const auto depthId = deferred.getGBuffer().getDepth()->getId();
    deferred.resize(0, 0);
    check(deferred.getGBuffer().getDepth()->getId() == depthId,
          "Minimized windows retain valid rendering targets");
    deferred.resize(width, height);
    render();
    glViewport(7, 9, 111, 117);
    glColorMask(GL_FALSE, GL_TRUE, GL_FALSE, GL_TRUE);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glEnable(GL_SCISSOR_TEST);
    glDisable(GL_DEPTH_TEST);
    bool caught = false;
    try{
        deferred.render(
            *camera, vr::State::defaults(program).merged(*state),
            vr::collectLights(*root, camera->getPosition()),
            [](const auto&){ throw std::runtime_error("geometry failure"); }, []{});
    } catch (const std::runtime_error&){
        caught = true;
    }
    GLint viewport[4]{}, framebuffer = -1;
    GLboolean colors[4]{}, depthMask = GL_TRUE;
    glGetIntegerv(GL_VIEWPORT, viewport);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &framebuffer);
    glGetBooleanv(GL_COLOR_WRITEMASK, colors);
    glGetBooleanv(GL_DEPTH_WRITEMASK, &depthMask);
    check(caught && framebuffer == 0 && viewport[0] == 7 && viewport[1] == 9 &&
              viewport[2] == 111 && viewport[3] == 117,
          "Failed rendering restores the caller framebuffer and viewport");
    check(!colors[0] && colors[1] && !colors[2] && colors[3] && !depthMask &&
              glIsEnabled(GL_BLEND) && glIsEnabled(GL_SCISSOR_TEST) && !glIsEnabled(GL_DEPTH_TEST),
          "Failed rendering restores color/depth masks and rasterization state");
    check(glGetError() == GL_NO_ERROR, "Deferred diagnostics leave valid OpenGL state");
    std::cout << checks << " deferred rendering checks passed\n";
}
} // namespace

// Creates a hidden core-profile context and releases resources before destroying it.
int main(int argc, char** argv){
    if (argc != 2 || !glfwInit()){
        return 1;
    }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    auto* window = glfwCreateWindow(width, height, "Deferred rendering tests", nullptr, nullptr);
    if (!window){
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(0);
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
