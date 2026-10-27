#pragma once
#include "ColorTarget.h"
#include "FrameProfiler.h"
#include "Program.h"
#include "RenderMode.h"
#include "ScreenQuad.h"
#include <functional>

namespace vr{
class Antialiasing final{
public:
    // Loads the display-space FXAA filter and the depth-preserving presentation pass.
    explicit Antialiasing(const std::filesystem::path& root);
    // Discards viewport-sized resources after a dimension or technique change.
    void configure(int width, int height, AntialiasingMode mode);
    // Filters the tone-mapped scene while leaving diagnostic channels and HUD untouched.
    const std::shared_ptr<Texture>& filter(const std::shared_ptr<Texture>& source);
    // Applies the same techniques to forward rendering, preserving depth for debug geometry.
    void renderForward(const std::function<void()>& draw, FrameProfiler* profiler);

    // Returns additional quad submissions from the last filtering or forward pass.
    unsigned getScreenDraws() const{
        return m_screenDraws;
    }

    // Configures a fullscreen pass independently of previous geometry state.
    static void screenPass();

private:
    std::shared_ptr<Program> m_program;
    ScreenQuad m_quad;
    std::unique_ptr<ColorTarget> m_filtered, m_forward, m_resolved;
    int m_width = 0, m_height = 0;
    AntialiasingMode m_mode = AntialiasingMode::Off;
    unsigned m_screenDraws = 0;
};
} // namespace vr
