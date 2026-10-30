#pragma once
#include "CullVisitor.h"

namespace vr{
struct ModelHit{
    ModelInstance* instance;
    glm::mat4 model;
    float distance;
};

class ModelPicker{
public:
    std::optional<ModelHit> pick(const std::vector<RenderNode>& items, const glm::vec3& eye,
        const glm::vec3& direction, float* surfaceDistance = nullptr);
    void invalidate(){ m_valid=false; m_signatures.clear(); }
    std::size_t getQueryCount() const{ return m_queries; }
private:
    struct Signature{
        Geometry* geometry;
        ModelInstance* instance;
        glm::mat4 model, instanceModel;
        std::uint64_t data, pose, state, texture;
        bool selectable;
        bool operator==(const Signature& b) const{
            return geometry==b.geometry && instance==b.instance && model==b.model &&
                instanceModel==b.instanceModel && data==b.data && pose==b.pose &&
                state==b.state && texture==b.texture && selectable==b.selectable;
        }
    };
    std::vector<Signature> m_signatures;
    glm::vec3 m_eye{0},m_direction{0};
    std::optional<ModelHit> m_hit;
    float m_distance = 0;
    bool m_valid=false;
    std::size_t m_queries=0;
};

// Finds the closest visible model, including glass, while ignoring transparent debug walls.
std::optional<ModelHit> pickModel(const std::vector<RenderNode>& items, const glm::vec3& eye,
                                  const glm::vec3& direction, float* surfaceDistance=nullptr);
} // namespace vr
