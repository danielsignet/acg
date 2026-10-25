#pragma once
#include "CullVisitor.h"
#include "PathVisitor.h"
#include <map>

namespace vr{
struct RenderStats{
    unsigned draws = 0;
    std::size_t triangles = 0;
    std::map<std::string, std::size_t> lodLevels;
    std::map<std::string, unsigned> roomDraws;
    unsigned boundsTested = 0, subtreesRejected = 0;
    double cullCpuMs = 0, submitCpuMs = 0;
};

class RenderVisitor : public PathVisitor{
public:
    // Initializes rendering with the supplied default shader program.
    explicit RenderVisitor(const std::shared_ptr<Program>& program)
        : PathVisitor(State::defaults(program)), m_culler(State::defaults(program)){
    }

    // Collects transformed lights and then draws the scene instances.
    void render(Node& root, const Camera& camera, const glm::mat4* cullingViewProjection = nullptr);

    void setCulling(bool enabled){
        m_culling = enabled;
    }

    bool getCulling() const{
        return m_culling;
    }

    const std::vector<RenderNode>& getRenderList() const{
        return m_culler.getRenderList();
    }

    // Returns the draw, triangle and detail-level statistics of the latest frame.
    const RenderStats& getStats() const{
        return m_stats;
    }

private:
    // Handles a geometry instance with its resolved transform and rendering state.
    void geometry(Geometry& node, const glm::mat4& model, const State& state) override;

    // Handles a light with the transform accumulated along its current path.
    void light(Light& node, const glm::mat4& model, const State& state) override;

    // Records the detail level selected for this traversal path.
    void lod(LOD& node, std::size_t level) override;

    struct PositionedLight{
        Light* node;
        glm::mat4 model;
    };

    std::vector<PositionedLight> m_lights;
    const Camera* m_camera = nullptr;
    bool m_collectingLights = false;
    RenderStats m_stats;
    CullVisitor m_culler;
    bool m_culling = true;
};
} // namespace vr
