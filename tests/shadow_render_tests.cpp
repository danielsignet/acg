#include <GLFW/glfw3.h>
#include <algorithm>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>
#include <stdexcept>
#include <vr/DeferredRenderer.h>
#include <vr/FramebufferState.h>
#include <vr/SceneGraph.h>
#include <vr/ShadowCameras.h>
#include <vr/ShadowRenderer.h>

namespace{
constexpr int width = 640, height = 480;
unsigned checks = 0;
std::unique_ptr<vr::DeferredRenderer> deferred;

// A group of shadow faces must restore caller state just like an isolated target pass.
void callerState(const std::filesystem::path& path){
    vr::FramebufferState restoreTestState;
    vr::Scene scene;
    auto sun = std::make_shared<vr::Light>();
    sun->setPosition({0, 1, 0, 0});
    scene.getRoot()->addChild(sun);
    auto point = std::make_shared<vr::Light>();
    point->setPosition({1, 3, 2, 1});
    scene.getRoot()->addChild(point);
    scene.getRoot()->addChild(vr::GeometryFactory::createBox());
    vr::ShadowRenderer shadows(path, 64, 64);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(3, 7, 120, 90);
    glDepthFunc(GL_GREATER);
    glDepthMask(GL_FALSE);
    glColorMask(GL_TRUE, GL_FALSE, GL_TRUE, GL_FALSE);
    glEnable(GL_BLEND);
    glEnable(GL_SCISSOR_TEST);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(2, 3);
    glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    shadows.render(scene,{0, 0, 5}, true, false);
    GLint viewport[4]{}, depth = 0, mode[2]{};
    GLboolean mask[4]{}, write = GL_TRUE;
    glGetIntegerv(GL_VIEWPORT, viewport);
    glGetIntegerv(GL_DEPTH_FUNC, &depth);
    glGetIntegerv(GL_POLYGON_MODE, mode);
    glGetBooleanv(GL_COLOR_WRITEMASK, mask);
    glGetBooleanv(GL_DEPTH_WRITEMASK, &write);
    if (shadows.getStats().passes != 7 || viewport[0] != 3 || viewport[1] != 7 ||
        viewport[2] != 120 || viewport[3] != 90 || depth != GL_GREATER || write != GL_FALSE ||
        !mask[0] || mask[1] || !mask[2] || mask[3] || mode[0] != GL_LINE ||
        !glIsEnabled(GL_BLEND) || !glIsEnabled(GL_SCISSOR_TEST) ||
        !glIsEnabled(GL_POLYGON_OFFSET_FILL)){
        throw std::runtime_error("A multi-face shadow batch must preserve caller raster state");
    }
    ++checks;
}

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

// Retained exterior surfaces keep their physical lighting in the transparent view.
void portalReceivers(const std::filesystem::path& path){
    auto program = std::make_shared<vr::Program>(path / "shaders/phong-shading.vs",
                                                 path / "shaders/phong-shading.fs");
    vr::ShadowRenderer shadows(path, 512, 256);
    vr::RenderVisitor renderer(program);
    renderer.setCulling(false);
    vr::Scene scene;
    auto root = scene.getRoot();
    auto state = std::make_shared<vr::State>();
    state->setUniform("uFog", false);
    root->setState(state);
    auto sun = std::make_shared<vr::Light>();
    sun->setPosition({0, 1, 0, 0});
    sun->setDiffuse({1, 1, 1});
    root->addChild(sun);
    auto floor = place(root, vr::GeometryFactory::createGround(),{0, 0, 4},{0.5f, 1, 1.0f / 3});
    floor->setRoomId("patio");
    auto caster = place(root, vr::GeometryFactory::createBox(),{-4, 1, 4},{2, 2, 2});
    caster->setRoomId("hidden");
    const auto camera = scene.getCamera();
    camera->setPerspective(65, static_cast<float>(width) / height, 0.1f, 50);
    camera->setLookAt({0, 2, -4},{0, 0, 4});
    const auto vp = camera->getProjection() * camera->getView();
    vr::PortalGraph graph;
    graph.build(
        {{"room",{{-4, 0, -8},{4, 4, 0}}}},
        {{"room", "patio",{{-1, 0, 0},{1, 3, 0}}},{"room", "patio",{{2, 0, 0},{3, 3, 0}}}},
        "patio");
    const auto clipped = graph.collect(camera->getPosition(), &vp);
    auto unbounded = clipped;
    unbounded.volumes["patio"] ={vr::ClipVolume{}};
    const auto render = [&](const vr::PortalResult& portals, bool shadowed){
        glViewport(0, 0, width, height);
        glEnable(GL_DEPTH_TEST);
        glDepthMask(GL_TRUE);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glDisable(GL_BLEND);
        glDisable(GL_SCISSOR_TEST);
        glClearColor(0, 0, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        shadows.render(scene, camera->getPosition(), shadowed, false);
        renderer.render(*root, *camera, nullptr, &portals, deferred.get());
        return pixels();
    };
    const auto sample = [&](const auto& image, const glm::vec3& world){
        const auto clip = vp * glm::vec4(world, 1);
        const auto ndc = glm::vec3(clip) / clip.w;
        const int x = static_cast<int>((ndc.x * 0.5f + 0.5f) * width);
        const int y = static_cast<int>((ndc.y * 0.5f + 0.5f) * height);
        check(x >= 0 && x < width && y >= 0 && y < height, "Portal sample is on screen");
        const auto index = (y * width + x) * 3;
        return glm::ivec3(image[index], image[index + 1], image[index + 2]);
    };
    const auto reference = render(unbounded, true);
    const auto physicalDepth = shadows.getSunTarget()->readDepth(0);
    const auto unshadowed = render(unbounded, false);
    check(sample(reference,{-4, 0, 4}) != sample(unshadowed,{-4, 0, 4}),
          "Off-camera geometry casts a physical shadow on the retained exterior floor");
    const auto culled = render(clipped, true);
    check(shadows.getSunTarget()->readDepth(0) == physicalDepth && shadows.getCasters().size() == 2,
          "Observer visibility preserves the physical depth map and off-camera shadow caster");
    check(culled == reference && sample(culled,{-4, 0, 4}) != glm::ivec3(0),
          "Portal culling preserves transparent-view surfaces and their physical lighting");
    check(sample(culled,{0, 0, 4}) != glm::ivec3(0) && sample(culled,{5, 0, 4}) != glm::ivec3(0),
          "Both doorway volumes retain their visible exterior floor fragments");
    vr::PortalResult fallback;
    const auto retained = render(fallback, false);
    check(sample(retained,{3, 0, 4}) != glm::ivec3(0),
          "Portal fallback retains exterior floor surfaces");
    check(glGetError() == GL_NO_ERROR, "Portal receiver rendering has valid GL state");
}

// All positional lights must be blocked by physical geometry, including transparent walls.
void wallLighting(const std::filesystem::path& path){
    auto program = std::make_shared<vr::Program>(path / "shaders/phong-shading.vs",
                                                 path / "shaders/phong-shading.fs");
    vr::ShadowRenderer shadows(path, 256, 256);
    vr::RenderVisitor renderer(program);
    vr::Scene scene;
    shadows.attach(scene);
    auto root = scene.getRoot();
    auto state = std::make_shared<vr::State>();
    state->setUniform("uFog", false);
    auto material = std::make_shared<vr::Material>();
    material->setAmbient({0, 0, 0});
    material->setDiffuse({0.8f, 0.8f, 0.8f});
    material->setSpecular({0, 0, 0});
    state->setMaterial(material);
    root->setState(state);
    std::array<std::shared_ptr<vr::Light>, 4> lights;
    for (unsigned i = 0; i < lights.size(); ++i){
        lights[i] = std::make_shared<vr::Light>();
        lights[i]->setPosition({static_cast<float>(i) * 0.6f - 0.9f, 4, 0, 1});
        lights[i]->setAmbient({0, 0, 0});
        lights[i]->setAttenuation({1, 0, 0});
        root->addChild(lights[i]);
    }
    place(root, vr::GeometryFactory::createGround(),{0, 0, 0},{0.5f, 1, 0.5f});
    auto roof = place(root, vr::GeometryFactory::createBox(),{0, 2, 0},{8, 0.2f, 8});
    auto wallState = std::make_shared<vr::State>();
    roof->setState(wallState);
    const auto camera = scene.getCamera();
    camera->setPerspective(55, static_cast<float>(width) / height, 0.1f, 50);
    camera->setLookAt({0, 1.5f, 6},{0, 0, 0});
    const auto render = [&](const vr::PortalResult* portals = nullptr){
        glViewport(0, 0, width, height);
        glEnable(GL_DEPTH_TEST);
        glDepthMask(GL_TRUE);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glDisable(GL_BLEND);
        glDisable(GL_SCISSOR_TEST);
        glClearColor(0, 0, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        shadows.render(scene, camera->getPosition(), true, false);
        renderer.render(*root, *camera, nullptr, portals, deferred.get());
        const auto image = pixels();
        const auto offset = ((height / 2) * width + width / 2) * 3;
        return glm::ivec3(image[offset], image[offset + 1], image[offset + 2]);
    };
    for (unsigned selected = 0; selected < lights.size(); ++selected){
        for (unsigned i = 0; i < lights.size(); ++i){
            lights[i]->setDiffuse(glm::vec3(i == selected ? 1.5f : 0));
        }
        roof->setVisible(false);
        const auto lit = render();
        roof->setVisible(true);
        const auto blocked = render();
        check(lit.x > 70 && blocked == glm::ivec3(0) && shadows.getStats().passes == 24,
              "Every enabled point light has its own wall-occluded shadow map");
        wallState->setBlend(true);
        wallState->setUniform("uOpacity", 0.12f);
        check(render() == blocked,
              "Transparent appearance leaves the physical light barrier solid");
        wallState->setBlend(false);
    }
    roof->setVisible(false);
    for (unsigned i = 1; i < lights.size(); ++i){
        lights[i]->setEnabled(false);
    }
    auto beam = lights[0];
    beam->setPosition({0, 3, 4, 1});
    beam->setDiffuse({1.5f, 1.5f, 1.5f});
    const auto direction = glm::normalize(glm::vec3(0, -3, -4));
    beam->setSpot(direction, 14, 20);
    beam->setRange(35);
    check(render().x > 70, "Spotlight illuminates a receiver inside its beam");
    beam->setSpot({1, 0, 0}, 14, 20);
    check(render() == glm::ivec3(0), "Spotlight cone excludes receivers outside the beam");
    beam->setSpot(direction, 14, 20);
    beam->setRange(2);
    check(render() == glm::ivec3(0), "Flashlight range limits its illumination");
    beam->setRange(35);
    auto wall = place(root, vr::GeometryFactory::createBox(),{0, 1.5f, 2},{6, 3, 0.2f});
    wall->setState(wallState);
    wallState->setBlend(true);
    wall->setRoomId("hidden");
    vr::PortalResult portals;
    portals.active = true;
    check(render(&portals) == glm::ivec3(0),
          "A wall hidden by observer culling still blocks the flashlight beam");
    check(glGetError() == GL_NO_ERROR,
          "Multiple-light and flashlight rendering has valid GL state");
}

// Compares batched and individual depth/color output after scene and mesh changes.
void instancing(const std::filesystem::path& path){
    auto program = std::make_shared<vr::Program>(path / "shaders/phong-shading.vs",
                                                 path / "shaders/phong-shading.fs");
    vr::ShadowRenderer shadows(path, 512, 256);
    vr::RenderVisitor renderer(program);
    renderer.setCulling(false);
    vr::Scene scene;
    const auto root = scene.getRoot();
    auto state = std::make_shared<vr::State>();
    state->setUniform("uFog", false);
    root->setState(state);
    auto sun = std::make_shared<vr::Light>();
    sun->setPosition({-1, 2, 1, 0});
    root->addChild(sun);
    auto point = std::make_shared<vr::Light>();
    point->setPosition({4, 6, 3, 1});
    root->addChild(point);
    auto spot = std::make_shared<vr::Light>();
    spot->setPosition({0, 7, 4, 1});
    spot->setSpot({0, -1, -0.5f}, 20, 35);
    spot->setRange(30);
    root->addChild(spot);
    const auto mesh = vr::GeometryFactory::createBox();
    std::vector<std::shared_ptr<vr::Transform>> boxes;
    for (int z = -2; z <= 2; ++z){
        for (int x = -2; x <= 2; ++x){
            boxes.push_back(place(root, mesh,{x * 2.0f, 0.6f, z * 2.0f},{0.9f, 1.2f, 0.8f}));
        }
    }
    auto transparent = std::make_shared<vr::State>();
    transparent->setBlend(true);
    transparent->setUniform("uOpacity", 0.12f);
    boxes.back()->setState(transparent);
    place(root, vr::GeometryFactory::createGround(),{0, 0, 0},{1, 1, 1});
    const auto panel = vr::GeometryFactory::createAlphaTestPanel();
    std::vector<std::shared_ptr<vr::State>> cutouts;
    std::vector<unsigned char> rgba(16, 255), reversed(16, 255);
    for (unsigned i = 0; i < 4; ++i){
        rgba[i * 4 + 3] = static_cast<unsigned char>(i * 85);
        reversed[i * 4 + 3] = static_cast<unsigned char>((3 - i) * 85);
    }
    const auto texture = std::make_shared<vr::Texture>(2, 2, rgba);
    const auto otherTexture = std::make_shared<vr::Texture>(2, 2, reversed);
    for (unsigned i = 0; i < 6; ++i){
        const auto node = place(root, panel,{-6.0f + i * 2.0f, 0, -6},{1, 1, 1});
        auto local = std::make_shared<vr::State>();
        local->setTexture(0, i < 4 ? texture : otherTexture);
        local->setUniform("uAlphaCutoff", i < 2 ? 0.25f : 0.75f);
        local->setCullFace(false);
        node->setState(local);
        cutouts.push_back(local);
    }
    const auto camera = scene.getCamera();
    camera->setPerspective(55, static_cast<float>(width) / height, 0.1f, 90);
    camera->setLookAt({11, 10, 16},{0, 0, 0});
    const auto depth = [&]{
        auto result = shadows.getSunTarget()->readDepth(0);
        for (unsigned light = 0; light < 2; ++light){
            for (unsigned face = 0; face < 6; ++face){
                const auto data = shadows.getPointTarget(light)->readDepth(face);
                result.insert(result.end(), data.begin(), data.end());
            }
        }
        return result;
    };
    const auto render = [&](bool enabled){
        shadows.setInstancing(enabled);
        glViewport(0, 0, width, height);
        glDepthMask(GL_TRUE);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glDisable(GL_SCISSOR_TEST);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        shadows.render(scene, camera->getPosition(), true, true);
        renderer.render(*root, *camera, nullptr, nullptr, deferred.get());
        check(glGetError() == GL_NO_ERROR,
              "Instanced shadows and ordinary mesh draws share valid VAOs");
        return pixels();
    };
    for (unsigned stage = 0; stage < 3; ++stage){
        if (stage == 1){
            boxes.front()->setMatrix(
                glm::scale(glm::translate(glm::mat4(1),{-7, 2, 2}),{-0.7f, 2.0f, 1.3f}));
            boxes[1]->setVisible(false);
            cutouts[0]->setUniform("uAlphaCutoff", 0.6f);
            spot->setSpot({0.4f, -1, -0.5f}, 20, 35);
        } else if (stage == 2){
            std::vector<glm::vec3> positions, normals;
            std::vector<glm::vec2> uv;
            for (const auto& vertex : mesh->getVertices()){
                positions.push_back(vertex.position * glm::vec3(0.7f, 1.4f, 1));
                normals.push_back(vertex.normal);
                uv.push_back(vertex.texCoord);
            }
            mesh->setData(positions, mesh->getIndices(), normals, uv);
        }
        const auto referenceImage = render(false);
        const auto referenceDepth = depth();
        const auto individual = shadows.getStats();
        const auto batchedImage = render(true);
        const auto batched = shadows.getStats();
        check(referenceDepth == depth(),
              "Instancing preserves every directional, point and spot depth texel");
        check(referenceImage == batchedImage,
              "Instancing preserves the exact shaded scene including alpha cutouts");
        check(batched.draws * 2 < individual.draws && batched.instances == individual.instances &&
                  batched.triangles == individual.triangles && batched.passes == 13,
              "Batching reduces draw calls while retaining all culled instances and triangles");
        check(batched.boundsTested + batched.unculled == batched.instances + batched.rejected &&
                  batched.boundsTested == individual.boundsTested &&
                  batched.rejected == individual.rejected,
              "Light frustum and spotlight culling remain per instance");
    }
    shadows.render(scene, camera->getPosition(), false, true);
    check(shadows.getStats().draws == 0 && shadows.getStats().instances == 0 &&
              shadows.getCasters().empty(),
          "Disabling shadows clears all cached instance and submission state");
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
        renderer.render(*root, *camera, nullptr, portals, deferred.get());
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
    // Ambient illumination is independent of direct shadow maps, including mesh contacts.
    const auto sunDiffuse = sun->getDiffuse(), pointDiffuse = point->getDiffuse();
    const auto sunAmbient = sun->getAmbient(), pointAmbient = point->getAmbient();
    sun->setDiffuse({0, 0, 0});
    point->setDiffuse({0, 0, 0});
    sun->setAmbient({0.2f, 0.2f, 0.2f});
    point->setAmbient({0.2f, 0.2f, 0.2f});
    state->setLightEnabled(0, true);
    state->setLightEnabled(1, true);
    const auto ambientOnly = render(false, false);
    for (bool filtered :{false, true}){
        check(changed(ambientOnly, render(true, false, nullptr, filtered)) == 0,
              "Shadow maps do not put mesh facets or contact seams into ambient lighting");
    }
    sun->setDiffuse(sunDiffuse);
    point->setDiffuse(pointDiffuse);
    sun->setAmbient(sunAmbient);
    point->setAmbient(pointAmbient);
    // The adjacent ground must not create self-shadows at a lit box's contact edge.
    const auto originalView = camera->getView();
    camera->setLookAt({3, 2, 6},{0, 0.5f, 0});
    const auto contactVP = camera->getProjection() * camera->getView();
    for (unsigned selected = 0; selected < 2; ++selected){
        state->setLightEnabled(0, selected == 0);
        state->setLightEnabled(1, selected == 1);
        const auto contactReference = render(false, false);
        for (bool filtered :{false, true}){
            const auto contactShadowed = render(true, false, nullptr, filtered);
            int maximumError = 0;
            for (float y :{0.005f, 0.01f, 0.025f, 0.05f, 0.1f, 0.2f, 0.4f}){
                for (unsigned column = 0; column < 60; ++column){
                    const auto clip = contactVP * glm::vec4(-0.8f + column * 1.6f / 59, y, 1, 1);
                    const auto ndc = glm::vec3(clip) / clip.w;
                    const auto x = static_cast<int>((ndc.x * 0.5f + 0.5f) * width);
                    // Round above the contact edge so the sample stays on the vertical face.
                    const auto row = static_cast<int>(std::ceil((ndc.y * 0.5f + 0.5f) * height));
                    const auto index = (row * width + x) * 3;
                    const int error =
                        static_cast<int>(contactReference[index]) - contactShadowed[index];
                    maximumError = std::max(maximumError, error);
                }
            }
            std::cout << "Box contact shadow error: " << maximumError << '\n';
            check(maximumError <= 2, "Lit box faces stay free of ground-contact shadow seams");
        }
    }
    camera->setView(originalView);
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
        std::get<float>(cubeRoot->getState()->getPassUniforms().at("uPointShadowFar[0]"));
    for (unsigned face = 0; face < 6; ++face){
        const auto data = shadows.getPointTarget()->readDepth(face);
        const float distance = data[128 * 256 + 128] * farPlane;
        check(std::abs(distance - 2.5f) < 0.03f,
              "All six cubemap faces store radial distance to their own occluder");
    }
    portalReceivers(path);
    wallLighting(path);
    instancing(path);
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
// Direct lights may outnumber shadow cubemaps without overlapping IBL samplers.
void lightBudget(const std::filesystem::path& path){
    auto program = std::make_shared<vr::Program>(path / "shaders/phong-shading.vs",
                                                 path / "shaders/phong-shading.fs");
    vr::Scene scene;
    scene.getRoot()->setState(std::make_shared<vr::State>());
    scene.getRoot()->addChild(vr::GeometryFactory::createBox());
    for (unsigned i = 0; i < vr::State::MaxLights; ++i){
        auto light = std::make_shared<vr::Light>();
        light->setPosition(i == 0 ? glm::vec4(0, 1, 0, 0) : glm::vec4(2, 3, 2, 1));
        light->setDiffuse({0.1f, 0.1f, 0.1f});
        light->setRange(10);
        scene.getRoot()->addChild(light);
    }
    const auto camera = scene.getCamera();
    camera->setPerspective(50, static_cast<float>(width) / height, 0.1f, 20);
    camera->setLookAt({0, 1, 5},{0, 0, 0});
    vr::ShadowRenderer shadows(path, 64, 64);
    shadows.attach(scene);
    shadows.render(scene, camera->getPosition(), true, false);
    check(shadows.getPointTarget(vr::State::MaxPointShadows - 1) != nullptr,
          "Point shadows fill their independent sampler budget");
    bool capped = false;
    try{ shadows.getPointTarget(vr::State::MaxPointShadows); }
    catch (const std::out_of_range&){ capped = true; }
    check(capped, "Additional direct lights do not allocate excess shadow samplers");
    vr::RenderVisitor renderer(program);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, width, height);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    renderer.render(*scene.getRoot(), *camera, nullptr, nullptr, deferred.get());
    check(renderer.getStats().lights == vr::State::MaxLights && glGetError() == GL_NO_ERROR,
          "Every direct light remains available when the shadow sampler budget is full");
}
} // namespace

int main(int argc, char** argv){
    if (argc != 2 && argc != 3){
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
        if (argc == 3){
            deferred = std::make_unique<vr::DeferredRenderer>(argv[1]);
            deferred->resize(width, height);
            deferred->setPreviews(false);
        }
        callerState(argv[1]);
        lightBudget(argv[1]);
        run(argv[1]);
    } catch (const std::exception& e){
        std::cerr << "FAILED: " << e.what() << '\n';
        result = 1;
    }
    deferred.reset();
    glfwDestroyWindow(window);
    glfwTerminate();
    return result;
}
