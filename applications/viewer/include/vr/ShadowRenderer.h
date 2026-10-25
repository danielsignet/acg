#pragma once
#include "CullVisitor.h"
#include "RenderToTexture.h"
#include "Scene.h"

namespace vr{
struct ShadowStats{
    unsigned passes = 0;
    unsigned draws = 0;
    std::size_t triangles = 0;
    double cpuMs = 0;
};

class ShadowRenderer{
public:
    // Creates directional and point-light shadow resources.
    explicit ShadowRenderer(const std::filesystem::path& root, int sunResolution = 2048,
                            int pointResolution = 1024);

    // Attaches both render targets and their seven light cameras to a loaded scene.
    void attach(Scene& scene) const;

    // Updates light depth images independently of observer frustum and portal culling.
    void render(Scene& scene, const glm::vec3& lodEye, bool enabled, bool filtered);

    // Returns light-pass counts and CPU submission time from the latest frame.
    const ShadowStats& getStats() const{
        return m_stats;
    }

    // Returns the directional light's two-dimensional depth attachment.
    const std::shared_ptr<RenderToTexture>& getSunTarget() const{
        return m_sun;
    }

    // Returns the point light's six-face depth attachment.
    const std::shared_ptr<RenderToTexture>& getPointTarget() const{
        return m_point;
    }

    // Returns the independently collected geometry instances that can cast shadows.
    const std::vector<RenderNode>& getCasters() const{
        return m_casters;
    }

private:
    // Renders only depth and alpha cutouts from one light camera.
    void drawDepthPass(const Camera& camera, bool radial, const glm::vec3& point, float farPlane);

    // Fits and renders the directional camera, then publishes its projection to State.
    void renderSun(State& state, const BoundingBox& bounds, const glm::vec3& direction,
                   const glm::vec3& point, int index);

    // Renders all six radial depth faces and publishes their shared far distance.
    void renderPoint(State& state, const BoundingBox& bounds, const glm::vec3& point, int index);

    std::shared_ptr<RenderToTexture> m_sun;
    std::shared_ptr<RenderToTexture> m_point;
    std::shared_ptr<Camera> m_sunCamera;
    std::array<std::shared_ptr<Camera>, 6> m_pointCameras;
    std::shared_ptr<Program> m_depthProgram;
    std::vector<RenderNode> m_casters;
    ShadowStats m_stats;
};
} // namespace vr
