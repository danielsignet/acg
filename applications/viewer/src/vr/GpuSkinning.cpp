#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <vr/FrameProfiler.h>
#include <vr/GLState.h>
#include <vr/Geometry.h>
#include <vr/GpuSkinning.h>
#include <vr/SkeletalAnimation.h>

namespace vr{
static_assert(SkinWeights::Count == 12, "GPU skinning must preserve every source influence");

namespace{
StreamBuffer::Policy skinStreamPolicy(){
    // Retain the former path for reproducible A/B measurements and driver diagnostics.
    static const bool reuse = []{
        const auto* value = std::getenv("ACG_SKIN_STREAM_REUSE");
        return value && std::strcmp(value, "1") == 0;
    }();
    return reuse ? StreamBuffer::Policy::Reuse : StreamBuffer::Policy::Discard;
}
}

GpuSkinData::GpuSkinData() : output(skinStreamPolicy()), paletteStream(skinStreamPolicy()){
}

GpuSkinData::~GpuSkinData(){
    const GLuint textures[]{joints, weights, palette};
    glDeleteTextures(3, textures);
    const GLuint buffers[]{inputBuffer, jointBuffer, weightBuffer};
    glDeleteBuffers(3, buffers);
    if (inputArray){
        GLState::deleteVertexArrays(1, &inputArray);
    }
}

GpuSkinning::GpuSkinning(const std::filesystem::path& root, FrameProfiler* profiler)
    : m_profiler(profiler){
    m_program = std::make_shared<Program>(
        root / "shaders/skinning-feedback.vs", std::filesystem::path{}, std::filesystem::path{},
        std::vector<std::string>{"skinnedPosition", "skinnedNormal", "skinnedUv",
                                 "skinnedTangent"});
    glGetIntegerv(GL_MAX_TEXTURE_BUFFER_SIZE, &m_maxTexels);
    glGenTransformFeedbacks(1, &m_feedback);
}

GpuSkinning::~GpuSkinning(){
    if (m_feedback){
        glDeleteTransformFeedbacks(1, &m_feedback);
    }
}

void GpuSkinning::beginFrame(){
    m_pending.clear();
    m_stats ={};
}

bool GpuSkinning::enqueue(Geometry& geometry){
    if (!geometry.m_skin || geometry.m_skin->matrices.empty() ||
        geometry.m_vertices.size() > static_cast<std::size_t>(m_maxTexels) / 3 ||
        geometry.m_skin->matrices.size() > static_cast<std::size_t>(m_maxTexels) / 7){
        return false;
    }
    if (std::find(m_pending.begin(), m_pending.end(), &geometry) == m_pending.end()){
        m_pending.push_back(&geometry);
    }
    return true;
}

void GpuSkinning::prepare(){
    {
        FrameProfiler::Scope pass(m_profiler, FrameProfiler::Stage::GpuSkinning);

        struct Restore{
            GLint program = 0, array = 0, feedback = 0;
            bool discard;

            Restore(){
                GLState::getIntegerv(GL_CURRENT_PROGRAM, &program);
                GLState::getIntegerv(GL_VERTEX_ARRAY_BINDING, &array);
                GLState::getIntegerv(GL_TRANSFORM_FEEDBACK_BINDING, &feedback);
                discard = GLState::isEnabled(GL_RASTERIZER_DISCARD) != GL_FALSE;
            }

            ~Restore(){
                GLState::bindTransformFeedback(static_cast<GLuint>(feedback));
                if (!discard){
                    GLState::disable(GL_RASTERIZER_DISCARD);
                }
                GLState::bindVertexArray(static_cast<GLuint>(array));
                GLState::useProgram(static_cast<GLuint>(program));
            }
        } restore;

        GLState::enable(GL_RASTERIZER_DISCARD);
        GLState::bindTransformFeedback(m_feedback);
        for (auto* geometry : m_pending){
            skin(*geometry);
        }
    }
    for (auto* geometry : m_pending){
        geometry->ensureSkinPositions();
    }
}

void GpuSkinning::skin(Geometry& geometry){
    if (!geometry.m_gpuData){
        auto data = std::make_shared<GpuSkinData>();
        std::vector<GpuVertex> rest;
        std::vector<glm::uvec4> joints;
        std::vector<glm::vec4> weights;
        const auto& vertices = geometry.getRestVertices();
        rest.reserve(vertices.size());
        joints.reserve(vertices.size() * 3);
        weights.reserve(vertices.size() * 3);
        for (const auto& vertex : vertices){
            rest.push_back({vertex.position, vertex.normal, vertex.texCoord, vertex.tangent});
            for (unsigned group = 0; group < 3; ++group){
                glm::uvec4 indices;
                glm::vec4 influences;
                for (unsigned axis = 0; axis < 4; ++axis){
                    indices[axis] = vertex.skin.joints[group * 4 + axis];
                    influences[axis] = vertex.skin.weights[group * 4 + axis];
                    if (influences[axis] > 0 && indices[axis] >= geometry.m_skin->matrices.size()){
                        throw std::out_of_range("GPU skin joint");
                    }
                }
                joints.push_back(indices);
                weights.push_back(influences);
            }
        }
        glGenVertexArrays(1, &data->inputArray);
        glGenBuffers(1, &data->inputBuffer);
        GLState::bindVertexArray(data->inputArray);
        glBindBuffer(GL_ARRAY_BUFFER, data->inputBuffer);
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(rest.size() * sizeof(GpuVertex)),
                     rest.data(), GL_STATIC_DRAW);
        geometry.bindAttributes(data->inputBuffer);
        glGenBuffers(1, &data->jointBuffer);
        glBindBuffer(GL_TEXTURE_BUFFER, data->jointBuffer);
        glBufferData(GL_TEXTURE_BUFFER, static_cast<GLsizeiptr>(joints.size() * sizeof(glm::uvec4)),
                     joints.data(), GL_STATIC_DRAW);
        glGenTextures(1, &data->joints);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_BUFFER, data->joints);
        glTexBuffer(GL_TEXTURE_BUFFER, GL_RGBA32UI, data->jointBuffer);
        glGenBuffers(1, &data->weightBuffer);
        glBindBuffer(GL_TEXTURE_BUFFER, data->weightBuffer);
        glBufferData(GL_TEXTURE_BUFFER, static_cast<GLsizeiptr>(weights.size() * sizeof(glm::vec4)),
                     weights.data(), GL_STATIC_DRAW);
        glGenTextures(1, &data->weights);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_BUFFER, data->weights);
        glTexBuffer(GL_TEXTURE_BUFFER, GL_RGBA32F, data->weightBuffer);
        glGenTextures(1, &data->palette);
        geometry.m_gpuData = std::move(data);
    }
    auto& data = *geometry.m_gpuData;
    if (data.revision == geometry.m_skin->revision){
        return;
    }
    m_palette.clear();
    const auto& skin = *geometry.m_skin;
    for (std::size_t joint = 0; joint < skin.matrices.size(); ++joint){
        for (unsigned column = 0; column < 4; ++column){
            m_palette.push_back(skin.matrices[joint][column]);
        }
        for (unsigned column = 0; column < 3; ++column){
            m_palette.emplace_back(skin.normals[joint][column], 0);
        }
    }
    const auto paletteBuffer =
        data.paletteStream.acquire(m_palette.size() * sizeof(glm::vec4), m_palette.data());
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_BUFFER, data.palette);
    glTexBuffer(GL_TEXTURE_BUFFER, GL_RGBA32F, paletteBuffer);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_BUFFER, data.joints);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_BUFFER, data.weights);
    const auto output = data.output.acquire(geometry.m_vertices.size() * sizeof(GpuVertex));
    m_program->use();
    m_program->set("uJoints", 0);
    m_program->set("uWeights", 1);
    m_program->set("uPalette", 2);
    GLState::bindVertexArray(data.inputArray);
    glBindBufferBase(GL_TRANSFORM_FEEDBACK_BUFFER, 0, output);
    {
        FrameProfiler::Scope draw(m_profiler, FrameProfiler::Stage::SkinningDraw);
        glBeginTransformFeedback(GL_POINTS);
        glDrawArrays(GL_POINTS, 0, static_cast<GLsizei>(geometry.m_vertices.size()));
        glEndTransformFeedback();
    }
    data.paletteStream.seal();
    data.revision = skin.revision;
    ++m_stats.draws;
    m_stats.vertices += geometry.m_vertices.size();
    m_stats.paletteBytes += m_palette.size() * sizeof(glm::vec4);
}

void GpuSkinning::endFrame(){
    for (auto* geometry : m_pending){
        if (geometry->m_gpuData){
            geometry->m_gpuData->output.seal();
        }
    }
    m_pending.clear();
}
} // namespace vr
