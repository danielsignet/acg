#pragma once
#include "RenderMode.h"
#include <memory>
#include <string>

namespace vr{
class Node;
class Camera;
class EnvironmentMap;
class Program;
class FrameProfiler;

struct SceneRenderContext{
    Node& root;
    const Camera& camera;
    EnvironmentMap* environment;
    std::shared_ptr<Program> program;
    ShadingMode shading;
    int width,height;
    bool environmentCapture=false;
    FrameProfiler* profiler=nullptr;
    bool reflectionPass=false;
};

// Optional application-owned auxiliary passes, discovered from node callbacks.
// The hook runs after skinning preparation and for each observer/mirror camera.
class SceneRenderExtension{
public:
    virtual ~SceneRenderExtension()=default;
    virtual void beforeSceneRender(const SceneRenderContext& context)=0;
    // Restore view-local graph state after a normal or failed auxiliary render.
    virtual void afterSceneRender(const SceneRenderContext&) noexcept{}
    // Update auxiliary-view dependencies before any light depth maps are rendered.
    virtual void prepareFrameView(const SceneRenderContext&){}
    // Reject reflection targets hidden from the primary observer by room apertures.
    virtual bool roomVisible(const std::string&, const Camera&) const{ return true; }
    // Filter leaf submissions without rejecting a parent that also owns a passage.
    virtual bool roomVisibleInView(const std::string&, const SceneRenderContext&) const{ return true; }
    // Auxiliary observers may still see motion outside the primary observer's rooms.
    virtual bool dynamicEnvironment() const{ return false; }
};
}
