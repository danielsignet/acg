#pragma once
#include "Camera.h"
#include "ColorTarget.h"
#include "ScreenQuad.h"
#include "Program.h"
#include <array>

namespace vr{
class DepthOfField final{
public:
    static constexpr unsigned ScreenDraws = 3;

    // Loads the half-resolution prefilter and near/far lens-blur program.
    explicit DepthOfField(const std::filesystem::path& root);
    // Replaces the intermediate targets together, including odd viewport sizes.
    void resize(int width, int height);
    // Filters separate near/far layers using a positive camera-space focus distance.
    void render(const Camera& camera, const std::shared_ptr<Texture>& color,
                const std::shared_ptr<Texture>& depth, float focusDistance);

    // Returns the background blur with its signed circle of confusion in alpha.
    const std::shared_ptr<Texture>& getFar() const{
        return m_targets[1]->getTexture();
    }

    // Returns premultiplied foreground blur and its coverage in alpha.
    const std::shared_ptr<Texture>& getNear() const{
        return m_targets[2]->getTexture();
    }

private:
    std::shared_ptr<Program> m_program;
    ScreenQuad m_quad;
    std::array<std::unique_ptr<ColorTarget>, 3> m_targets;
};
} // namespace vr
