#pragma once
#include "CullVisitor.h"
#include "LightCollector.h"
#include "RenderToTexture.h"
#include "Scene.h"
#include "ShadowCollector.h"

namespace vr{
struct ShadowStats{
    unsigned passes = 0;
    unsigned boundsTested = 0, rejected = 0;
    unsigned unculled = 0;
    unsigned draws = 0, instances = 0;
    std::size_t triangles = 0;
    double cpuMs = 0;
    unsigned cacheHits = 0, reusedFaces = 0, staticRebuilds = 0;
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
        if (m_instancing != enabled){
            invalidate();
        }
        m_instancing = enabled;
    }

    void setCaching(bool enabled){
        if (m_caching != enabled){
            invalidate();
        }
        m_caching = enabled;
    }

    void invalidate();

    void setStateCaching(bool enabled){
        m_collector.setStateCaching(enabled);
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
        glm::vec2 lodRange;
        std::vector<std::size_t> casters;
        bool dynamic = false;
    };

    struct CasterState{
        Geometry* geometry;
        std::uint64_t geometryRevision, textureRevision;
        glm::mat4 model;
        float cutoff;
        glm::vec2 range;
        bool culling;
        std::uint64_t poseRevision = 0;

        bool operator==(const CasterState& other) const{
            return geometry == other.geometry && geometryRevision == other.geometryRevision &&
                   textureRevision == other.textureRevision && model == other.model &&
                   cutoff == other.cutoff && range == other.range && culling == other.culling &&
                   poseRevision == other.poseRevision;
        }
    };

    struct FaceCache{
        std::unique_ptr<RenderToTexture> depth;
        glm::mat4 projection{1};
        glm::vec3 point{0};
        float farPlane = 0;
        bool radial = false;
        std::optional<std::array<glm::vec4, 6>> cone;
        std::uint64_t staticRevision = 0, activeRevision = 0;
        bool observed = false, ready = false;
        std::vector<CasterState> dynamicCasters;
    };
    enum class Casters{ All, Static, Dynamic };
    void renderFace(RenderToTexture& target, unsigned layer, FaceCache& cache, bool radial,
                    const glm::vec3& point, float farPlane, const Frustum* cone = nullptr);
    std::vector<CasterState> dynamicCasters(const Frustum& frustum, const Frustum* cone,
                                          bool& volatileTexture) const;

    // Resolves depth-only batch state and world bounds once for this frame's casters.
    void prepareCasters();

    // Renders only depth and alpha cutouts from one light camera.
    void drawDepthPass(const Camera& camera, bool radial, const glm::vec3& point, float farPlane,
                       const Frustum* cone = nullptr, Casters selection = Casters::All);

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
    bool m_caching = true, m_hasStatic = false;
    std::uint64_t m_staticRevision = 1;
    std::vector<CasterState> m_staticCasters;
    FaceCache m_sunCache;
    std::vector<std::array<FaceCache, 6>> m_pointCaches;
    ShadowCollector m_collector;
    ShadowStats m_stats;
};
} // namespace vr
