#pragma once
#include "Camera.h"
#include "ColorTarget.h"
#include "GBuffer.h"
#include "ScreenQuad.h"

namespace vr{
// Estimates opaque-surface ambient visibility and resolves it into ambient alpha.
class AmbientOcclusion final{
public:
    // Creates the deterministic hemisphere kernel and screen-space programs.
    explicit AmbientOcclusion(const std::filesystem::path& root);
    // Replaces the half-resolution AO and full-resolution resolve targets atomically.
    void resize(int width, int height);
    // Computes AO and filters it using surface depth and normals at discontinuities.
    void render(const Camera& camera, const GBuffer& gbuffer);
    // Returns unchanged ambient RGB with filtered ambient visibility in alpha.
    const std::shared_ptr<Texture>& getTexture() const;

private:
    std::shared_ptr<Program> m_sampleProgram, m_resolveProgram;
    std::unique_ptr<ColorTarget> m_raw, m_resolved;
    ScreenQuad m_quad;
};
} // namespace vr
