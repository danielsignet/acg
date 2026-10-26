#include <vr/AmbientOcclusion.h>
#include <vr/FramebufferState.h>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace vr{
// Creates the deterministic hemisphere kernel and screen-space programs.
AmbientOcclusion::AmbientOcclusion(const std::filesystem::path& root){
    const auto shaders = root / "shaders";
    m_sampleProgram = std::make_shared<Program>(shaders / "screen-quad.vs", shaders / "ssao.fs");
    m_resolveProgram =
        std::make_shared<Program>(shaders / "screen-quad.vs", shaders / "ssao-resolve.fs");
    m_sampleProgram->use();
    constexpr unsigned samples = 32;
    for (unsigned i = 0; i < samples; ++i){
        const float t = (static_cast<float>(i) + 0.5f) / samples;
        const float angle = static_cast<float>(i) * 2.39996323f;
        const float z = 0.15f + 0.85f * t;
        const float radial = std::sqrt(1 - z * z);
        const float scale = 0.12f + 0.88f * t * t;
        m_sampleProgram->set("uSamples[" + std::to_string(i) + "]",
                             glm::vec3(std::cos(angle) * radial, std::sin(angle) * radial, z) *
                                 scale);
    }
}

// Replaces the half-resolution AO and full-resolution resolve targets atomically.
void AmbientOcclusion::resize(int width, int height){
    if (width <= 0 || height <= 0 ||
        (m_resolved && getTexture()->getWidth() == width && getTexture()->getHeight() == height)){
        return;
    }
    auto raw =
        std::make_unique<ColorTarget>(std::max(1, (width + 1) / 2), std::max(1, (height + 1) / 2));
    auto resolved = std::make_unique<ColorTarget>(width, height);
    m_raw = std::move(raw);
    m_resolved = std::move(resolved);
}

// Computes AO and filters it using surface depth and normals at discontinuities.
void AmbientOcclusion::render(const Camera& camera, const GBuffer& gbuffer){
    if (!m_resolved){
        throw std::logic_error("Ambient occlusion viewport has not been initialized");
    }
    FramebufferState saved;
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glDisable(GL_SCISSOR_TEST);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    gbuffer.get(GBuffer::Position)->apply(0);
    gbuffer.get(GBuffer::Normal)->apply(1);
    m_sampleProgram->use();
    m_sampleProgram->set("uPosition", 0);
    m_sampleProgram->set("uNormal", 1);
    m_sampleProgram->set("uView", camera.getView());
    m_sampleProgram->set("uProjection", camera.getProjection());
    m_raw->bind();
    m_quad.draw();
    m_resolveProgram->use();
    m_resolveProgram->set("uPosition", 0);
    m_resolveProgram->set("uNormal", 1);
    m_resolveProgram->set("uRawAO", 2);
    m_resolveProgram->set("uAmbient", 3);
    m_resolveProgram->set("uView", camera.getView());
    m_raw->getTexture()->apply(2);
    gbuffer.get(GBuffer::Ambient)->apply(3);
    m_resolved->bind();
    m_quad.draw();
}

// Returns unchanged ambient RGB with filtered ambient visibility in alpha.
const std::shared_ptr<Texture>& AmbientOcclusion::getTexture() const{
    if (!m_resolved){
        throw std::logic_error("Ambient occlusion viewport has not been initialized");
    }
    return m_resolved->getTexture();
}
} // namespace vr
