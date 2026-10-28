#include <GLFW/glfw3.h>
#include <algorithm>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>
#include <stdexcept>
#include <vr/DeferredRenderer.h>
#include <vr/EnvironmentMap.h>
#include <vr/FramebufferImage.h>
#include <vr/FramebufferState.h>
#include <vr/GLState.h>
#include <vr/LightCollector.h>
#include <vr/ModelInstance.h>
#include <vr/Reader.h>
#include <vr/SceneGraph.h>

namespace{
constexpr int width = 400, height = 300;

void check(bool condition, const char* message){
    if (!condition){
        throw std::runtime_error(message);
    }
}

void trackedState(){
    vr::FramebufferState restore;
    const auto verify = []{
        for (const auto name :
             {GL_DRAW_FRAMEBUFFER_BINDING, GL_READ_FRAMEBUFFER_BINDING, GL_VERTEX_ARRAY_BINDING,
              GL_VIEWPORT, GL_DEPTH_FUNC, GL_POLYGON_MODE, GL_FRONT_FACE, GL_BLEND_SRC_RGB,
              GL_BLEND_DST_RGB, GL_BLEND_SRC_ALPHA, GL_BLEND_DST_ALPHA, GL_BLEND_EQUATION_RGB,
              GL_BLEND_EQUATION_ALPHA}){
            GLint actual[4]{}, cached[4]{};
            glGetIntegerv(name, actual);
            vr::GLState::getIntegerv(name, cached);
            check(std::equal(actual, actual + 4, cached),
                  "Tracked integer GL state matches the driver");
        }
        for (const auto name :{GL_COLOR_WRITEMASK, GL_DEPTH_WRITEMASK}){
            GLboolean actual[4]{}, cached[4]{};
            glGetBooleanv(name, actual);
            vr::GLState::getBooleanv(name, cached);
            check(std::equal(actual, actual + 4, cached), "Tracked GL masks match the driver");
        }
        for (const auto name :
             {GL_MIN_SAMPLE_SHADING_VALUE, GL_POLYGON_OFFSET_FACTOR, GL_POLYGON_OFFSET_UNITS}){
            GLfloat actual = 0, cached = 0;
            glGetFloatv(name, &actual);
            vr::GLState::getFloatv(name, &cached);
            check(actual == cached, "Tracked floating point GL state matches the driver");
        }
        GLdouble actual = 0, cached = 0;
        glGetDoublev(GL_DEPTH_CLEAR_VALUE, &actual);
        vr::GLState::getDoublev(GL_DEPTH_CLEAR_VALUE, &cached);
        check(actual == cached, "Tracked clear depth matches the driver");
        for (const auto flag :{GL_DEPTH_TEST, GL_BLEND, GL_CULL_FACE, GL_SCISSOR_TEST,
                                GL_POLYGON_OFFSET_FILL, GL_MULTISAMPLE, GL_SAMPLE_SHADING}){
            check(glIsEnabled(flag) == vr::GLState::isEnabled(flag),
                  "Tracked GL enables match the driver");
        }
    };
    verify(); // Lazily captures pre-existing caller state.
    {
        vr::FramebufferState saved;
        vr::GLState::viewport(3, 5, 177, 123);
        vr::GLState::depthFunc(GL_GREATER);
        vr::GLState::depthMask(GL_FALSE);
        vr::GLState::colorMask(GL_TRUE, GL_FALSE, GL_TRUE, GL_FALSE);
        vr::GLState::clearDepth(0.25);
        vr::GLState::frontFace(GL_CW);
        vr::GLState::blendFunc(GL_ONE, GL_ZERO);
        vr::GLState::blendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ZERO);
        vr::GLState::blendEquation(GL_FUNC_ADD);
        vr::GLState::blendEquationSeparate(GL_FUNC_SUBTRACT, GL_FUNC_ADD);
        vr::GLState::minSampleShading(0.5f);
        vr::GLState::polygonMode(GL_FRONT_AND_BACK, GL_LINE);
        vr::GLState::polygonOffset(1.5f, 3);
        vr::GLState::enable(GL_BLEND);
        vr::GLState::disable(GL_CULL_FACE);
        GLuint targets[2]{}, array = 0;
        glGenFramebuffers(2, targets);
        glGenVertexArrays(1, &array);
        vr::GLState::bindFramebuffer(GL_DRAW_FRAMEBUFFER, targets[0]);
        vr::GLState::bindFramebuffer(GL_READ_FRAMEBUFFER, targets[1]);
        vr::GLState::bindVertexArray(array);
        verify();
        vr::GLState::deleteFramebuffers(2, targets);
        vr::GLState::deleteVertexArrays(1, &array);
        verify(); // Deleting a bound object implicitly binds zero.
    }
    verify(); // Guards restore both the driver and its tracked values.
    glViewport(9, 11, 200, 150);
    vr::GLState::invalidate();
    verify(); // External raw GL calls can explicitly invalidate the owned context cache.
    check(glGetError() == GL_NO_ERROR, "GL state tracking and restoration remain valid");
}

void lights(){
    auto root = std::make_shared<vr::Group>();
    auto light = std::make_shared<vr::Light>();
    for (float x :{-3.0f, 4.0f}){
        auto transform = std::make_shared<vr::Transform>();
        transform->setMatrix(glm::translate(glm::mat4(1),{x, 0, 0}));
        transform->addChild(light);
        root->addChild(transform);
    }
    auto lod = std::make_shared<vr::LOD>();
    auto nearLight = std::make_shared<vr::Light>();
    auto farLight = std::make_shared<vr::Light>();
    lod->addLevel(5, nearLight);
    lod->addLevel(100, farLight);
    root->addChild(lod);
    const auto near = vr::collectLights(*root,{0, 0, 0});
    const auto far = vr::collectLights(*root,{0, 0, 20});
    check(near.size() == 3 && near[0].node == light.get() && near[1].node == light.get() &&
              near[0].model[3].x == -3 && near[1].model[3].x == 4 &&
              near[2].node == nearLight.get() && far[2].node == farLight.get(),
          "Light-only traversal preserves shared DAG placements and camera-selected LOD");
}

void caches(){
    auto instance = vr::ModelInstance::create("with attachment", vr::GeometryFactory::createBox());
    auto attachment = std::make_shared<vr::Transform>();
    attachment->setMatrix(glm::translate(glm::mat4(1),{10, 0, 0}));
    attachment->addChild(vr::GeometryFactory::createBox());
    unsigned updates = 0;
    attachment->setUpdateCallback(std::make_shared<vr::FunctionCallback>(
        [&](vr::Node&, const vr::UpdateContext&){ ++updates; }));
    instance->addChild(attachment);
    instance->setSubdivided(true);
    const auto beforeUpdates = updates;
    vr::UpdateVisitor updater;
    updater.update(*instance,{0, 0, false});
    check(updates == beforeUpdates + 1 && instance->getBound().getMaximum().x > 10,
          "Variant selection retains attached nodes in updates and culling bounds");
    vr::Transform transform;
    const auto a = glm::translate(glm::mat4(1),{2, 0, 0});
    const auto b = glm::translate(glm::mat4(1),{-3, 1, 0});
    transform.setMatrix(glm::scale(glm::mat4(1),{2, 3, 4}));
    const auto revision = transform.getMatrixRevision();
    transform.setMatrix(transform.getMatrix());
    check(transform.getMatrixRevision() == revision, "Identical transforms retain their revision");
    for (int i = 0; i < 20; ++i){
        check(transform.getWorldMatrix(a) == a * transform.getMatrix() &&
                  transform.getWorldMatrix(b) == b * transform.getMatrix(),
              "A shared transform caches both DAG placements without mixing their parents");
    }
    transform.setMatrix(glm::translate(glm::mat4(1),{0, 2, 0}));
    check(transform.getWorldMatrix(a) == a * transform.getMatrix(),
          "Local edits invalidate world matrices");
    auto parent = std::make_shared<vr::Material>(), local = std::make_shared<vr::Material>();
    local->setRoughness(0.6f);
    auto first = local->resolve(parent);
    check(local->resolve(parent) == first, "Unchanged inherited materials reuse a resolved object");
    parent->setDiffuse({0.2f, 0.3f, 0.4f});
    auto second = local->resolve(parent);
    check(second != first && second->getDiffuse() == parent->getDiffuse(),
          "Parent edits invalidate material resolution");
    local->setRoughness(0.2f);
    auto third = local->resolve(parent);
    check(third->getRoughness() == 0.2f, "Local edits invalidate material resolution");
    third->setDiffuse({1, 0, 0});
    check(local->resolve(parent)->getDiffuse() == parent->getDiffuse(),
          "Editing resolved output cannot poison later inheritance");
    *parent = vr::Material{};
    check(local->resolve(parent)->getDiffuse() == parent->getDiffuse(),
          "Replacing an existing material invalidates cached inheritance");
}

void rendering(const std::filesystem::path& path){
    auto program = std::make_shared<vr::Program>(path / "shaders/phong-shading.vs",
                                                 path / "shaders/phong-shading.fs");
    check(program->supportsInstancing(),
          "The shared forward/G-buffer vertex shader supports instancing");
    program->use();
    const auto sampler = glGetUniformLocation(program->getId(), "uTextures[0]");
    check(sampler >= 0, "Uniform-cache regression uses a live sampler array");
    for (int value :{1, 1, 2, 0}){
        program->set("uTextures", value);
        GLint actual = -1;
        glGetUniformiv(program->getId(), sampler, &actual);
        check(actual == value, "Uniform cache respects repeated values and array aliases");
        program->set("uTextures[0]", 3);
    }
    vr::RenderVisitor renderer(program);
    renderer.setCulling(false);
    vr::DeferredRenderer deferred(path);
    deferred.resize(width, height);
    deferred.setBloom(false);
    deferred.setAmbientOcclusion(false);
    deferred.setDepthOfField(false);
    vr::Scene scene;
    const auto root = scene.getRoot();
    auto state = std::make_shared<vr::State>();
    state->setUniform("uFog", false);
    state->setCullFace(false);
    root->setState(state);
    auto light = std::make_shared<vr::Light>();
    light->setPosition({-2, 4, 6, 1});
    root->addChild(light);
    auto mesh = vr::GeometryFactory::createBox();
    auto material = std::make_shared<vr::Material>();
    material->setDiffuse({0.5f, 0.7f, 0.2f});
    auto meshState = std::make_shared<vr::State>();
    meshState->setMaterial(material);
    mesh->setState(meshState);
    auto normal =
        std::make_shared<vr::Texture>(1, 1, std::vector<unsigned char>{170, 145, 245, 255});
    meshState->setNormalMap(normal);
    for (int i = 0; i < 8; ++i){
        auto transform = std::make_shared<vr::Transform>();
        auto model = glm::translate(glm::mat4(1),{static_cast<float>(i % 4) * 1.4f - 2.1f,
                                                   static_cast<float>(i / 4) * 1.4f - 0.7f, 0});
        model = glm::rotate(model, 0.15f * i,{0, 1, 0});
        model = glm::scale(model,{i % 2 ? -0.7f : 0.7f, 0.8f, 0.5f});
        if (i == 2){
            model[1][0] += 0.2f;
        }
        transform->setMatrix(model);
        transform->addChild(mesh);
        root->addChild(transform);
        if (i >= 5){
            auto local = std::make_shared<vr::State>();
            if (i == 5){
                local->setUniform("uTint", glm::vec3(1, 0.5f, 0.3f));
            }
            if (i == 6){
                local->setLightEnabled(0, false);
                local->setUniform("uLodRange", glm::vec2(0, 0.6f));
            }
            if (i == 7){
                local->setBlend(true);
                local->setUniform("uOpacity", 0.5f);
            }
            transform->setState(local);
        }
    }
    const auto camera = scene.getCamera();
    camera->setPerspective(48, static_cast<float>(width) / height, 0.1f, 30);
    camera->setLookAt({0, 1, 9},{0, 0, 0});
    const auto render = [&](bool batching, bool useDeferred){
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, width, height);
        glEnable(GL_DEPTH_TEST);
        glDepthMask(GL_TRUE);
        glClearColor(0.03f, 0.04f, 0.05f, 1);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        vr::GLState::invalidate(); // This test's caller intentionally uses raw GL setup.
        renderer.setInstancing(batching);
        renderer.render(*root, *camera, nullptr, nullptr, useDeferred ? &deferred : nullptr);
        std::vector<unsigned char> result(width * height * 3);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, result.data());
        return result;
    };
    for (const bool useDeferred :{false, true}){
        const auto reference = render(false, useDeferred);
        const auto before = renderer.getStats();
        const auto batched = render(true, useDeferred);
        const auto after = renderer.getStats();
        unsigned different = 0;
        for (std::size_t i = 0; i < reference.size(); ++i){
            different += std::abs(int(reference[i]) - int(batched[i])) > 2;
        }
        check(different == 0, "Instancing preserves pixels for tint, light masks, transparency, "
                              "LOD, normal maps and mirrored/nonuniform transforms");
        check(after.draws < before.draws && after.instancedDraws > 0 &&
                  after.instances == before.instances && after.triangles == before.triangles &&
                  after.transparentDraws == before.transparentDraws,
              "Instancing reduces real draw calls while retaining instance/triangle/transparency "
              "counts");
        std::cout << (useDeferred ? "Deferred" : "Forward") << " draws: " << before.draws << " -> "
                  << after.draws << '\n';
    }
    // Edits after a cached traversal must still reach all instances on the next frame.
    material->setDiffuse({0.8f, 0.15f, 0.3f});
    const auto changed = render(true, false);
    check(changed == render(false, false),
          "Cached material inheritance respects later source edits");
    check(glGetError() == GL_NO_ERROR,
          "Batching keeps valid GL state across forward and deferred passes");
}

void carRendering(const std::filesystem::path& path){
    vr::Reader reader(path);
    auto modelState = std::make_shared<vr::State>();
    modelState->setUniform("uEnvironmentMaterial", true);
    auto paintState = std::make_shared<vr::State>();
    auto paint = std::make_shared<vr::Material>();
    paint->setDiffuse({0.807f, 0.1f, 0.005f});
    paint->setMetallic(0.88f);
    paint->setRoughness(0.18f);
    paint->setSpecular({0.75f, 0.75f, 0.75f});
    paint->setShininess(700);
    paintState->setMaterial(paint);
    vr::Scene scene;
    auto state = std::make_shared<vr::State>();
    state->setUniform("uFog", false);
    scene.getRoot()->setState(state);
    scene.getRoot()->addChild(reader.readModel(
        "models/porsche_911_gt3.glb", true,{{"*", modelState},{"EXT_Carpaint.004", paintState}}));
    auto program = std::make_shared<vr::Program>(path / "shaders/phong-shading.vs",
                                                 path / "shaders/phong-shading.fs");
    vr::EnvironmentMap environment(path,
                                   "textures/sky/kloofendal_48d_partly_cloudy_puresky_2k.hdr");
    vr::RenderVisitor renderer(program);
    renderer.setCulling(false);
    renderer.setEnvironment(&environment);
    vr::DeferredRenderer deferred(path);
    deferred.resize(width, height);
    deferred.setPreviews(false);
    deferred.setAmbientOcclusion(false);
    deferred.setEnvironment(&environment);
    const auto camera = scene.getCamera();
    camera->setPerspective(48, float(width) / height, 0.1f, 40);
    const auto render = [&](bool batching, bool useDeferred){
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, width, height);
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);
        glDepthMask(GL_TRUE);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        vr::GLState::invalidate();
        renderer.setInstancing(batching);
        renderer.render(*scene.getRoot(), *camera, nullptr, nullptr,
                        useDeferred ? &deferred : nullptr);
        return vr::FramebufferImage::capture(0, 0, width, height);
    };
    for (const bool useDeferred :{false, true}){
        camera->setLookAt({5, 2.5f, 6},{0, 0.7f, 0});
        renderer.setShadingMode(vr::ShadingMode::CookTorrance);
        const auto reference = render(false, useDeferred);
        for (auto mode :
             {vr::ShadingMode::Phong, vr::ShadingMode::BlinnPhong, vr::ShadingMode::CookTorrance}){
            renderer.setShadingMode(mode);
            const auto batched = render(true, useDeferred);
            const auto changed = reference.countChangedPixels(batched, 2);
            check(changed == 0, "The imported car's environment reflections preserve material "
                                "color and roughness in every lighting mode");
        }
    }
}
} // namespace

int main(int argc, char** argv){
    if (argc != 2 || !glfwInit()){
        return 1;
    }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    auto* window =
        glfwCreateWindow(width, height, "Scene graph performance tests", nullptr, nullptr);
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
            caches();
            lights();
            carRendering(argv[1]);
            rendering(argv[1]);
            {
                vr::GLState::Scope trackedContext;
                trackedState();
                carRendering(argv[1]);
                rendering(argv[1]);
            }
            std::cout << "Scene graph performance regressions passed\n";
        } catch (const std::exception& error){
            std::cerr << error.what() << '\n';
            result = 1;
        }
    }
    glfwDestroyWindow(window);
    glfwTerminate();
    return result;
}
