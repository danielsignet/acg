#include <GLFW/glfw3.h>
#include <algorithm>
#include <array>
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>
#include <stdexcept>
#include <vr/DeferredRenderer.h>
#include <vr/FramebufferImage.h>
#include <vr/GLState.h>
#include <vr/Geometry.h>
#include <vr/GpuSkinning.h>
#include <vr/ModelInstance.h>
#include <vr/Reader.h>
#include <vr/SkeletalAnimation.h>
#include <vr/StreamBuffer.h>
#include <vr/UpdateVisitor.h>

namespace{
constexpr int width = 400, height = 300;

void check(bool condition, const char* message){
    if (!condition){
        throw std::runtime_error(message);
    }
}

// Queue GPU reads through repeated ring wraps before reading the captured results.
void streaming(){
    constexpr unsigned rounds = 32, words = 128;
    GLuint captured = 0;
    glGenBuffers(1, &captured);
    glBindBuffer(GL_COPY_WRITE_BUFFER, captured);
    glBufferData(GL_COPY_WRITE_BUFFER, rounds * words * sizeof(unsigned), nullptr, GL_STREAM_READ);
    vr::StreamBuffer stream;
    std::array<unsigned, words> input{};
    for (unsigned round = 0; round < rounds; ++round){
        for (unsigned i = 0; i < words; ++i){
            input[i] = round * words + i;
        }
        const auto source = stream.acquire(sizeof(input), input.data());
        glBindBuffer(GL_COPY_READ_BUFFER, source);
        glCopyBufferSubData(GL_COPY_READ_BUFFER, GL_COPY_WRITE_BUFFER, 0, round * sizeof(input),
                            sizeof(input));
        stream.seal();
    }
    std::array<unsigned, rounds * words> output{};
    glGetBufferSubData(GL_COPY_WRITE_BUFFER, 0, sizeof(output), output.data());
    glDeleteBuffers(1, &captured);
    for (unsigned i = 0; i < output.size(); ++i){
        check(output[i] == i,
              "Ring wrap preserves every pending GPU read without overwriting in-flight data");
    }
}

void compare(vr::Geometry& cpu, vr::Geometry& gpu){
    const auto vertices = gpu.readGpuVertices();
    const auto& expected = cpu.getVertices();
    check(vertices.size() == expected.size(), "GPU skinning preserves the vertex count");
    for (std::size_t i = 0; i < vertices.size(); ++i){
        check(glm::distance(vertices[i].position, expected[i].position) < 0.002f,
              "GPU positions match full-influence CPU skinning");
        check(glm::distance(vertices[i].normal, expected[i].normal) < 0.00003f &&
                  glm::distance(vertices[i].tangent, expected[i].tangent) < 0.00003f &&
                  vertices[i].texCoord == expected[i].texCoord,
              "GPU skinning preserves normals, tangents and UVs");
    }
    check(glm::distance(cpu.getBound().getMinimum(), gpu.getBound().getMinimum()) < 0.002f &&
              glm::distance(cpu.getBound().getMaximum(), gpu.getBound().getMaximum()) < 0.002f,
          "GPU rendering retains exact deformation-aware CPU bounds");
    const auto& indices = cpu.getIndices();
    for (std::size_t i = 0; i < indices.size(); i += 3){
        const auto a = expected[indices[i]].position, b = expected[indices[i + 1]].position,
                   c = expected[indices[i + 2]].position;
        const auto cross = glm::cross(b - a, c - a);
        if (glm::length(cross) < 0.01f){
            continue;
        }
        const auto normal = glm::normalize(cross);
        const auto origin = (a + b + c) / 3.0f + normal;
        const auto reference = cpu.intersectRay(origin, -normal, 2);
        const auto hit = gpu.intersectRay(origin, -normal, 2);
        check(bool(reference) == bool(hit) && (!hit || std::abs(*reference - *hit) < 0.002f),
              "Picking uses the current GPU-rendered pose without GPU readback");
        break;
    }
}

void influences(const std::filesystem::path& root){
    auto pose = std::make_shared<vr::SkinningPose>();
    for (unsigned i = 0; i < 12; ++i){
        const auto model =
            glm::rotate(glm::translate(glm::mat4(1),{float(i) * 0.01f, 0.02f * float(i), 0}),
                        float(i) * 0.02f, glm::vec3(0, 1, 0));
        pose->matrices.push_back(model);
        pose->normals.push_back(glm::transpose(glm::inverse(glm::mat3(model))));
    }
    pose->revision = 1;
    auto gpu = std::make_shared<vr::Geometry>();
    std::vector<vr::SkinWeights> weights(3);
    for (auto& vertex : weights){
        for (unsigned i = 0; i < 12; ++i){
            vertex.joints[i] = i;
            vertex.weights[i] = 1.0f / 12;
        }
    }
    gpu->setData({{-1, -1, 0},{1, -1, 0},{0, 1, 0}},{0, 1, 2},{{0, 0, 1},{0, 0, 1},{0, 0, 1}},
                 {{0, 0},{1, 0},{0.5f, 1}},{{1, 0, 0, 1},{1, 0, 0, 1},{1, 0, 0, 1}}, weights);
    gpu->setSkin(pose);
    auto cpu = gpu->instantiateSkin(pose);
    for (auto& matrix : pose->matrices){
        matrix[3].x += 0.3f;
    }
    ++pose->revision;
    auto skinning = std::make_shared<vr::GpuSkinning>(root);
    vr::UpdateVisitor updater;
    skinning->beginFrame();
    updater.update(*gpu,{0, 0, false, nullptr, skinning});
    check(updater.getSkinnedVertices() == 0, "GPU mode skips full CPU normal and tangent skinning");
    skinning->prepare();
    compare(*cpu, *gpu);
    check(skinning->getStats().draws == 1 && skinning->getStats().vertices == 3,
          "All twelve bone influences are evaluated in one GPU deformation pass");
    skinning->endFrame();
    // Reload can release the current material shader before the next deformation pass.
    auto transient = std::make_shared<vr::Program>(root / "shaders/phong-shading.vs",
                                                   root / "shaders/phong-shading.fs");
    transient->use();
    transient.reset();
    ++pose->revision;
    skinning->beginFrame();
    updater.update(*gpu,{0, 0, false, nullptr, skinning});
    skinning->prepare();
    compare(*cpu, *gpu);
    skinning->endFrame();
    check(glGetError() == GL_NO_ERROR,
          "Deformation after shader replacement never restores a deleted program");
}

void dragon(const std::filesystem::path& root){
    vr::Reader reader(root);
    const auto asset = reader.readModel("models/dragon/Dragon.fbx");
    auto cpu = vr::ModelInstance::create("CPU", asset),
         gpu = vr::ModelInstance::create("GPU", asset);
    auto skinning = std::make_shared<vr::GpuSkinning>(root);
    vr::UpdateVisitor updater;
    vr::CullVisitor cpuList, gpuList;
    auto program = std::make_shared<vr::Program>(root / "shaders/phong-shading.vs",
                                                 root / "shaders/phong-shading.fs");
    vr::RenderVisitor renderer(program);
    vr::DeferredRenderer deferred(root);
    deferred.resize(width, height);
    deferred.setPreviews(false);
    deferred.setAmbientOcclusion(false);
    vr::Scene cpuScene, gpuScene;
    for (auto scene :{&cpuScene, &gpuScene}){
        auto state = std::make_shared<vr::State>();
        state->setUniform("uFog", false);
        state->setLighting(false);
        state->setCullFace(false);
        scene->getRoot()->setState(state);
        auto placement = std::make_shared<vr::Transform>();
        placement->setMatrix(glm::scale(glm::mat4(1), glm::vec3(0.005f)));
        placement->addChild(scene == &cpuScene ? cpu : gpu);
        scene->getRoot()->addChild(placement);
        scene->getCamera()->setPerspective(48, float(width) / height, 0.1f, 40);
        scene->getCamera()->setLookAt({5, 3, 7},{0, 1.5f, 0});
    }
    const auto render = [&](vr::Scene& scene, bool useDeferred){
        vr::GLState::bindFramebuffer(GL_FRAMEBUFFER, 0);
        vr::GLState::viewport(0, 0, width, height);
        vr::GLState::enable(GL_DEPTH_TEST);
        vr::GLState::depthFunc(GL_LESS);
        vr::GLState::depthMask(GL_TRUE);
        glClearColor(0.1f, 0.12f, 0.15f, 1);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        renderer.render(*scene.getRoot(), *scene.getCamera(), nullptr, nullptr,
                        useDeferred ? &deferred : nullptr);
        return vr::FramebufferImage::capture(0, 0, width, height);
    };
    for (bool subdivided :{false, true}){
        cpu->setSubdivided(subdivided);
        gpu->setSubdivided(subdivided);
        for (std::size_t clip = 0; clip < cpu->getAnimation()->getClips().size(); ++clip){
            cpu->getAnimation()->setClip(clip);
            gpu->getAnimation()->setClip(clip);
            const auto duration = cpu->getAnimation()->getClips()[clip].duration;
            updater.update(*cpu,{0, duration * 0.37, true});
            skinning->beginFrame();
            updater.update(*gpu,{0, duration * 0.37, true, nullptr, skinning});
            check(updater.getSkinnedVertices() == 0,
                  "GPU playback does not perform full CPU skinning");
            skinning->prepare();
            cpuList.collect(*cpu, glm::mat4(1),{0, 0, 0}, false);
            gpuList.collect(*gpu, glm::mat4(1),{0, 0, 0}, false);
            check(cpuList.getRenderList().size() == gpuList.getRenderList().size(),
                  "GPU mode preserves every animated submesh");
            for (std::size_t i = 0; i < cpuList.getRenderList().size(); ++i){
                compare(*cpuList.getRenderList()[i].geometry, *gpuList.getRenderList()[i].geometry);
            }
            const auto draws = skinning->getStats().draws;
            for (bool useDeferred :{false, true}){
                const auto reference = render(cpuScene, useDeferred);
                const auto result = render(gpuScene, useDeferred);
                check(reference.countChangedPixels(result, 3) < 12,
                      "GPU skinning retains forward/deferred rendered pixels");
            }
            skinning->prepare();
            check(skinning->getStats().draws == draws && draws == gpuList.getRenderList().size(),
                  "Every view reuses a single GPU deformation per submesh and pose");
            skinning->endFrame();
            skinning->beginFrame();
            updater.update(*gpu,{0, 1, false, nullptr, skinning});
            skinning->prepare();
            check(skinning->getStats().draws == 0 && skinning->getStats().cpuPositions == 0,
                  "Paused GPU animation reuses both vertex output and exact CPU positions");
            skinning->endFrame();
        }
    }
    check(glGetError() == GL_NO_ERROR, "GPU deformation leaves valid OpenGL state");
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
    auto* window = glfwCreateWindow(width, height, "GPU skinning regressions", nullptr, nullptr);
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
        vr::GLState::Scope tracked;
        streaming();
        influences(argv[1]);
        dragon(argv[1]);
        std::cout << "GPU/CPU deformation, twelve influences, picking, all clips, subdivision and "
                     "view reuse passed\n";
    } catch (const std::exception& error){
        std::cerr << error.what() << '\n';
        result = 1;
    }
    glfwDestroyWindow(window);
    glfwTerminate();
    return result;
}
