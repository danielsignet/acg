#pragma once
#include "ColorTarget.h"
#include "RenderVisitor.h"
#include "Scene.h"

namespace vr{
class PlanarReflection final{
public:
    // Reuses the ordinary forward renderer for an independently culled reflected camera.
    explicit PlanarReflection(const std::shared_ptr<Program>& program) : m_renderer(program){
    }

    // Replaces reflection targets after viewport or sample-count changes.
    void resize(int width, int height, AntialiasingMode mode);

    // Disables auxiliary mirror rendering together with the reflection comparison control.
    void setEnabled(bool enabled){
        m_enabled = enabled;
    }

    // Updates a visible front-facing mirror; its own surface is excluded from the capture.
    void render(Scene& scene, const Camera& camera, EnvironmentMap* environment,
                ShadingMode shading, bool culling, bool portals);
    // Returns the reflected scene texture, or no texture when the mirror is not visible.
    std::shared_ptr<Texture> getTexture() const;

    // Returns geometry submitted only for the latest reflected camera.
    const RenderStats& getStats() const{
        return m_renderer.getStats();
    }

    // Reports whether the most recent frame submitted a reflection capture.
    bool getRendered() const{
        return m_rendered;
    }

    // Reflects world coordinates through a normalized plane, including its translation.
    static glm::mat4 reflectionMatrix(const glm::vec4& plane);

private:
    RenderVisitor m_renderer;
    std::unique_ptr<ColorTarget> m_target, m_resolved;
    int m_width = 0, m_height = 0, m_samples = 1;
    bool m_rendered = false;
    bool m_enabled = true;
};
} // namespace vr
