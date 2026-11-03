#pragma once
#include "Transform.h"
#include "UpdateCallback.h"

namespace vr{
class SpinCallback final : public UpdateCallback{
public:
    void accept(CallbackVisitor& visitor) override{visitor.apply(*this);}
    // Captures the base transform and a normalized local rotation axis.
    SpinCallback(const std::shared_ptr<Transform>& target, float degreesPerSecond,
                 const glm::vec3& axis ={0.3f, 1, 0.2f});

    // Rotates the local object only while object animation is enabled.
    void update(Node& node, const UpdateContext& context) override;
    void setEnabled(bool enabled){ m_enabled=enabled; }
    bool getEnabled() const{ return m_enabled; }
    bool changesRenderedContent() const override{ return m_changed; }

private:
    std::weak_ptr<Transform> m_target;
    glm::mat4 m_baseMatrix;
    glm::vec3 m_axis;
    float m_radiansPerSecond;
    float m_angleRadians = 0;
    bool m_enabled=true, m_changed=false;
};
} // namespace vr
