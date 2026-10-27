#include <vr/DeferredRenderer.h>
#include <vr/Reader.h>
#include <vr/Subdivision.h>
#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>
#include <array>
#include <cmath>
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace{
constexpr int width = 320, height = 240;

// Reports a failed geometry, material or GPU behavior contract.
void check(bool condition, const char* message){
    if (!condition){
        throw std::runtime_error(message);
    }
}

// Checks mirrored UV seams, degenerate UVs and tangent regeneration after subdivision.
void tangents(){
    auto mesh = std::make_shared<vr::Geometry>();
    mesh->setData({{-1, -1, 0},{0, -1, 0},{0, 1, 0},{1, -1, 0},{1, 1, 0},{-1, 1, 0}},
                  {0, 1, 2, 0, 2, 5, 1, 3, 4, 1, 4, 2}, std::vector<glm::vec3>(6,{0, 0, 1}),
                  {{1, 0},{0, 0},{0, 1},{1, 0},{1, 1},{1, 1}});
    check(mesh->getVertices().size() == 8 && mesh->getTriangleCount() == 4,
          "Mirrored charts split their shared edge without changing any triangles");
    for (std::size_t i = 0; i < mesh->getIndices().size(); ++i){
        const auto& vertex = mesh->getVertices()[mesh->getIndices()[i]];
        const float side = i < 6 ? -1.0f : 1.0f;
        check(glm::distance(glm::vec3(vertex.tangent), glm::vec3(side, 0, 0)) < 1e-5f &&
                  vertex.tangent.w == side,
              "Mirrored UV charts preserve tangent direction and bitangent handedness");
    }
    const auto refined = vr::Subdivision::create(mesh);
    vr::CullVisitor collector;
    collector.collect(*refined, glm::mat4(1),{0, 0, 6}, false);
    const auto geometry = collector.getRenderList().front().geometry;
    check(geometry && geometry->getTriangleCount() == 16,
          "Subdivision keeps its fourfold triangle count with tangent attributes");
    for (const auto& vertex : geometry->getVertices()){
        check(std::abs(glm::dot(vertex.normal, glm::vec3(vertex.tangent))) < 1e-5f &&
                  std::abs(glm::length(glm::vec3(vertex.tangent)) - 1) < 1e-5f,
              "Subdivision regenerates normalized tangents orthogonal to the new normals");
    }
    mesh->setData({{0, 0, 0},{1, 0, 0},{0, 1, 0}},{0, 1, 2},
                  std::vector<glm::vec3>(3,{0, 0, 1}), std::vector<glm::vec2>(3,{0, 0}));
    for (const auto& vertex : mesh->getVertices()){
        check(vertex.tangent == glm::vec4(0),
              "A degenerate UV chart safely disables normal mapping instead of inventing a basis");
    }
    auto texture =
        std::make_shared<vr::Texture>(1, 1, std::vector<unsigned char>{204, 179, 217, 255});
    vr::State parent, child;
    parent.setNormalMap(texture);
    check(parent.merged(child).getNormalMap() == texture,
          "A child inherits a normal map independently of color textures");
    child.setNormalMap(nullptr);
    check(!parent.merged(child).getNormalMap() && parent.getNormalMap() == texture,
          "An explicit null normal map removes inheritance without changing the parent");
}

// Checks glTF normal metadata, supplied/generated tangent agreement and XML overrides.
void importing(const std::filesystem::path& root){
    struct Fixture{
        std::filesystem::path directory =
            std::filesystem::temp_directory_path() /
            ("acg-normal-map-" +
             std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));

        Fixture(){
            std::filesystem::create_directory(directory);
        }

        ~Fixture(){
            for (const auto* file :
                 {"normal.png", "triangle.bin", "provided.gltf", "generated.gltf", "scene.xml"}){
                std::error_code error;
                std::filesystem::remove(directory / file, error);
            }
            std::error_code error;
            std::filesystem::remove(directory, error);
        }
    } fixture;

    const std::array<float, 36> data{0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 1, 0, 0, 1,
                                     0, 0, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1};
    {
        std::ofstream binary(fixture.directory / "triangle.bin", std::ios::binary);
        binary.write(reinterpret_cast<const char*>(data.data()), sizeof(data));
    }
    std::filesystem::copy_file(root / "textures/brick-normal.png",
                               fixture.directory / "normal.png");
    for (const bool supplied :{false, true}){
        std::ofstream gltf(fixture.directory / (supplied ? "provided.gltf" : "generated.gltf"));
        gltf
            << R"({"asset":{"version":"2.0"},"scene":0,"scenes":[{"nodes":[0]}],"nodes":[{"mesh":0}],
"buffers":[{"uri":"triangle.bin","byteLength":144}],
"bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36},{"buffer":0,"byteOffset":36,"byteLength":36},
{"buffer":0,"byteOffset":72,"byteLength":24},{"buffer":0,"byteOffset":96,"byteLength":48}],
"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[0,0,0],"max":[1,1,0]},
{"bufferView":1,"componentType":5126,"count":3,"type":"VEC3"},
{"bufferView":2,"componentType":5126,"count":3,"type":"VEC2"},
{"bufferView":3,"componentType":5126,"count":3,"type":"VEC4"}],
"images":[{"uri":"normal.png"}],"textures":[{"source":0}],
"materials":[{"normalTexture":{"index":0,"scale":0.4}}],
"meshes":[{"primitives":[{"material":0,"attributes":{"POSITION":0,"NORMAL":1,"TEXCOORD_0":2)";
        if (supplied){
            gltf << R"(,"TANGENT":3)";
        }
        gltf << "}}]}]}";
    }
    vr::Reader reader(root);
    std::vector<glm::vec4> reference;
    for (const bool supplied :{false, true}){
        const auto asset =
            reader.readModel(fixture.directory / (supplied ? "provided.gltf" : "generated.gltf"));
        vr::CullVisitor collector;
        collector.collect(*asset, glm::mat4(1),{0, 0, 6}, false);
        const auto& item = collector.getRenderList().front();
        check(item.state.getNormalMap() &&
                  std::abs(std::get<float>(item.state.getUniforms().at("uNormalScale")) - 0.4f) <
                      1e-5f,
              "glTF normal textures and their strength reach the resolved material state");
        for (const auto& vertex : item.geometry->getVertices()){
            if (!supplied){
                reference.push_back(vertex.tangent);
            } else{
                check(glm::distance(vertex.tangent, reference.front()) < 1e-5f,
                      "Imported and generated glTF frames agree after Assimp's V-coordinate "
                      "conversion");
            }
            const auto bitangent =
                glm::cross(vertex.normal, glm::vec3(vertex.tangent)) * vertex.tangent.w;
            const float normalY = std::get<float>(item.state.getUniforms().at("uNormalY"));
            check(glm::distance(bitangent * normalY, glm::vec3(0, 1, 0)) < 1e-5f,
                  "A positive green normal component follows glTF's original surface direction");
        }
    }
    {
        std::ofstream xml(fixture.directory / "scene.xml");
        xml << R"(<scene><group><state normalMap="textures/brick-normal.png" normalScale="0.7"/>
<instance ref="box"/><group><state normalMap="none"/><instance ref="box"/></group></group></scene>)";
    }
    reader.registerNode("box", vr::GeometryFactory::createBox());
    const auto scene = reader.readScene(fixture.directory / "scene.xml");
    vr::CullVisitor collector;
    collector.collect(*scene->getRoot(), glm::mat4(1),{0, 0, 6}, false);
    const auto& items = collector.getRenderList();
    check(items.size() == 2 && items[0].state.getNormalMap() && !items[1].state.getNormalMap() &&
              std::get<float>(items[0].state.getUniforms().at("uNormalScale")) == 0.7f,
          "XML can assign normal textures and strength or explicitly remove an inherited map");
}

// Checks exact transformed normals, lighting parity, geometry invariance and AO independence.
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
    camera->setPerspective(50, static_cast<float>(width) / height, 0.1f, 30);
    camera->setLookAt({0, 0, 6},{0, 0, 0});
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
        check(glGetError() == GL_NO_ERROR, "Normal mapping leaves valid sampler and OpenGL state");
        return pixels;
    };
    const auto center = (height / 2 * width + width / 2) * 4;
    const auto sampleNormal = [&]{
        const auto data = deferred.getGBuffer().read(vr::GBuffer::Normal);
        return glm::vec3(data[center], data[center + 1], data[center + 2]);
    };
    const auto plain = render();
    const auto geometryBefore = deferred.getGBuffer().read(vr::GBuffer::Position);
    const auto albedoBefore = deferred.getGBuffer().read(vr::GBuffer::Albedo);
    const auto planeBefore = deferred.getGBuffer().read(vr::GBuffer::Plane);
    state->setNormalMap(
        std::make_shared<vr::Texture>(1, 1, std::vector<unsigned char>{204, 179, 217, 255}));
    const auto mapped = render();
    const auto expected = glm::normalize(glm::vec3(204, 179, 217) / 255.0f * 2.0f - 1.0f);
    check(glm::distance(sampleNormal(), expected) < 0.002f,
          "RGB normal data is decoded in linear space and written to the normal G-buffer");
    check(geometryBefore == deferred.getGBuffer().read(vr::GBuffer::Position) &&
              albedoBefore == deferred.getGBuffer().read(vr::GBuffer::Albedo) &&
              planeBefore == deferred.getGBuffer().read(vr::GBuffer::Plane),
          "Normal maps change shading without moving geometry, changing albedo or receiver planes");
    check(std::abs(static_cast<int>(plain[center]) - mapped[center]) > 5,
          "A changed surface normal visibly changes illumination");
    const auto forward = render(false);
    for (unsigned channel = 0; channel < 3; ++channel){
        check(std::abs(static_cast<int>(mapped[center + channel]) - forward[center + channel]) <= 2,
              "Forward and deferred agree on normal-mapped lighting");
    }
    renderer.setShadingMode(vr::ShadingMode::UnmappedPhong);
    check(render() == plain && render(false)[center] == plain[center],
          "Plain Phong disables the normal map in both rendering paths");
    renderer.setShadingMode(vr::ShadingMode::Phong);
    state->setUniform("uNormalScale", 0.0f);
    check(render() == plain, "Zero normal scale restores the unperturbed surface exactly");
    state->setUniform("uNormalScale", 1.0f);
    state->setUniform("uNormalY", -1.0f);
    render();
    check(glm::distance(sampleNormal(), glm::vec3(expected.x, -expected.y, expected.z)) < 0.002f,
          "Normal texture conventions can reverse Y without changing tangent-space X or Z");
    state->setUniform("uNormalY", 1.0f);
    const auto rotation = glm::rotate(glm::mat4(1), glm::radians(30.0f),{0, 1, 0});
    transform->setMatrix(glm::scale(rotation,{2, 0.7f, 3}));
    render();
    check(glm::distance(sampleNormal(), glm::mat3(rotation) * expected) < 0.002f,
          "Nonuniform scale and rotation preserve an orthonormal world-space tangent basis");
    transform->setMatrix(glm::scale(glm::mat4(1),{-2, 0.7f, 3}));
    glFrontFace(GL_CW);
    render();
    check(glm::distance(sampleNormal(), glm::vec3(-expected.x, expected.y, expected.z)) < 0.002f,
          "Mirrored model transforms retain the correct bitangent sign");
    glFrontFace(GL_CCW);
    transform->setMatrix(glm::mat4(1));
    deferred.setAmbientOcclusion(true);
    render();
    const auto readAmbient = [&]{
        std::vector<float> data(width * height * 4);
        deferred.getAmbientBuffer()->apply(0);
        glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_FLOAT, data.data());
        return data;
    };
    const auto mappedAO = readAmbient();
    renderer.setShadingMode(vr::ShadingMode::UnmappedPhong);
    render();
    check(mappedAO == readAmbient(),
          "Texture relief cannot introduce false geometric self-occlusion into AO");
    renderer.setShadingMode(vr::ShadingMode::Phong);
    mesh->setData(positions, indices, normals);
    render();
    check(glm::distance(sampleNormal(), glm::vec3(0, 0, 1)) < 0.002f,
          "A mesh without usable UVs keeps its original normal despite an assigned normal map");
}
} // namespace

// Runs CPU and GPU checks with all GL objects destroyed before the hidden context.
int main(int argc, char** argv){
    if (argc != 2 || !glfwInit()){
        return 1;
    }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    auto* window = glfwCreateWindow(width, height, "Normal mapping tests", nullptr, nullptr);
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
            tangents();
            importing(argv[1]);
            rendering(argv[1]);
            std::cout << "Normal mapping geometry, materials and GPU checks passed\n";
        } catch (const std::exception& error){
            std::cerr << "FAILED: " << error.what() << '\n';
            result = 1;
        }
    }
    glfwDestroyWindow(window);
    glfwTerminate();
    return result;
}
