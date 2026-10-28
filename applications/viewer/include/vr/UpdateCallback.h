#pragma once
#include <functional>
#include <memory>
#include <utility>

namespace vr{
class FrameProfiler;
class GpuSkinning;

struct UpdateContext{
    double time;
    double delta;
    bool animate = true;
    FrameProfiler* profiler = nullptr;
    std::shared_ptr<GpuSkinning> gpuSkinning;
};

class Node;

class UpdateCallback{
public:
    // Allows derived objects to release their owned resources safely.
    virtual ~UpdateCallback() = default;

    // Updates the target from elapsed time while honoring its enabled state.
    virtual void update(Node& node, const UpdateContext& context) = 0;
};

class FunctionCallback : public UpdateCallback{
public:
    // Stores a callable as a node-update callback.
    explicit FunctionCallback(std::function<void(Node&, const UpdateContext&)> callback)
        : m_function(std::move(callback)){
    }

    // Updates the target from elapsed time while honoring its enabled state.
    void update(Node& node, const UpdateContext& context) override{
        m_function(node, context);
    }

private:
    std::function<void(Node&, const UpdateContext&)> m_function;
};
} // namespace vr
