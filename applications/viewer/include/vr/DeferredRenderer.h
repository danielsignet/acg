#pragma once
#include "FrameProfiler.h"
#include "AmbientOcclusion.h"
#include "DepthOfField.h"
#include "Antialiasing.h"
#include "ColorTarget.h"
#include "GBuffer.h"
#include "LightCollector.h"
#include "ScreenQuad.h"
#include "RenderMode.h"
#include <functional>
#include <optional>

namespace vr{
class EnvironmentMap;

class DeferredRenderer final{
public:
    static constexpr unsigned PreviewCount = GBuffer::Count + 2;

    // Loads the geometry, lighting, post-processing and buffer-preview programs.
    explicit DeferredRenderer(const std::filesystem::path& root);
    // Replaces viewport-sized attachments only after all replacements are complete.
    void resize(int width, int height);
    // Selects Off, display-space FXAA or four-sample deferred/forward rendering.
    void setAntialiasing(AntialiasingMode mode);

    // Returns the selected scene antialiasing technique.
    AntialiasingMode getAntialiasing() const{
        return m_antialiasingMode;
    }

    // Applies the selected technique around an existing forward draw callback.
    void renderForward(const std::function<void()>& draw);

    // Shares precomputed sky lighting without transferring ownership.
    void setEnvironment(EnvironmentMap* environment){
        m_environment = environment;
    }

    // Renders opaque surfaces, screen-space lighting, transparency and post effects.
    void render(const Camera& camera, const State& state,
                const std::vector<PositionedLight>& lights,
                const std::function<void(const std::shared_ptr<Program>&)>& opaque,
                const std::function<void()>& transparent);

    // Attaches the optional frame profiler without transferring ownership.
    void setProfiler(FrameProfiler* profiler){
        m_profiler = profiler;
    }

    // Selects the final lit image or one independently displayed G-buffer channel.
    void setView(BufferView view){
        m_view = view;
    }

    // Returns the currently displayed channel.
    BufferView getView() const{
        return m_view;
    }

    // Enables screen-space ambient occlusion of ambient lighting.
    void setAmbientOcclusion(bool enabled){
        m_ambientOcclusion = enabled;
    }

    // Reports whether ambient occlusion is enabled.
    bool getAmbientOcclusion() const{
        return m_ambientOcclusion;
    }

    // Enables highlight extraction and the independently blurred bloom chain.
    void setBloom(bool enabled){
        m_bloom = enabled;
    }

    // Reports whether bloom is enabled.
    bool getBloom() const{
        return m_bloom;
    }

    // Enables depth of field while a targeted surface supplies a focus distance.
    void setDepthOfField(bool enabled){
        m_depthOfField = enabled;
    }

    // Reports whether depth of field is enabled.
    bool getDepthOfField() const{
        return m_depthOfField;
    }

    // Sets the positive camera-space hit depth; no target leaves the image sharp.
    void setFocusDistance(std::optional<float> distance);

    // Returns the surface depth used by the next post-processing pass.
    const std::optional<float>& getFocusDistance() const{
        return m_focusDistance;
    }

    // Shows or hides the surface and AO diagnostic thumbnails.
    void setPreviews(bool enabled){
        m_previews = enabled;
    }

    // Reports whether diagnostic thumbnails are enabled.
    bool getPreviews() const{
        return m_previews;
    }

    // Returns screen-quad submissions from the latest frame.
    unsigned getScreenDraws() const{
        return m_screenDraws;
    }

    // Returns a thumbnail rectangle in top-left framebuffer coordinates.
    glm::ivec4 previewRectangle(unsigned index) const;
    // Returns the latest geometry buffers for numerical diagnostics.
    const GBuffer& getGBuffer() const;
    // Returns ambient RGB with AO in alpha, reusing the existing lighting sampler.
    const std::shared_ptr<Texture>& getAmbientBuffer() const;

private:
    FrameProfiler* m_profiler = nullptr;
    EnvironmentMap* m_environment = nullptr;
    // Draws and counts one screen-aligned quad.
    void drawQuad();
    unsigned m_screenDraws = 0;

    // Configures a screen pass without stale geometry rasterization state.
    static void screenPass();
    // Resolves one coherent nearest surface for AO, post effects and diagnostic views.
    void resolveGeometry();
    // Binds surface attachments and evaluates lights once per visible pixel.
    void light(const Camera& camera, const State& state,
               const std::vector<PositionedLight>& lights);
    // Extracts and filters intermediate images before composing the final color target.
    void postProcess(const Camera& camera);
    // Runs a separable Gaussian filter without sampling from its output attachment.
    void blur(const std::shared_ptr<Texture>& source,
              std::array<std::unique_ptr<ColorTarget>, 2>& targets, unsigned passes);
    // Presents the selected output and optionally the surface and AO thumbnails.
    void present(const Camera& camera, GLuint framebuffer, const GLint* viewport);
    std::shared_ptr<Program> m_geometryProgram, m_lightingProgram, m_postProgram, m_viewProgram;
    std::shared_ptr<Program> m_multisampleLighting, m_resolveProgram, m_ambientProgram;
    std::unique_ptr<Antialiasing> m_antialiasing;
    AntialiasingMode m_antialiasingMode = AntialiasingMode::Off;
    ScreenQuad m_quad;
    std::unique_ptr<AmbientOcclusion> m_occlusion;
    std::unique_ptr<DepthOfField> m_dof;
    bool m_ambientOcclusion = false;
    std::unique_ptr<GBuffer> m_gbuffer;
    std::unique_ptr<GBuffer> m_multisampleBuffer;
    std::unique_ptr<ColorTarget> m_multisampleColor;
    std::unique_ptr<ColorTarget> m_color, m_composite;
    std::array<std::unique_ptr<ColorTarget>, 2> m_bloomBlur;
    int m_width = 0, m_height = 0;
    BufferView m_view = BufferView::Lit;
    bool m_bloom = false, m_depthOfField = false, m_previews = true;
    std::optional<float> m_focusDistance;
};
} // namespace vr
