#include <vr/DeferredRenderer.h>
#include <vr/SceneGraph.h>
#include <vr/ShadowRenderer.h>
#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace{
unsigned checks = 0;
void check(bool value, const char* message){
    ++checks;
    if (!value) throw std::runtime_error(message);
}

// Every sampled point reached by a light must retain that light in its screen tile.
void coverage(vr::Camera& camera, std::vector<vr::PositionedLight>& lights){
    vr::LightGrid grid;
    grid.build(camera, lights);
    const auto inverse = glm::inverse(camera.getProjection() * camera.getView());
    unsigned hits = 0;
    for (unsigned y = 0; y < vr::LightGrid::Rows; ++y){
        for (unsigned x = 0; x < vr::LightGrid::Columns; ++x){
            const auto mask = grid.getMask(y * vr::LightGrid::Columns + x);
            // Include tile corners/boundaries, both clip planes and interior depths.
            for (const float v : {0.0f, 0.5f, 1.0f}){
                for (const float u : {0.0f, 0.5f, 1.0f}){
                    for (const float z : {-1.0f, -0.9f, 0.0f, 0.9f, 0.99f, 1.0f}){
                        auto point = inverse * glm::vec4(
                            2 * (x + u) / vr::LightGrid::Columns - 1,
                            2 * (y + v) / vr::LightGrid::Rows - 1, z, 1);
                        point /= point.w;
                        for (unsigned i = 0; i < lights.size(); ++i){
                            const auto& light = lights[i];
                            if (!light.active || !light.node->getEnabled()) continue;
                            const auto position = light.model * light.node->getPosition();
                            const auto radius = light.node->getRange();
                            if (position.w == 0 || radius == 0 ||
                                glm::distance(glm::vec3(position), glm::vec3(point)) < radius){
                                check((mask & (1u << i)) != 0,
                                      "Culling preserves every potentially illuminated sample");
                                ++hits;
                            }
                        }
                    }
                }
            }
        }
    }
    check(hits > 1000, "Soundness samples include many actual lighting candidates");
    check(grid.getCandidatePairs() < grid.getPossiblePairs(),
          "Spatial light lists eliminate work instead of merely masking object lights");
}

void run(const std::filesystem::path& path){
    vr::Scene scene;
    const auto root = scene.getRoot();
    auto state = std::make_shared<vr::State>();
    auto material = std::make_shared<vr::Material>();
    material->setDiffuse({0.5f, 0.6f, 0.7f});
    material->setAmbient({0.06f, 0.04f, 0.02f});
    material->setSpecular({0.4f, 0.3f, 0.2f});
    state->setMaterial(material);
    state->setUniform("uFog", false);
    root->setState(state);
    std::vector<std::shared_ptr<vr::Light>> nodes;
    const auto add = [&](glm::vec4 position, float range){
        auto light = std::make_shared<vr::Light>();
        light->setPosition(position);
        light->setRange(range);
        light->setAmbient({0.03f, 0.02f, 0.01f});
        light->setDiffuse({0.3f, 0.4f, 0.5f});
        light->setSpecular({0.1f, 0.2f, 0.3f});
        root->addChild(light);
        nodes.push_back(light);
        return light;
    };
    add({-2, 0, 1.5f, 1}, 2.5f);
    add({2, 0.5f, 1.5f, 1}, 2.5f);
    add({6, 0, 2, 1}, 4); // Center outside the viewport, volume reaches visible surfaces.
    add({0.2f, 0.5f, 1, 0}, 1); // A directional light's position/range cannot cull it.
    add({0, 3, 2, 1}, 0); // Infinite-range point light must remain in every tile.
    add({0, 0, 8, 1}, 1); // Entirely behind the camera.
    add({0, 0, 5.7f, 1}, 7); // Camera inside the volume.
    add({3.5f, 0, 5.8f, 1}, 3.5f); // Crosses the near plane.
    add({0, 0, 1, 1}, 1)->setEnabled(false);
    auto spot = add({0, 1, 2, 1}, 3);
    spot->setSpot({0, -0.3f, -1}, 20, 35);
    auto lights = vr::collectLights(*root, {0, 0, 6});
    // Range remains a world-space radius under a scaled/rotated light parent.
    lights[9].model = glm::scale(glm::rotate(glm::mat4(1), 0.3f, {0, 1, 0}), {2, 1, 3});
    vr::Camera camera;
    camera.setLookAt({0, 0, 6}, {0, 0, 0});
    camera.setPerspective(60, 1.4f, 0.1f, 50);
    coverage(camera, lights);
    camera.setLookAt({2, 1, 6}, {-1, 0, 0});
    coverage(camera, lights);
    camera.setOrthographic(-5, 5, -3, 3, 0.1f, 50);
    coverage(camera, lights);
    vr::LightGrid grid;
    camera.setLookAt({0, 0, 6}, {0, 0, 0});
    camera.setPerspective(60, 1.4f, 0.1f, 50);
    grid.build(camera, lights);
    for (unsigned tile = 0; tile < vr::LightGrid::Count; ++tile){
        check((grid.getMask(tile) & ((1u << 5) | (1u << 8))) == 0,
              "Behind-camera and disabled lights occupy no tile list");
    }
    check(grid.getCandidatePairs() < grid.getPossiblePairs() * 0.85f,
          "The fixture rejects a meaningful fraction of tile/light pairs");
    std::cout << "Tile-light pairs: " << grid.getCandidatePairs() << " / "
              << grid.getPossiblePairs() << '\n';
    grid.build(camera, lights, false);
    check(grid.getCandidatePairs() == grid.getPossiblePairs(), "Reference path retains all active lights");
    grid.build(camera, {});
    check(grid.getCandidatePairs() == 0 && grid.getMask(0) == 0, "Empty scenes clear stale light masks");

    auto program = std::make_shared<vr::Program>(path / "shaders/phong-shading.vs",
                                                 path / "shaders/phong-shading.fs");
    vr::RenderVisitor renderer(program);
    renderer.setCulling(false);
    vr::DeferredRenderer deferred(path);
    deferred.setPreviews(false);
    auto object = std::make_shared<vr::Transform>();
    object->setMatrix(glm::scale(glm::mat4(1), {16, 8, 0.2f}));
    object->addChild(vr::GeometryFactory::createBox());
    root->addChild(object);
    auto glass = std::make_shared<vr::Transform>();
    glass->setMatrix(glm::translate(glm::mat4(1), {0.5f, 0, 1}));
    auto transparent = std::make_shared<vr::State>();
    transparent->setBlend(true);
    transparent->setUniform("uOpacity", 0.35f);
    glass->setState(transparent);
    glass->addChild(vr::GeometryFactory::createBox());
    root->addChild(glass);
    vr::ShadowRenderer shadows(path, 128, 128);
    bool shadowed = false;
    const auto render = [&](int width, int height, bool culling){
        deferred.resize(width, height);
        deferred.setLightCulling(culling);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, width, height);
        glDepthMask(GL_TRUE);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        if (shadowed) shadows.render(scene, scene.getCamera()->getPosition(), true, true);
        renderer.render(*root, *scene.getCamera(), nullptr, nullptr, &deferred);
        std::vector<unsigned char> image(width * height * 4);
        glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, image.data());
        check(glGetError() == GL_NO_ERROR, "Tiled lighting leaves valid OpenGL state");
        return image;
    };
    unsigned comparisons = 0;
    for (const auto mode : {vr::AntialiasingMode::Off, vr::AntialiasingMode::Msaa,
                            vr::AntialiasingMode::Fxaa}){
        deferred.setAntialiasing(mode);
        for (const auto size : {glm::ivec2(223, 157), glm::ivec2(197, 113)}){
            for (int view = 0; view < 3; ++view){
                auto& eye = *scene.getCamera();
                eye.setLookAt(view == 1 ? glm::vec3(2, 1, 6) : glm::vec3(0, 0, 6), {0, 0, 0});
                if (view == 2) eye.setOrthographic(-5, 5, -3, 3, 0.1f, 50);
                else eye.setPerspective(60, static_cast<float>(size.x) / size.y, 0.1f, 50);
                state->setLightEnabled(1, view != 1); // Object masks retain original indices.
                renderer.setShadingMode(view == 1 ? vr::ShadingMode::Toon : vr::ShadingMode::Phong);
                const auto reference = render(size.x, size.y, false);
                const auto culled = render(size.x, size.y, true);
                check(reference == culled,
                      "Culled/reference images match exactly, including MSAA, resize, masks and transparency");
                check(deferred.getLightGrid().getCandidatePairs() <
                          deferred.getLightGrid().getPossiblePairs(), "GPU path consumes spatially culled lists");
                ++comparisons;
            }
        }
    }
    // Moving and re-enabling lights must update the next frame, with no stale masks.
    nodes[0]->setPosition({4, 1, 1, 1});
    nodes[5]->setPosition({-1, 0, 1, 1});
    nodes[8]->setEnabled(true);
    deferred.setBloom(true);
    deferred.setAmbientOcclusion(true);
    const auto reference = render(197, 113, false);
    check(reference == render(197, 113, true), "Moving lights, SSAO and bloom match the full-list image");
    nodes[0]->setEnabled(false);
    check(reference != render(197, 113, true), "The fixture contains visible finite-range illumination");
    nodes[0]->setEnabled(true);
    // An occluder exercises nonuniform light/shadow slots across neighboring tiles.
    glass->getState()->setBlend(false);
    shadowed = true;
    for (const auto mode : {vr::AntialiasingMode::Off, vr::AntialiasingMode::Msaa}){
        deferred.setAntialiasing(mode);
        const auto shadowReference = render(197, 113, false);
        check(shadowReference == render(197, 113, true),
              "Spatial light lists preserve filtered point and directional shadows");
        ++comparisons;
    }
    std::cout << comparisons + 1 << " exact image comparisons; " << checks << " checks passed\n";
}
} // namespace

int main(int argc, char** argv){
    if (argc != 2 || !glfwInit()) return 1;
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    auto* window = glfwCreateWindow(256, 192, "Light culling tests", nullptr, nullptr);
    if (!window){ glfwTerminate(); return 1; }
    glfwMakeContextCurrent(window);
    int result = 0;
    if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress))) result = 1;
    else try{ run(argv[1]); }
    catch (const std::exception& error){ std::cerr << "FAILED: " << error.what() << '\n'; result = 1; }
    glfwDestroyWindow(window);
    glfwTerminate();
    return result;
}
