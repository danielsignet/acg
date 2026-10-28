#include <vr/DeferredRenderer.h>
#include <vr/GeometryFactory.h>
#include <vr/Reader.h>
#include <vr/ShadowRenderer.h>
#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace{
constexpr int width = 320, height = 240;

// Reports a material-import or GPU surface-relief regression.
void check(bool condition, const char* message){
    if (!condition){
        throw std::runtime_error(message);
    }
}

// Creates a scalar ramp with a known derivative, independently of the demonstration image.
std::shared_ptr<vr::Texture> ramp(){
    std::vector<unsigned char> pixels(256 * 4 * 4);
    for (unsigned y = 0; y < 4; ++y){
        for (unsigned x = 0; x < 256; ++x){
            const auto index = (y * 256 + x) * 4;
            for (unsigned channel = 0; channel < 3; ++channel){
                pixels[index + channel] = static_cast<unsigned char>(x);
            }
            pixels[index + 3] = 255;
        }
    }
    return std::make_shared<vr::Texture>(256, 4, std::move(pixels), false);
}

// Checks separate inheritance, OBJ height metadata and explicit XML overrides.
void materials(const std::filesystem::path& root){
    vr::State parent, child;
    const auto texture = ramp();
    parent.setHeightMap(texture);
    check(parent.merged(child).getHeightMap() == texture && !parent.getNormalMap(),
          "Scalar height maps inherit independently of RGB normal maps");
    child.setHeightMap(nullptr);
    check(!parent.merged(child).getHeightMap() && parent.getHeightMap() == texture,
          "Explicit null removes an inherited height map without modifying its parent");
    bool rejected = false;
    try{
        child.setHeightMap(vr::Texture::depth(4, 4));
    } catch (const std::invalid_argument&){
        rejected = true;
    }
    check(rejected, "Depth attachments cannot masquerade as surface-height data");

    struct Fixture{
        std::filesystem::path directory =
            std::filesystem::temp_directory_path() /
            ("acg-bump-map-" +
             std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));

        Fixture(){
            std::filesystem::create_directory(directory);
        }

        ~Fixture(){
            for (const auto* file :{"height.png", "surface.obj", "surface.mtl", "scene.xml"}){
                std::error_code error;
                std::filesystem::remove(directory / file, error);
            }
            std::error_code error;
            std::filesystem::remove(directory, error);
        }
    } fixture;

    std::filesystem::copy_file(root / "textures/brick-height.png",
                               fixture.directory / "height.png");
    {
        std::ofstream obj(fixture.directory / "surface.obj");
        obj << "mtllib surface.mtl\nv 0 0 0\nv 1 0 0\nv 0 1 0\n"
               "vt 0 0\nvt 1 0\nvt 0 1\nusemtl relief\nf 1/1 2/2 3/3\n";
        std::ofstream mtl(fixture.directory / "surface.mtl");
        mtl << "newmtl relief\nKd 0.7 0.7 0.7\nmap_Bump height.png\n";
    }
    vr::Reader reader(root);
    const auto model = reader.readModel(fixture.directory / "surface.obj");
    vr::CullVisitor collector;
    collector.collect(*model, glm::mat4(1),{0, 0, 5}, false);
    const auto& imported = collector.getRenderList().front().state;
    check(imported.getHeightMap() && !imported.getNormalMap(),
          "Assimp HEIGHT metadata reaches the height slot instead of the RGB normal slot");
    {
        std::ofstream xml(fixture.directory / "scene.xml");
        xml << R"(<scene><group><state heightMap="textures/brick-height.png" heightScale="-0.02"/>
<instance ref="box"/><group><state heightMap="none"/><instance ref="box"/></group></group></scene>)";
    }
    reader.registerNode("box", vr::GeometryFactory::createBox());
    const auto scene = reader.readScene(fixture.directory / "scene.xml");
    collector.collect(*scene->getRoot(), glm::mat4(1),{0, 0, 5}, false);
    const auto& items = collector.getRenderList();
    check(items.size() == 2 && items[0].state.getHeightMap() && !items[1].state.getHeightMap() &&
              std::abs(std::get<float>(items[0].state.getUniforms().at("uHeightScale")) + 0.02f) <
                  1e-6f,
          "XML preserves height-map inheritance, removal and signed physical height scale");
}

// Checks analytic height gradients, scale, renderer parity and geometric invariants.
void rendering(const std::filesystem::path& root){
    auto program = std::make_shared<vr::Program>(root / "shaders/phong-shading.vs",
                                                 root / "shaders/phong-shading.fs");
    vr::RenderVisitor renderer(program);
    renderer.setCulling(false);
    vr::DeferredRenderer deferred(root);
    deferred.resize(width, height);
    deferred.setPreviews(false);
    vr::Scene scene;
    auto state = std::make_shared<vr::State>();
    state->setUniform("uFog", false);
    scene.getRoot()->setState(state);
    auto transform = std::make_shared<vr::Transform>();
    auto mesh = std::make_shared<vr::Geometry>();
    const std::vector<glm::vec3> positions{{-1, -1, 0},{1, -1, 0},{1, 1, 0},{-1, 1, 0}};
    const std::vector<unsigned> indices{0, 1, 2, 0, 2, 3};
    const std::vector<glm::vec3> normals(4,{0, 0, 1});
    mesh->setData(positions, indices, normals,{{0, 0},{1, 0},{1, 1},{0, 1}});
    transform->addChild(mesh);
    scene.getRoot()->addChild(transform);
    auto light = std::make_shared<vr::Light>();
    light->setPosition({3, 1, 4, 1});
    light->setDiffuse({0.8f, 0.8f, 0.8f});
    light->setAmbient({0.1f, 0.1f, 0.1f});
    scene.getRoot()->addChild(light);
    const auto camera = scene.getCamera();
    camera->setPerspective(50, static_cast<float>(width) / height, 0.1f, 100);
    camera->setLookAt({0, 0, 4},{0, 0, 0});
    const auto render = [&](bool useDeferred = true){
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, width, height);
        glEnable(GL_DEPTH_TEST);
        glDepthMask(GL_TRUE);
        glClearColor(0.035f, 0.052f, 0.078f, 1);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        renderer.render(*scene.getRoot(), *camera, nullptr, nullptr,
                        useDeferred ? &deferred : nullptr);
        std::vector<unsigned char> pixels(width * height * 4);
        glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        check(glGetError() == GL_NO_ERROR,
              "Height mapping preserves the OpenGL sampler budget and state");
        return pixels;
    };
    const auto center = (height / 2 * width + width / 2) * 4;
    const auto sampleNormal = [&]{
        const auto data = deferred.getGBuffer().read(vr::GBuffer::Normal);
        return glm::vec3(data[center], data[center + 1], data[center + 2]);
    };
    const auto flat = render();
    const auto position = deferred.getGBuffer().read(vr::GBuffer::Position);
    const auto albedo = deferred.getGBuffer().read(vr::GBuffer::Albedo);
    const auto plane = deferred.getGBuffer().read(vr::GBuffer::Plane);
    state->setHeightMap(ramp());
    state->setUniform("uHeightScale", 0.4f);
    const auto bumped = render();
    constexpr float slope = 256.0f / 255;
    const auto expected = glm::normalize(glm::vec3(-0.4f * slope / 2, 0, 1));
    check(glm::distance(sampleNormal(), expected) < 0.002f,
          "A linear height ramp yields the analytic world-space shading normal");
    check(position == deferred.getGBuffer().read(vr::GBuffer::Position) &&
              albedo == deferred.getGBuffer().read(vr::GBuffer::Albedo) &&
              plane == deferred.getGBuffer().read(vr::GBuffer::Plane) &&
              mesh->getTriangleCount() == 2,
          "Bump mapping preserves positions, albedo, receiver planes and topology");
    check(std::abs(static_cast<int>(flat[center]) - bumped[center]) > 4,
          "Height-derived normals visibly change illumination");
    for (const auto mode :
         {vr::ShadingMode::Phong, vr::ShadingMode::BlinnPhong, vr::ShadingMode::CookTorrance}){
        renderer.setShadingMode(mode);
        const auto deferredPixels = render();
        const auto forwardPixels = render(false);
        for (unsigned channel = 0; channel < 3; ++channel){
            check(std::abs(static_cast<int>(deferredPixels[center + channel]) -
                           forwardPixels[center + channel]) <= 2,
                  "Forward and deferred agree on height relief for each supported BRDF");
        }
    }
    renderer.setShadingMode(vr::ShadingMode::Phong);
    renderer.setShadingMode(vr::ShadingMode::UnmappedPhong);
    check(render() == flat, "Plain Phong disables height perturbations as well as normal maps");
    renderer.setShadingMode(vr::ShadingMode::Phong);
    state->setUniform("uHeightScale", 0.0f);
    check(render() == flat, "Zero height scale restores the unperturbed surface exactly");
    state->setUniform("uHeightScale", -0.4f);
    render();
    check(glm::distance(sampleNormal(), glm::vec3(-expected.x, 0, expected.z)) < 0.002f,
          "Negative height reverses the surface relief");
    state->setUniform("uHeightScale", 0.4f);
    transform->setMatrix(glm::scale(glm::mat4(1),{2, 0.7f, 3}));
    render();
    check(glm::distance(sampleNormal(), glm::normalize(glm::vec3(-0.4f * slope / 4, 0, 1))) <
              0.002f,
          "World-space height gradients account for nonuniform model scale");
    transform->setMatrix(glm::mat4(1));
    mesh->setData(positions, indices, normals,{{1, 0},{0, 0},{0, 1},{1, 1}});
    render();
    check(glm::distance(sampleNormal(), glm::vec3(-expected.x, 0, expected.z)) < 0.002f,
          "Mirroring UVs reverses the height gradient without inverting the surface normal");
    mesh->setData(positions, indices, normals,{{0, 0},{1, 0},{1, 1},{0, 1}});
    auto normalMap =
        std::make_shared<vr::Texture>(1, 1, std::vector<unsigned char>{204, 179, 217, 255});
    state->setNormalMap(normalMap);
    state->setUniform("uBumpMapping", false);
    render();
    const auto rgbNormal = glm::normalize(glm::vec3(204, 179, 217) / 255.0f * 2.0f - 1.0f);
    check(glm::distance(sampleNormal(), rgbNormal) < 0.002f && state->getHeightMap(),
          "Disabling height relief falls back to the stored RGB normal map "
          "without losing either asset");
    state->setUniform("uBumpMapping", true);
    render();
    check(glm::distance(sampleNormal(), expected) < 0.002f,
          "Enabling height relief selects height data in the same sampler slot");
    deferred.setAntialiasing(vr::AntialiasingMode::Msaa);
    render();
    check(glm::distance(sampleNormal(), expected) < 0.002f,
          "MSAA resolves the same height-derived normal");
    deferred.setAntialiasing(vr::AntialiasingMode::Off);
    deferred.setAmbientOcclusion(true);
    const auto ambient = [&]{
        std::vector<float> data(width * height * 4);
        deferred.getAmbientBuffer()->apply(0);
        glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_FLOAT, data.data());
        return data;
    };
    render();
    const auto bumpedAmbient = ambient();
    state->setUniform("uBumpMapping", false);
    render();
    check(bumpedAmbient == ambient(),
          "Height detail cannot create false geometric self-occlusion in SSAO");
    state->setUniform("uBumpMapping", true);
    vr::ShadowRenderer shadows(root, 64, 32);
    shadows.attach(scene);
    shadows.render(scene, camera->getPosition(), true, true);
    const auto bumpedDepth = shadows.getPointTarget()->readDepth(5);
    check(std::any_of(bumpedDepth.begin(), bumpedDepth.end(),
                      [](float depth){ return depth < 0.9999f; }),
          "The point-light depth test contains the actual quad surface");
    state->setUniform("uBumpMapping", false);
    shadows.render(scene, camera->getPosition(), true, true);
    check(bumpedDepth == shadows.getPointTarget()->readDepth(5),
          "Height shading does not displace silhouettes or shadow-caster geometry");
    state->setUniform("uBumpMapping", true);
    mesh->setData(positions, indices, normals);
    render();
    check(glm::distance(sampleNormal(), glm::vec3(0, 0, 1)) < 0.002f,
          "Missing UV coordinates safely produce no height perturbation");
}
} // namespace

// Runs import and framebuffer checks while all GL objects retain a current context.
int main(int argc, char** argv){
    if (argc != 2 || !glfwInit()){
        return 1;
    }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    auto* window = glfwCreateWindow(width, height, "Bump mapping tests", nullptr, nullptr);
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
            materials(argv[1]);
            rendering(argv[1]);
            std::cout << "Height-map imports, gradients, controls and renderer parity passed\n";
        } catch (const std::exception& error){
            std::cerr << "FAILED: " << error.what() << '\n';
            result = 1;
        }
    }
    glfwDestroyWindow(window);
    glfwTerminate();
    return result;
}
