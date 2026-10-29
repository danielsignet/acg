#pragma once
#include "ColorTarget.h"
#include "CullVisitor.h"
#include "FrameProfiler.h"
#include "LightCollector.h"
#include "PathVisitor.h"
#include "RenderMode.h"
#include "SceneRenderExtension.h"
#include <functional>
#include <map>
#include <set>

namespace vr{
class DeferredRenderer;
class EnvironmentMap;
class PlanarReflection;

struct RenderStats{
    unsigned instances = 0, instancedDraws = 0;
    unsigned draws = 0, lights = 0, visitedNodes = 0, screenDraws = 0, screenCopies = 0;
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

    // Collects lights and geometry, invokes the optional preparation hook, then draws the scene.
    void render(Node& root, const Camera& camera, const glm::mat4* cullingViewProjection = nullptr,
                const PortalResult* portals = nullptr, DeferredRenderer* deferred = nullptr,
                const std::function<void()>& beforeDraw ={});

    // Attaches the optional frame profiler without transferring ownership.
    void setProfiler(FrameProfiler* profiler){
        m_profiler = profiler;
    }

    // Enables or disables hierarchical frustum rejection.
    void setCulling(bool enabled){
        m_culling = enabled;
    }

    // Returns whether frustum culling is enabled.
    bool getCulling() const{
        return m_culling;
    }

    void setInstancing(bool enabled){
        m_instancing = enabled;
    }

    bool getInstancing() const{
        return m_instancing;
    }

    void setStateCaching(bool enabled){
        PathVisitor::setStateCaching(enabled);
        m_culler.setStateCaching(enabled);
    }

    std::size_t getResolvedStateCacheHits() const{
        return m_culler.getStateCacheHits();
    }

    std::size_t getResolvedStateCacheMisses() const{
        return m_culler.getStateCacheMisses();
    }

    // Selects the existing shader mode for all rendered instances.
    void setShadingMode(ShadingMode mode){
        m_shadingMode = mode;
    }

    // Returns the shader mode selected for the whole scene.
    ShadingMode getShadingMode() const{
        return m_shadingMode;
    }

    // Shares precomputed environment lighting with forward surfaces and the background.
    void setEnvironment(EnvironmentMap* environment){
        m_environment = environment;
    }

    // Publishes the current planar reflection only to surfaces explicitly marked as mirrors.
    void setReflection(const PlanarReflection* reflection){
        m_reflection = reflection;
    }

    // Configures an auxiliary HDR pass with a world-space mirror clipping plane.
    void setReflectionPass(const glm::vec4& plane){
        m_clipPlane = plane;
        m_linearOutput = true;
    }

    // Captures linear scene radiance with static IBL and without recursive mirror images.
    void setEnvironmentCapture(){
        m_environmentCapture = true;
        m_linearOutput = true;
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
    Node* m_extensionRoot=nullptr;
    std::vector<std::weak_ptr<SceneRenderExtension>> m_extensions;
    FrameProfiler* m_profiler = nullptr;
    // Collects lights before culling so off-screen lights still affect visible geometry.
    void collectLights(Node& root);

    // Builds the visible render list and records culling statistics.
    void collectGeometry(Node& root, const Camera& camera, const glm::mat4& viewProjection,
                         const PortalResult* portals);

    // Draws opaque instances first and transparent instances in stable back-to-front order.
    void drawRenderList(const Camera& camera);
    void drawOpaque(const std::vector<const RenderNode*>& items,
                    const std::shared_ptr<Program>& program, bool linearOutput);

    // Submits one resolved instance and updates room and transparency counts.
    void drawInstance(const RenderNode& item, const std::shared_ptr<Program>& program ={},
                      bool linearOutput = false);

    // Submits a mesh with its resolved transform and rendering state.
    void drawGeometry(const RenderNode& item, const std::shared_ptr<Program>& program,
                      bool linearOutput, const std::vector<glm::mat4>* models = nullptr);

    // Copies the current color before refractive glass samples the background.
    void captureTransmission();

    DeferredRenderer* m_deferred = nullptr;
    EnvironmentMap* m_environment = nullptr;
    const PlanarReflection* m_reflection = nullptr;
    std::set<GLuint> m_environmentPrograms;
    std::unique_ptr<ColorTarget> m_transmission;
    glm::vec4 m_viewport{0};
    std::optional<glm::vec4> m_clipPlane;
    bool m_linearOutput = false;
    bool m_environmentCapture = false;
    State m_globalState;
    std::vector<PositionedLight> m_lights;
    const Camera* m_camera = nullptr;
    RenderStats m_stats;
    CullVisitor m_culler;
    bool m_culling = true;
    bool m_instancing = true;
    ShadingMode m_shadingMode = ShadingMode::Phong;
};
} // namespace vr
