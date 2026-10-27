#include <vr/DepthOfField.h>
#include <vr/FramebufferState.h>
#include <algorithm>
#include <stdexcept>

namespace vr{
// Loads the half-resolution prefilter and near/far lens-blur program.
DepthOfField::DepthOfField(const std::filesystem::path& root)
    : m_program(std::make_shared<Program>(root / "shaders/screen-quad.vs",
                                          root / "shaders/depth-of-field.fs")){
}

// Replaces the intermediate targets together, including odd viewport sizes.
void DepthOfField::resize(int width, int height){
    std::array<std::unique_ptr<ColorTarget>, 3> targets;
    for (auto& target : targets){
        target = std::make_unique<ColorTarget>(std::max(1, (width + 1) / 2),
                                               std::max(1, (height + 1) / 2));
    }
    m_targets = std::move(targets);
}

// Filters separate near/far layers using a positive camera-space focus distance.
void DepthOfField::render(const Camera& camera, const std::shared_ptr<Texture>& color,
                          const std::shared_ptr<Texture>& depth, float focusDistance){
    if (!m_targets[0]){
        throw std::logic_error("Depth of field viewport has not been initialized");
    }
    FramebufferState saved;
    m_program->use();
    m_program->set("uColor", 0);
    m_program->set("uDepth", 1);
    m_program->set("uInverseProjection", glm::inverse(camera.getProjection()));
    m_program->set("uFocusDistance", focusDistance);
    depth->apply(1);
    for (unsigned pass = 0; pass < m_targets.size(); ++pass){
        (pass == 0 ? color : m_targets[0]->getTexture())->apply(0);
        m_targets[pass]->bind();
        m_program->set("uPass", static_cast<int>(pass));
        m_quad.draw();
    }
}
} // namespace vr
