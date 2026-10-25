#pragma once
#include "CullVisitor.h"
#include "LightCollector.h"
#include "PathVisitor.h"
#include "RenderMode.h"
#include <map>

namespace vr{
class DeferredRenderer;

struct RenderStats{
    unsigned draws = 0;
    std::size_t triangles = 0;
    std::map<std::string, std::size_t> lodLevels;
    std::map<std::string, unsigned> roomDraws;
    unsigned boundsTested = 0, subtreesRejected = 0;
    unsigned portalRejected = 0, transparentDraws = 0;
    double cullCpuMs = 0, submitCpuMs = 0;
};

class RenderVisitor : public PathVisitor{
public:
    // Initializes rendering with the supplied default shader program.
    explicit RenderVisitor(const std::shared_ptr<Program>& program)
        : PathVisitor(State::defaults(program)), m_culler(State::defaults(program)){
    }

    // Collects transformed lights and then draws the scene instances.
    void render(Node& root, const Camera& camera, const glm::mat4* cullingViewProjection = nullptr,
                const PortalResult* portals = nullptr, DeferredRenderer* deferred = nullptr);

    // Enables or disables hierarchical frustum rejection.
    void setCulling(bool enabled){
        m_culling = enabled;
    }

    // Returns whether frustum culling is enabled.
    bool getCulling() const{
        return m_culling;
    }

    // Selects the existing shader mode for all rendered instances.
    void setShadingMode(ShadingMode mode){
        m_shadingMode = mode;
    }

    // Returns the shader mode selected for the whole scene.
    ShadingMode getShadingMode() const{
        return m_shadingMode;
    }

    // Returns visible instances from the latest culling traversal.
    const std::vector<RenderNode>& getRenderList() const{
        return m_culler.getRenderList();
    }

    // Returns the draw, triangle and detail-level statistics of the latest frame.
    const RenderStats& getStats() const{
        return m_stats;
    }

private:
    // Collects lights before culling so off-screen lights still affect visible geometry.
    void collectLights(Node& root);

    // Builds the visible render list and records culling statistics.
    void collectGeometry(Node& root, const Camera& camera, const glm::mat4& viewProjection,
                         const PortalResult* portals);

    // Draws opaque instances first and transparent instances in stable back-to-front order.
    void drawRenderList(const Camera& camera);

    // Submits one resolved instance and updates room and transparency counts.
    void drawInstance(const RenderNode& item, const std::shared_ptr<Program>& program ={},
                      bool linearOutput = false);

    // Submits a mesh with its resolved transform and rendering state.
    void drawGeometry(const RenderNode& item, const std::shared_ptr<Program>& program,
                      bool linearOutput);

    DeferredRenderer* m_deferred = nullptr;
    State m_globalState;
    std::vector<PositionedLight> m_lights;
    const Camera* m_camera = nullptr;
    RenderStats m_stats;
    CullVisitor m_culler;
    bool m_culling = true;
    ShadingMode m_shadingMode = ShadingMode::Phong;
};
} // namespace vr
