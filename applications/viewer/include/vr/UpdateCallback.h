#pragma once
#include "CallbackVisitor.h"
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
class SceneRenderExtension;

class UpdateCallback{
public:
    // Allows derived objects to release their owned resources safely.
    virtual ~UpdateCallback() = default;
    virtual void accept(CallbackVisitor& visitor){visitor.apply(*this);}
    // Optional rendering capability, explicitly supplied by application callbacks.
    virtual SceneRenderExtension* renderExtension(){return nullptr;}

    // Updates the target from elapsed time while honoring its enabled state.
    virtual void update(Node& node, const UpdateContext& context) = 0;

    // Structural controllers can opt out of continuous reflection updates.
    virtual bool changesRenderedContent() const{ return true; }
};

template<class T> std::shared_ptr<T> callbackAs(const std::shared_ptr<UpdateCallback>& callback){
    if(!callback)return {};
    class Match final:public CallbackVisitor{
    public:
        T* result=nullptr;
        void apply(T& value) override{result=&value;}
    } visitor;
    callback->accept(visitor);
    return visitor.result?std::shared_ptr<T>(callback,visitor.result):nullptr;
}

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
