#pragma once
#include "UpdateCallback.h"
#include <glm/glm.hpp>

namespace vr{
class State;
// CPU query counterpart of bounce.glsl; rendering deforms vertices on the GPU.
glm::mat4 bounceMatrix(float radius, float time);
glm::vec2 bounceParameters(const State& state);

class BounceAnimation final : public UpdateCallback{
public:
    void accept(CallbackVisitor& visitor) override{visitor.apply(*this);}
    explicit BounceAnimation(float radius=1, double startTime=0, bool enabled=true);
    void update(Node& node, const UpdateContext& context) override;
    bool changesRenderedContent() const override{ return m_changed; }
    void setEnabled(bool enabled){ m_enabled=enabled; }
    bool getEnabled() const{ return m_enabled; }
    double getTime() const{ return m_time; }
private:
    float m_radius;
    double m_time;
    bool m_enabled, m_changed=false;
};
}
