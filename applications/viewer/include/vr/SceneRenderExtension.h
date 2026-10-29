#pragma once
#include "RenderMode.h"
#include <memory>

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
};

// Optional application-owned auxiliary passes, discovered from node callbacks.
// The hook runs after skinning preparation and for each observer/mirror camera.
class SceneRenderExtension{
public:
    virtual ~SceneRenderExtension()=default;
    virtual void beforeSceneRender(const SceneRenderContext& context)=0;
};
}
