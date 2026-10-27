#pragma once
#include "CullVisitor.h"
#include "RenderToTexture.h"
#include "Scene.h"
#include "LightCollector.h"

namespace vr{
struct ShadowStats{
    unsigned passes = 0;
    unsigned boundsTested = 0, rejected = 0;
    unsigned draws = 0, instances = 0;
    std::size_t triangles = 0;
    double cpuMs = 0;
};

class ShadowRenderer{
public:
    // Creates directional and point-light shadow resources.
    explicit ShadowRenderer(const std::filesystem::path& root, int sunResolution = 2048,
                            int pointResolution = 1024);

    // Attaches allocated depth targets and their independent cameras to a loaded scene.
    void attach(Scene& scene) const;

    // Updates light depth images independently of observer frustum and portal culling.
    void render(Scene& scene, const glm::vec3& lodEye, bool enabled, bool filtered);

    // Returns light-pass counts and CPU submission time from the latest frame.
    const ShadowStats& getStats() const{
        return m_stats;
    }

    // Selects batching or individual draws for the same light-culled instances.
    void setInstancing(bool enabled){
        m_instancing = enabled;
    }

    // Returns whether compatible shadow casters are submitted together.
    bool getInstancing() const{
        return m_instancing;
    }

    // Returns the directional light's two-dimensional depth attachment.
    const std::shared_ptr<RenderToTexture>& getSunTarget() const{
        return m_sun;
    }

    // Returns one positional light's six-face depth attachment.
    const std::shared_ptr<RenderToTexture>& getPointTarget(unsigned slot = 0) const{
        return m_points.at(slot);
    }

    // Returns the independently collected geometry instances that can cast shadows.
    const std::vector<RenderNode>& getCasters() const{
        return m_casters;
    }

private:
    struct Batch{
        Geometry* geometry;
        std::shared_ptr<Texture> albedo;
        float alphaCutoff;
        std::vector<std::size_t> casters;
    };

    // Resolves depth-only batch state and world bounds once for this frame's casters.
    void prepareCasters();

    // Renders only depth and alpha cutouts from one light camera.
    void drawDepthPass(const Camera& camera, bool radial, const glm::vec3& point, float farPlane,
                       const Frustum* cone = nullptr);

    // Creates a cubemap with six independent cameras for one positional light.
    std::shared_ptr<RenderToTexture> createPointTarget(unsigned slot) const;

    // Fits and renders the directional camera, then publishes its projection to State.
    void renderSun(State& state, const BoundingBox& bounds, const glm::vec3& direction,
                   const glm::vec3& point, int index);

    // Renders all six radial depth faces and publishes their shared far distance.
    void renderPoint(State& state, const BoundingBox& bounds, const PositionedLight& light,
                     unsigned index, unsigned slot);

    std::shared_ptr<RenderToTexture> m_sun;
    std::vector<std::shared_ptr<RenderToTexture>> m_points;
    std::shared_ptr<Camera> m_sunCamera;
    int m_pointResolution;
    std::shared_ptr<Program> m_depthProgram;
    std::vector<RenderNode> m_casters;
    std::vector<BoundingBox> m_worldBounds;
    std::vector<Batch> m_batches;
    std::vector<glm::mat4> m_visibleModels;
    bool m_instancing = true;
    ShadowStats m_stats;
};
} // namespace vr
