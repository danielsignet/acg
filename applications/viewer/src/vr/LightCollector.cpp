#include <vr/LightCollector.h>
#include <vr/Light.h>
#include <stdexcept>

namespace vr{
namespace{
class LightCollector : public PathVisitor{
public:
    // Collects one independent transformed light list for either rendering pipeline.
    std::vector<PositionedLight> collect(Node& root, const glm::vec3& eye){
        setEye(eye);
        root.accept(*this);
        return std::move(m_lights);
    }

private:
    std::vector<PositionedLight> m_lights;

    // Retains the transform accumulated along this light instance's graph path.
    void light(Light& node, const glm::mat4& model, const State&) override{
        if (m_lights.size() >= State::MaxLights){
            throw std::runtime_error("Too many lights");
        }
        m_lights.push_back({&node, model});
    }
};
} // namespace

// Collects transformed lights independently of observer frustum and portal visibility.
std::vector<PositionedLight> collectLights(Node& root, const glm::vec3& eye){
    LightCollector collector;
    return collector.collect(root, eye);
}
} // namespace vr
