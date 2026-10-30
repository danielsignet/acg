#include <GLFW/glfw3.h>
#include <algorithm>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vr/DeferredRenderer.h>
#include <vr/EnvironmentMap.h>
#include <vr/ModelInstance.h>
#include <vr/PlanarReflection.h>
#include <vr/ReflectionProbes.h>
#include <vr/SceneGraph.h>

namespace{
constexpr int width = 256, height = 192;

// Reports a failed material, reflection or image contract to CTest.
void check(bool value, const char* message){
    if (!value){
        throw std::runtime_error(message);
    }
}

// Reads the presented image directly, without generating repository artifacts.
std::vector<unsigned char> pixels(){
    std::vector<unsigned char> result(width * height * 4);
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, result.data());
    return result;
}

// Counts visible changes beyond small half-float and color-buffer quantization differences.
unsigned changed(const std::vector<unsigned char>& a, const std::vector<unsigned char>& b){
    unsigned count = 0;
    for (std::size_t i = 0; i < a.size(); i += 4){
        count += std::abs(static_cast<int>(a[i]) - b[i]) > 3 ||
                 std::abs(static_cast<int>(a[i + 1]) - b[i + 1]) > 3 ||
                 std::abs(static_cast<int>(a[i + 2]) - b[i + 2]) > 3;
    }
    return count;
}

// Exercises the BRDFs, packed material parameters, shared IBL and an infinite-distance sky.
void materials(const std::filesystem::path& root){
    vr::Material parent, local;
    parent.setRoughness(0.8f);
    parent.setMetallic(1);
    local.setRoughness(0);
    local.setMetallic(0);
    const auto merged = parent.merged(local);
    check(merged.getRoughness() == 0 && merged.getMetallic() == 0,
          "Explicit zero material properties override inherited roughness and metallic");
    for (float value :{-0.1f, 1.1f, std::numeric_limits<float>::quiet_NaN()}){
        bool roughnessRejected = false, metallicRejected = false;
        try{
            local.setRoughness(value);
        } catch (const std::invalid_argument&){
            roughnessRejected = true;
        }
        try{
            local.setMetallic(value);
        } catch (const std::invalid_argument&){
            metallicRejected = true;
        }
        check(roughnessRejected && metallicRejected,
              "Material parameters reject invalid and nonfinite values");
    }
    auto program = std::make_shared<vr::Program>(root / "shaders/phong-shading.vs",
                                                 root / "shaders/phong-shading.fs");
    vr::Scene scene;
    auto state = std::make_shared<vr::State>();
    state->setUniform("uFog", false);
    auto material = std::make_shared<vr::Material>();
    material->setDiffuse({0.6f, 0.14f, 0.035f});
    material->setAmbient({0, 0, 0});
    material->setShininess(24);
    material->setRoughness(0.35f);
    state->setMaterial(material);
    scene.getRoot()->setState(state);
    scene.getRoot()->addChild(vr::GeometryFactory::createSphere(48, 32));
    auto light = std::make_shared<vr::Light>();
    light->setPosition({0.5f, 0.6f, 1, 0});
    light->setAmbient({0, 0, 0});
    light->setDiffuse({2, 2, 2});
    light->setSpecular({2, 2, 2});
    scene.getRoot()->addChild(light);
    const auto camera = scene.getCamera();
    camera->setPerspective(50, static_cast<float>(width) / height, 0.1f, 40);
    camera->setLookAt({0, 0, 4},{0, 0, 0});
    vr::RenderVisitor renderer(program);
    vr::DeferredRenderer deferred(root);
    deferred.resize(width, height);
    deferred.setPreviews(false);
    const auto render = [&](bool useDeferred = true){
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, width, height);
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);
        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);
        glClearColor(0.035f, 0.052f, 0.078f, 1);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        renderer.render(*scene.getRoot(), *camera, nullptr, nullptr,
                        useDeferred ? &deferred : nullptr);
        check(glGetError() == GL_NO_ERROR, "Every BRDF and environment pass has valid GL state");
        return pixels();
    };
    renderer.setShadingMode(vr::ShadingMode::Phong);
    const auto phong = render();
    renderer.setShadingMode(vr::ShadingMode::BlinnPhong);
    const auto blinn = render();
    check(changed(phong, blinn) > 100,
          "Blinn-Phong uses a visibly different half-vector highlight");
    renderer.setShadingMode(vr::ShadingMode::CookTorrance);
    const auto cook = render();
    check(changed(cook, blinn) > 100, "Cook-Torrance produces distinct microfacet lighting");
    check(changed(cook, render(false)) < 100,
          "Forward and deferred evaluate the same Cook-Torrance BRDF");
    material->setRoughness(0.08f);
    const auto polished = render();
    material->setRoughness(0.8f);
    check(changed(polished, render()) > 100,
          "Roughness changes the actual Cook-Torrance highlight");
    material->setMetallic(1);
    check(changed(cook, render()) > 100,
          "Metallic changes diffuse/specular energy and reflection color");
    const auto normals = deferred.getGBuffer().read(vr::GBuffer::Normal);
    const auto albedo = deferred.getGBuffer().read(vr::GBuffer::Albedo);
    const auto center = (height / 2 * width + width / 2) * 4;
    check(std::abs(normals[center + 3] - 0.8f) < 0.001f && albedo[center + 3] == 1,
          "Roughness and metallic survive the existing G-buffer without extra attachments");
    vr::EnvironmentMap environment(root,
                                   "textures/sky/kloofendal_48d_partly_cloudy_puresky_2k.hdr");
    renderer.setEnvironment(&environment);
    deferred.setEnvironment(&environment);
    state->setUniform("uEnvironmentMaterial", true);
    light->setEnabled(false);
    const auto environmentOnly = render();
    check(
        environmentOnly[center] > 20 && glm::length(environment.getSunDirection()) > 0.99f,
        "The HDRI illuminates metal without point lights and supplies a normalized sun direction");
    check(changed(environmentOnly, render(false)) < 120,
          "Environment lighting and sky agree between forward and deferred rendering");
    for (auto mode :{vr::ShadingMode::Phong, vr::ShadingMode::BlinnPhong}){
        renderer.setShadingMode(mode);
        check(changed(environmentOnly, render()) < 5 &&
                  changed(environmentOnly, render(false)) < 120,
              "Metallic paint retains its colored environment reflection across highlight models");
    }
    renderer.setShadingMode(vr::ShadingMode::CookTorrance);
    camera->setLookAt({0.2f, 0, 4},{0.2f, 0, 0});
    const auto translated = render();
    const auto corner = ((height - 12) * width + 12) * 4;
    check(std::equal(environmentOnly.begin() + corner, environmentOnly.begin() + corner + 3,
                     translated.begin() + corner),
          "Sky directions are independent of camera translation");
    deferred.setAntialiasing(vr::AntialiasingMode::Msaa);
    render();
}

// Checks actual scene-color transmission, refraction and foreground depth in both renderers.
void glass(const std::filesystem::path& root){
    auto program = std::make_shared<vr::Program>(root / "shaders/phong-shading.vs",
                                                 root / "shaders/phong-shading.fs");
    vr::Scene scene;
    auto state = std::make_shared<vr::State>();
    state->setLighting(false);
    state->setUniform("uFog", false);
    auto material = std::make_shared<vr::Material>();
    material->setDiffuse(glm::vec3(1));
    state->setMaterial(material);
    scene.getRoot()->setState(state);
    auto background = std::make_shared<vr::Transform>();
    background->setMatrix(glm::scale(glm::translate(glm::mat4(1),{0, 0, -2}),{7, 5, 0.1f}));
    auto backgroundState = std::make_shared<vr::State>();
    std::vector<unsigned char> checker(8 * 8 * 4, 255);
    for (unsigned y = 0; y < 8; ++y){
        for (unsigned x = 0; x < 8; ++x){
            const auto index = (y * 8 + x) * 4;
            checker[index] = (x + y) % 2 ? 255 : 20;
            checker[index + 1] = (x + y) % 2 ? 30 : 180;
        }
    }
    backgroundState->setTexture(0, std::make_shared<vr::Texture>(8, 8, std::move(checker)));
    background->setState(backgroundState);
    background->addChild(vr::GeometryFactory::createBox());
    scene.getRoot()->addChild(background);
    auto sphere = std::make_shared<vr::Transform>();
    auto glassState = std::make_shared<vr::State>();
    glassState->setBlend(true);
    glassState->setUniform("uGlass", true);
    glassState->setUniform("uIor", 1.0f);
    sphere->setState(glassState);
    sphere->addChild(vr::GeometryFactory::createSphere(48, 32));
    scene.getRoot()->addChild(sphere);
    auto foreground = std::make_shared<vr::Transform>();
    foreground->setMatrix(glm::scale(glm::translate(glm::mat4(1),{0, 0, 2}),{0.3f, 0.3f, 0.3f}));
    foreground->addChild(vr::GeometryFactory::createBox());
    foreground->setVisible(false);
    scene.getRoot()->addChild(foreground);
    const auto camera = scene.getCamera();
    camera->setPerspective(50, static_cast<float>(width) / height, 0.1f, 40);
    camera->setLookAt({0, 0, 5},{0, 0, 0});
    vr::RenderVisitor renderer(program);
    vr::DeferredRenderer deferred(root);
    deferred.resize(width, height);
    deferred.setPreviews(false);
    deferred.setAmbientOcclusion(false);
    for (const auto mode :
         {vr::AntialiasingMode::Off, vr::AntialiasingMode::Fxaa, vr::AntialiasingMode::Msaa}){
        deferred.setAntialiasing(mode);
        for (bool useDeferred :{false, true}){
            const auto render = [&]{
                glBindFramebuffer(GL_FRAMEBUFFER, 0);
                glViewport(0, 0, width, height);
                glEnable(GL_DEPTH_TEST);
                glDepthFunc(GL_LESS);
                glDepthMask(GL_TRUE);
                glDisable(GL_BLEND);
                glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
                const auto draw = [&]{
                    renderer.render(*scene.getRoot(), *camera, nullptr, nullptr,
                                    useDeferred ? &deferred : nullptr);
                };
                if (useDeferred){
                    draw();
                } else{
                    deferred.renderForward(draw);
                }
                check(glGetError() == GL_NO_ERROR,
                      "Glass scene-color capture works in forward, deferred and every AA mode");
                return pixels();
            };
            sphere->setVisible(false);
            const auto unobstructed = render();
            sphere->setVisible(true);
            glassState->setUniform("uIor", 1.0f);
            check(changed(unobstructed, render()) < 80,
                  "Index one transmits the existing scene without adding an opaque sphere");
            glassState->setUniform("uIor", 1.5f);
            check(changed(unobstructed, render()) > 300,
                  "Glass bends the actual checkerboard behind both sphere interfaces");
            foreground->setVisible(true);
            sphere->setVisible(false);
            const auto occluder = render();
            sphere->setVisible(true);
            const auto result = render();
            const auto center = (height / 2 * width + width / 2) * 4;
            check(std::equal(occluder.begin() + center, occluder.begin() + center + 3,
                             result.begin() + center),
                  "Opaque foreground geometry keeps its depth in front of the glass");
            foreground->setVisible(false);
        }
    }
}

// Verifies mirror-plane geometry, independent visibility, clipping and surface presentation.
void mirrors(const std::filesystem::path& root){
    const glm::vec4 plane(0, 0, 1, 2);
    const auto reflection = vr::PlanarReflection::reflectionMatrix(plane);
    const glm::vec4 point(1, 2, 3, 1);
    check(glm::distance(reflection * point, glm::vec4(1, 2, -7, 1)) < 1e-6f &&
              glm::distance(reflection * reflection * point, point) < 1e-6f,
          "Translated mirror reflection is an involution and reverses signed plane distance");
    auto program = std::make_shared<vr::Program>(root / "shaders/phong-shading.vs",
                                                 root / "shaders/phong-shading.fs");
    vr::Scene scene;
    auto state = std::make_shared<vr::State>();
    state->setLighting(false);
    state->setUniform("uFog", false);
    scene.getRoot()->setState(state);
    auto mirror = std::make_shared<vr::Transform>("materialMirror");
    mirror->setMatrix(glm::scale(glm::translate(glm::mat4(1),{0, -1, 0}),{2, 1, 1}));
    auto mirrorState = std::make_shared<vr::State>();
    mirrorState->setUniform("uMirror", true);
    mirrorState->setCullFace(false);
    mirror->setState(mirrorState);
    mirror->addChild(vr::GeometryFactory::createAlphaTestPanel());
    scene.getRoot()->addChild(mirror);
    scene.registerTransform(mirror);
    for (int side :{-1, 1}){
        auto object = std::make_shared<vr::Transform>();
        object->setMatrix(
            glm::scale(glm::translate(glm::mat4(1),{0, 0, side * 2.0f}),{0.7f, 0.7f, 0.7f}));
        auto objectState = std::make_shared<vr::State>();
        auto material = std::make_shared<vr::Material>();
        material->setDiffuse(side > 0 ? glm::vec3(1, 0, 0) : glm::vec3(0, 0, 1));
        objectState->setMaterial(material);
        object->setState(objectState);
        object->addChild(vr::GeometryFactory::createBox());
        scene.getRoot()->addChild(object);
    }
    const auto camera = scene.getCamera();
    camera->setPerspective(50, static_cast<float>(width) / height, 0.1f, 40);
    camera->setLookAt({2, 0, 5},{0, 0, 0});
    vr::PlanarReflection planar(program);
    planar.resize(width, height, vr::AntialiasingMode::Off);
    planar.render(scene, *camera, nullptr, vr::ShadingMode::Phong, true, false);
    check(planar.getRendered() && planar.getStats().draws == 2,
          "The reflected camera collects its own objects while excluding the mirror");
    std::vector<float> radiance(width * height * 4);
    planar.getTexture()->apply(0);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_FLOAT, radiance.data());
    unsigned red = 0, blue = 0;
    for (std::size_t i = 0; i < radiance.size(); i += 4){
        red += radiance[i] > 0.5f && radiance[i + 2] < 0.1f;
        blue += radiance[i + 2] > 0.5f && radiance[i] < 0.1f;
    }
    check(red > 50 && blue == 0,
          "The mirror sees foreground geometry and clips geometry behind its plane");
    vr::RenderVisitor renderer(program);
    renderer.setReflection(&planar);
    vr::DeferredRenderer deferred(root);
    deferred.resize(width, height);
    deferred.setPreviews(false);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, width, height);
    renderer.render(*scene.getRoot(), *camera, nullptr, nullptr, &deferred);
    const auto emissive = deferred.getGBuffer().read(vr::GBuffer::Emissive);
    unsigned reflectedPixels = 0;
    for (std::size_t i = 0; i < emissive.size(); i += 4){
        reflectedPixels += emissive[i] > 0.5f;
    }
    check(reflectedPixels > 10, "Deferred mirror pixels contain the real reflected HDR image");
    planar.resize(width, height, vr::AntialiasingMode::Msaa);
    planar.render(scene, *camera, nullptr, vr::ShadingMode::CookTorrance, true, false);
    check(planar.getTexture() && planar.getTexture()->getSamples() == 1,
          "MSAA mirror captures resolve before the main scene samples them");
    camera->setLookAt({0, 0, -5},{0, 0, 0});
    planar.render(scene, *camera, nullptr, vr::ShadingMode::Phong, true, false);
    check(!planar.getRendered() && !planar.getTexture(),
          "The back of a mirror never reuses a stale capture");
    check(glGetError() == GL_NO_ERROR, "Mirror capture and presentation leave valid OpenGL state");
}

// Checks actual captured scene radiance, owner exclusion and complete-map publication.
void localReflections(const std::filesystem::path& root){
    auto program = std::make_shared<vr::Program>(root / "shaders/phong-shading.vs",
                                                 root / "shaders/phong-shading.fs");
    vr::EnvironmentMap environment(root,
                                   "textures/sky/kloofendal_48d_partly_cloudy_puresky_2k.hdr");
    vr::Scene scene;
    auto owner = vr::ModelInstance::create("probe owner", vr::GeometryFactory::createBox());
    auto ownerState = std::make_shared<vr::State>();
    ownerState->setUniform("uEnvironmentMaterial", true);
    ownerState->setLighting(false);
    ownerState->setCullFace(false);
    auto ownerMaterial = std::make_shared<vr::Material>();
    ownerMaterial->setDiffuse({0, 1, 0});
    ownerState->setMaterial(ownerMaterial);
    owner->setState(ownerState);
    scene.getRoot()->addChild(owner);
    auto wall = std::make_shared<vr::Transform>();
    wall->setMatrix(glm::scale(glm::translate(glm::mat4(1),{0, 0, 3}),{4, 4, 0.2f}));
    auto wallState = std::make_shared<vr::State>();
    auto wallMaterial = std::make_shared<vr::Material>();
    wallMaterial->setDiffuse({1, 0, 0});
    wallState->setMaterial(wallMaterial);
    wallState->setLighting(false);
    wallState->setUniform("uFog", false);
    wall->setState(wallState);
    wall->addChild(vr::GeometryFactory::createBox());
    scene.getRoot()->addChild(wall);
    const auto camera = scene.getCamera();
    camera->setPerspective(50, 1, 0.1f, 150);
    camera->setLookAt({0, 1, 5},{0, 0, 0});
    vr::ReflectionProbes probes(scene, environment, program);
    environment.setReflectionMode(vr::ReflectionMode::Cubemaps);
    check(probes.getProbeCount() == 1, "Only eligible model instances receive local cubemaps");
    for (unsigned face = 0; face < 6; ++face){
        probes.update(scene, *camera, environment, vr::ShadingMode::CookTorrance,{}, false);
        check(probes.getFaces() == 1 && owner->getVisible(),
              "Each update captures at most one face and restores the excluded owner");
        check(probes.getReadyCount() == 0,
              "Captured cube faces remain unpublished until incremental filtering finishes");
    }
    unsigned filterFrames=0;
    while(!probes.getReadyCount() && filterFrames++<24){
        check(probes.plan(scene,*camera,{},false)==nullptr,
              "Filtering does not wake a capture camera or its shadow dependencies");
        probes.update(scene,*camera,environment,vr::ShadingMode::CookTorrance,{},false);
        check(probes.getFaces()==0 && probes.getFilterDraws()>0 && probes.getFilterDraws()<=18,
              "Each filter frame obeys its submission budget without recapturing scene faces");
    }
    check(probes.getReadyCount()==1 && filterFrames>6 && filterFrames<24,
          "A complete filtered cube becomes available within a bounded number of frames");
    const auto sample = [&]{
        program->use();
        environment.apply(*program);
        glActiveTexture(GL_TEXTURE14);
        GLint faces = 0;
        glGetTexLevelParameteriv(GL_TEXTURE_CUBE_MAP_ARRAY, 0, GL_TEXTURE_DEPTH, &faces);
        std::vector<float> data(256 * 256 * faces * 4);
        glGetTexImage(GL_TEXTURE_CUBE_MAP_ARRAY, 0, GL_RGBA, GL_FLOAT, data.data());
        const auto center = ((6 + 4) * 256 * 256 + 128 * 256 + 128) * 4;
        return glm::vec3(data[center], data[center + 1], data[center + 2]);
    };
    const auto red = sample();
    check(red.r > 0.9f && red.g < 0.1f && red.b < 0.1f,
          "A local cubemap contains the red scene wall rather than its own green model");
    probes.update(scene, *camera, environment, vr::ShadingMode::CookTorrance,{}, false);
    check(probes.getFaces() == 0, "A complete frozen probe reuses its cached radiance");
    wallMaterial->setDiffuse({0, 0, 1});
    probes.invalidate();
    for (unsigned face = 0; face < 6; ++face){
        probes.update(scene, *camera, environment, vr::ShadingMode::CookTorrance,{}, false);
        check(glm::distance(sample(), red) < 0.001f,
                  "Updating one face never exposes a mixture of old and new cube faces");
    }
    bool published=false;
    for(unsigned frame=0;frame<24 && !published;++frame){
        probes.update(scene,*camera,environment,vr::ShadingMode::CookTorrance,{},false);
        const auto value=sample();
        published=glm::distance(value,red)>=.001f;
        check(!published || (value.b>.9f && value.r<.1f),
              "The old complete reflection survives every partial filter pass until atomic publication");
    }
    check(published,"Incremental filtering eventually publishes the material change");
    const auto blue = sample();
    check(blue.b > 0.9f && blue.r < 0.1f,
          "Invalidating a local probe publishes changed scene materials");
    probes.invalidate();
    camera->setLookAt({0, 1, 5},{0, 1, 10});
    probes.update(scene, *camera, environment, vr::ShadingMode::CookTorrance,{}, true);
    check(probes.getFaces() == 0, "Offscreen reflective objects do not trigger cube captures");
    check(glGetError() == GL_NO_ERROR, "Local cubemap capture leaves valid OpenGL state");

    // Room skies consume layers from the same array as local probes. Exercise
    // both the original single-sky case and the maximum supported sky count.
    for (unsigned i = 0; i < 16; ++i){
        auto instance = vr::ModelInstance::create("extra probe", vr::GeometryFactory::createBox());
        auto state = std::make_shared<vr::State>();
        state->setUniform("uEnvironmentMaterial", true);
        instance->setState(state);
        scene.getRoot()->addChild(instance);
    }
    const auto checkLayerBudget = [&]{
        vr::ReflectionProbes limited(scene, environment, program);
        check(limited.getProbeCount() + environment.getSkyCount() == vr::Texture::MaxCubeArrayLayers,
              "Local probes reserve room for every sky within the shared cubemap layer budget");
        unsigned assigned = 0, fallback = 0;
        for (const auto& node : scene.getRoot()->getChildren()){
            const auto instance = std::dynamic_pointer_cast<vr::ModelInstance>(node);
            if (!instance) continue;
            const int id = std::get<int>(instance->getState()->getUniforms().at("uReflectionProbe"));
            check(id >= 0 && static_cast<unsigned>(id) <= limited.getProbeCount(),
                  "No model references a cubemap outside the allocated array");
            assigned += id > 0;
            fallback += id == 0;
        }
        check(assigned == limited.getProbeCount() && fallback > 0,
              "Models beyond the probe budget use their sky reflection fallback");
        program->use();
        environment.apply(*program);
        glActiveTexture(GL_TEXTURE14);
        GLint faces = 0;
        glGetTexLevelParameteriv(GL_TEXTURE_CUBE_MAP_ARRAY, 0, GL_TEXTURE_DEPTH, &faces);
        check(faces == static_cast<GLint>(vr::Texture::MaxCubeArrayLayers * 6) &&
                  glGetError() == GL_NO_ERROR,
              "The packed sky and local-probe array allocates successfully on the GPU");
    };
    checkLayerBudget();
    std::vector<vr::RoomSky> skies;
    for (unsigned i = 1; i < vr::SceneEnvironments::MaxSkies; ++i){
        skies.push_back({"sky-" + std::to_string(i),
                        "textures/sky/kloofendal_48d_partly_cloudy_puresky_2k.hdr", 0});
    }
    vr::SceneEnvironments layout;
    layout.build(std::move(skies), {});
    environment.configureRooms(root, layout);
    checkLayerBudget();

    vr::ReflectionProbes resident(scene, environment, program);
    for (const auto& child : scene.getRoot()->getChildren())
        if (std::dynamic_pointer_cast<vr::ModelInstance>(child)) child->setVisible(false);
    auto incoming = std::make_shared<vr::Group>("newly visible reflection group");
    auto groupState = std::make_shared<vr::State>();
    groupState->setUniform("uEnvironmentMaterial", true);
    groupState->setUniform("uReflectionGroup", 77);
    incoming->setState(groupState);
    auto first = vr::ModelInstance::create("incoming first", vr::GeometryFactory::createBox());
    auto second = vr::ModelInstance::create("incoming second", vr::GeometryFactory::createBox());
    incoming->addChild(first);
    incoming->addChild(second);
    scene.getRoot()->addChild(incoming);
    camera->setLookAt({0,1,5},{0,0,0});
    resident.plan(scene,*camera,{},false);
    const auto layer = std::get<int>(first->getState()->getUniforms().at("uReflectionProbe"));
    check(layer > 0 && static_cast<unsigned>(layer) <= resident.getProbeCount() &&
              std::get<int>(second->getState()->getUniforms().at("uReflectionProbe")) == layer,
          "A newly visible group reuses one cold atlas slot for all of its members");
    check(std::get<int>(owner->getState()->getUniforms().at("uReflectionProbe")) == 0,
          "Evicted owners cannot sample a slot reassigned to another room");
    const auto unchanged = first->getState()->getRevision();
    resident.plan(scene,*camera,{},false);
    check(first->getState()->getRevision() == unchanged,
          "Retained probe bindings do not invalidate material state every frame");
    check(glGetError() == GL_NO_ERROR, "Probe residency leaves valid OpenGL state");
}
} // namespace

// Runs isolated GPU checks with all resources destroyed before the hidden context.
int main(int argc, char** argv){
    if (argc != 2 || !glfwInit()){
        return 1;
    }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    auto* window = glfwCreateWindow(width, height, "Material gallery tests", nullptr, nullptr);
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
        materials(argv[1]);
        glass(argv[1]);
        mirrors(argv[1]);
        localReflections(argv[1]);
        std::cout
            << "BRDF, material inheritance, HDR environment and planar mirror checks passed\n";
    } catch (const std::exception& error){
        std::cerr << "FAILED: " << error.what() << '\n';
        result = 1;
    }
    glfwDestroyWindow(window);
    glfwTerminate();
    return result;
}
