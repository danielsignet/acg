#include <stdexcept>
#include <vr/LOD.h>
#include <vr/Light.h>
#include <vr/Group.h>
#include <vr/LightCollector.h>
#include <vr/Transform.h>

namespace vr{
namespace{
class LightCollector : public NodeVisitor{
public:
    // Collects one independent transformed light list for either rendering pipeline.
    std::vector<PositionedLight> collect(Node& root, const glm::vec3& eye){
        m_eye = eye;
        root.accept(*this);
        return std::move(m_lights);
    }

private:
    std::vector<PositionedLight> m_lights;
    std::vector<glm::mat4> m_models{glm::mat4(1)};
    glm::vec3 m_eye{0};
    std::vector<bool> m_visible{true};

    void apply(Group& node) override{
        m_visible.push_back(m_visible.back() && node.getVisible());
        try{ traverse(node); }
        catch (...){ m_visible.pop_back(); throw; }
        m_visible.pop_back();
    }

    void apply(Transform& node) override{
        m_models.push_back(node.getWorldMatrix(m_models.back()));
        try{
            apply(static_cast<Group&>(node));
        } catch (...){
            m_models.pop_back();
            throw;
        }
        m_models.pop_back();
    }

    void apply(LOD& node) override{
        const auto center = glm::vec3(m_models.back() * glm::vec4(node.getCenter(), 1));
        m_visible.push_back(m_visible.back() && node.getVisible());
        try{ node.getChildren().at(node.selectLevel(glm::distance(m_eye, center)))->accept(*this); }
        catch (...){ m_visible.pop_back(); throw; }
        m_visible.pop_back();
    }

    // Retains the transform accumulated along this light instance's graph path.
    void apply(Light& node) override{
        if (m_lights.size() >= State::MaxLights){
            throw std::runtime_error("Too many lights");
        }
        m_lights.push_back({&node, m_models.back(), m_visible.back() && node.getVisible()});
    }
};
} // namespace

// Collects transformed lights independently of observer frustum and portal visibility.
std::vector<PositionedLight> collectLights(Node& root, const glm::vec3& eye){
    LightCollector collector;
    return collector.collect(root, eye);
}
} // namespace vr
