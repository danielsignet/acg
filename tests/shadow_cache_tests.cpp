#include <GLFW/glfw3.h>
#include <algorithm>
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>
#include <stdexcept>
#include <vr/SceneGraph.h>
#include <vr/GpuSkinning.h>
#include <vr/SkeletalAnimation.h>
#include <vr/ShadowRenderer.h>

namespace{
void check(bool condition, const char* message){
    if (!condition){
        throw std::runtime_error(message);
    }
}

std::shared_ptr<vr::Transform> place(const std::shared_ptr<vr::Group>& root,
                                     const std::shared_ptr<vr::Node>& mesh, glm::vec3 position,
                                     glm::vec3 scale){
    auto transform = std::make_shared<vr::Transform>();
    transform->setMatrix(glm::scale(glm::translate(glm::mat4(1), position), scale));
    transform->addChild(mesh);
    root->addChild(transform);
    return transform;
}

void compare(const std::vector<float>& a, const std::vector<float>& b){
    check(a.size() == b.size(), "Cached depth retains target dimensions");
    for (std::size_t i = 0; i < a.size(); ++i){
        check(std::abs(a[i] - b[i]) < 0.00002f,
              "Cached shadows match complete redraws without stale or missing depth");
    }
}

void run(const std::filesystem::path& root){
    vr::Scene scene;
    auto graph = scene.getRoot();
    graph->setState(std::make_shared<vr::State>());
    auto sun = std::make_shared<vr::Light>();
    sun->setPosition({-1, 3, 2, 0});
    graph->addChild(sun);
    auto point = std::make_shared<vr::Light>();
    point->setPosition({0, 4, 4, 1});
    graph->addChild(point);
    auto floor = vr::GeometryFactory::createBox();
    place(graph, floor,{0, -0.5f, 0},{16, 1, 16});
    auto moving = place(graph, vr::GeometryFactory::createBox(),{-2, 1, 0},{1, 2, 1});
    moving->setUpdateCallback(
        std::make_shared<vr::FunctionCallback>([](vr::Node&, const vr::UpdateContext&){}));
    auto cutout = vr::GeometryFactory::createAlphaTestPanel();
    auto alpha = std::make_shared<vr::State>();
    alpha->setTexture(
        0, std::make_shared<vr::Texture>(1, 1, std::vector<unsigned char>{255, 255, 255, 0}));
    alpha->setUniform("uAlphaCutoff", 0.5f);
    cutout->setState(alpha);
    place(graph, cutout,{1, 1, -1},{1, 1, 1});
    vr::ShadowRenderer cached(root, 128, 64), reference(root, 128, 64);
    reference.setCaching(false);
    const auto verify = [&]{
        cached.render(scene,{0, 3, 10}, true, true);
        const auto sunDepth = cached.getSunTarget()->readDepth(0);
        std::array<std::vector<float>, 6> cube;
        for (unsigned face = 0; face < 6; ++face){
            cube[face] = cached.getPointTarget()->readDepth(face);
        }
        reference.render(scene,{0, 3, 10}, true, true);
        compare(sunDepth, reference.getSunTarget()->readDepth(0));
        for (unsigned face = 0; face < 6; ++face){
            compare(cube[face], reference.getPointTarget()->readDepth(face));
        }
        check(cached.getStats().passes == 7, "Caching preserves all seven logical light views");
    };
    verify();
    verify();
    verify();
    check(cached.getStats().cacheHits == 7 && cached.getStats().draws < reference.getStats().draws,
          "Stable light views reuse static casters while still rendering dynamic depth");
    check(cached.getStats().draws == 0 && cached.getStats().reusedFaces == 7,
          "A callback alone must not rerender unchanged dynamic caster poses");
    moving->setMatrix(glm::scale(glm::translate(glm::mat4(1),{2, 1, 0}),{1, 2, 1}));
    verify();
    moving->setVisible(false);
    verify();
    verify();
    check(
        cached.getStats().draws == 0 && cached.getStats().reusedFaces == 7,
        "After a moving caster disappears, restored static depth is reused without ghost shadows");
    alpha->setTexture(
        0, std::make_shared<vr::Texture>(1, 1, std::vector<unsigned char>{255, 255, 255, 255}));
    verify();
    verify();
    verify();
    alpha->setUniform("uAlphaCutoff", 1.1f);
    verify();
    verify();
    cutout->setVisible(false);
    verify();
    verify();
    graph->getState()->setUniform("uLodRange", glm::vec2(0.2f, 0.8f));
    verify();
    verify();
    graph->getState()->setUniform("uLodRange", glm::vec2(0, 1));
    point->setPosition({-3, 4, 4, 1});
    verify();
    verify();
    point->setSpot({0, -1, -1}, 15, 30);
    verify();
    verify();
    point->setSpot({1, -1, -1}, 15, 30);
    verify();
    verify();
    point->setRange(8);
    verify();
    verify();
    floor->setData({{-0.5f, -0.5f, -0.5f},{0.5f, -0.5f, -0.5f},{0, 0.5f, 0.5f}},{0, 1, 2},
                   {{0, 1, 0},{0, 1, 0},{0, 1, 0}});
    verify();
    verify();
    sun->setPosition({1, 3, 2, 0});
    verify();
    verify();
    cached.getSunTarget()->render(0, [](const vr::Camera&){
        glClearDepth(0);
        glClear(GL_DEPTH_BUFFER_BIT);
    });
    verify();
    moving->setVisible(true);
    verify();
    cached.setCaching(false);
    verify();
    cached.setCaching(true);
    verify();
    verify();
    verify();
    auto pose = std::make_shared<vr::SkinningPose>();
    pose->matrices = {glm::mat4(1)};
    pose->normals = {glm::mat3(1)};
    pose->revision = 1;
    auto skinned = std::make_shared<vr::Geometry>();
    std::vector<vr::SkinWeights> weights(3);
    for (auto& weight : weights) weight.weights[0] = 1;
    skinned->setData({{-1,0,-1},{1,0,-1},{0,1,1}},{0,1,2},{{0,1,0},{0,1,0},{0,1,0}},
                     {{0,0},{1,0},{.5f,1}},{{1,0,0,1},{1,0,0,1},{1,0,0,1}},weights);
    skinned->setSkin(pose);
    place(graph,skinned,{0,1,0},{2,2,2});
    auto gpu = std::make_shared<vr::GpuSkinning>(root);
    const auto deform = [&]{
        gpu->beginFrame();
        skinned->prepareDeformation(gpu);
        gpu->prepare();
    };
    deform();verify();verify();gpu->endFrame();
    check(cached.getStats().reusedFaces > 0,"An unchanged GPU pose must reuse completed depth faces");
    const auto dataRevision=skinned->getDataRevision();
    pose->matrices[0]=glm::translate(glm::mat4(1),glm::vec3(2.5f,0,0));
    ++pose->revision;
    deform();
    check(skinned->getDataRevision()==dataRevision,"GPU pose changes retain the source mesh revision");
    verify();gpu->endFrame();
    check(glGetError() == GL_NO_ERROR, "Shadow depth caching preserves valid OpenGL state");
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
    auto* window = glfwCreateWindow(128, 128, "Shadow cache tests", nullptr, nullptr);
    if (!window){
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(0);
    int result = 0;
    try{
        if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress))){
            throw std::runtime_error("OpenGL initialization failed");
        }
        run(argv[1]);
        std::cout << "Static/dynamic depth cache, six cube faces and invalidation passed\n";
    } catch (const std::exception& error){
        std::cerr << error.what() << '\n';
        result = 1;
    }
    glfwDestroyWindow(window);
    glfwTerminate();
    return result;
}
