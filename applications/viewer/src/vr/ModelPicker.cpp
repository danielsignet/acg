#include <vr/ModelPicker.h>
#include <vr/Geometry.h>
#include <vr/ModelInstance.h>
#include <limits>

namespace vr{
// Finds the closest visible opaque model along a world-space ray, testing bounds first.
std::optional<ModelHit> pickModel(const std::vector<RenderNode>& items, const glm::vec3& eye,
                                  const glm::vec3& direction){
    std::optional<ModelHit> hit;
    float nearest = std::numeric_limits<float>::infinity();
    for (const auto& item : items){
        if (item.state.getBlend()){
            continue;
        }
        const auto inverse = glm::inverse(item.model);
        const auto origin = glm::vec3(inverse * glm::vec4(eye, 1));
        const auto ray = glm::mat3(inverse) * direction;
        const auto alpha = item.state.getUniforms().find("uAlphaCutoff");
        const float cutoff =
            alpha == item.state.getUniforms().end() ? 0 : std::get<float>(alpha->second);
        const auto* texture = item.state.getTexture(0).get();
        std::function<bool(const glm::vec2&)> accepts;
        if (texture && cutoff > 0){
            accepts = [texture, cutoff](const glm::vec2& coordinates){
                return texture->sampleAlpha(coordinates) >= cutoff;
            };
        }
        if (const auto distance = item.geometry->intersectRay(origin, ray, nearest, accepts)){
            nearest = *distance;
            hit = item.instance && item.instance->getSelectable()
                      ? std::optional<ModelHit>{{item.instance, item.instanceModel, *distance}}
                      : std::nullopt;
        }
    }
    return hit;
}
} // namespace vr
