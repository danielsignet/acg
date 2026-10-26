#pragma once
#include "FrameProfiler.h"
#include "ColorTarget.h"
#include "GBuffer.h"
#include "LightCollector.h"
#include "ScreenQuad.h"
#include "RenderMode.h"
#include <functional>

namespace vr{
class DeferredRenderer final{
public:
    static constexpr unsigned PreviewCount = GBuffer::Count + 1;

    // Loads the geometry, lighting, post-processing and buffer-preview programs.
    explicit DeferredRenderer(const std::filesystem::path& root);
    // Replaces viewport-sized attachments only after all replacements are complete.
    void resize(int width, int height);
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

    // Enables highlight extraction and the independently blurred bloom chain.
    void setBloom(bool enabled){
        m_bloom = enabled;
    }

    // Reports whether bloom is enabled.
    bool getBloom() const{
        return m_bloom;
    }

    // Enables depth-based focus on the surface at the center of the camera.
    void setDepthOfField(bool enabled){
        m_depthOfField = enabled;
    }

    // Reports whether depth of field is enabled.
    bool getDepthOfField() const{
        return m_depthOfField;
    }

    // Shows or hides the eight screen-aligned diagnostic thumbnails.
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

private:
    FrameProfiler* m_profiler = nullptr;
    // Draws and counts one screen-aligned quad.
    void drawQuad();
    unsigned m_screenDraws = 0;

    // Configures a screen pass without stale geometry rasterization state.
    static void screenPass();
    // Binds surface attachments and evaluates lights once per visible pixel.
    void light(const Camera& camera, const State& state,
               const std::vector<PositionedLight>& lights);
    // Extracts and filters intermediate images before composing the final color target.
    void postProcess(const Camera& camera);
    // Runs a separable Gaussian filter without sampling from its output attachment.
    void blur(const std::shared_ptr<Texture>& source,
              std::array<std::unique_ptr<ColorTarget>, 2>& targets, unsigned passes);
    // Presents the selected output and optionally the eight G-buffer thumbnails.
    void present(const Camera& camera, GLuint framebuffer, const GLint* viewport);
    std::shared_ptr<Program> m_geometryProgram, m_lightingProgram, m_postProgram, m_viewProgram;
    ScreenQuad m_quad;
    std::unique_ptr<GBuffer> m_gbuffer;
    std::unique_ptr<ColorTarget> m_color, m_composite;
    std::array<std::unique_ptr<ColorTarget>, 2> m_blur, m_bloomBlur;
    int m_width = 0, m_height = 0;
    BufferView m_view = BufferView::Lit;
    bool m_bloom = false, m_depthOfField = false, m_previews = true;
};
} // namespace vr
