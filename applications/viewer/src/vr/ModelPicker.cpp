#include <vr/ModelPicker.h>
#include <vr/FrameWork.h>
#include <vr/Geometry.h>
#include <vr/ModelInstance.h>
#include <vr/SkeletalAnimation.h>
#include <algorithm>
#include <cmath>
#include <limits>

namespace vr{
namespace{
std::optional<float> entryDistance(const BoundingBox& box,const glm::vec3& eye,const glm::vec3& ray){
    if(!box.valid()) return 0.0f; // Unknown bounds must not hide a target.
    float near=0,far=std::numeric_limits<float>::infinity();
    for(unsigned axis=0;axis<3;++axis){
        if(std::abs(ray[axis])<1e-8f){
            if(eye[axis]<box.getMinimum()[axis] || eye[axis]>box.getMaximum()[axis]) return {};
        }else{
            const float a=(box.getMinimum()[axis]-eye[axis])/ray[axis];
            const float b=(box.getMaximum()[axis]-eye[axis])/ray[axis];
            near=std::max(near,std::min(a,b));far=std::min(far,std::max(a,b));
            if(near>far) return {};
        }
    }
    return near;
}
}

// Finds the closest visible model, including glass, while ignoring transparent debug walls.
std::optional<ModelHit> pickModel(const std::vector<RenderNode>& items, const glm::vec3& eye,
                                  const glm::vec3& direction, float* surfaceDistance){
    ModelPicker picker;
    return picker.pick(items,eye,direction,surfaceDistance);
}

std::optional<ModelHit> ModelPicker::pick(const std::vector<RenderNode>& items,const glm::vec3& eye,
    const glm::vec3& direction,float* surfaceDistance){
    struct Candidate{const RenderNode* item;float entry;};
    std::vector<Candidate> candidates;
    std::vector<Signature> signatures;
    for(const auto& item:items){
        const auto glass=item.state.getUniforms().find("uGlass");
        if(item.state.getBlend() && !(glass!=item.state.getUniforms().end() && std::get<bool>(glass->second))) continue;
        const auto entry=frameWorkOptimized()?entryDistance(item.geometry->getWorldBound(item.model),eye,direction):std::optional<float>(0.0f);
        if(!entry) continue;
        candidates.push_back({&item,*entry});
        const auto texture=item.state.getTexture(0);
        signatures.push_back({item.geometry,item.instance,item.model,item.instanceModel,
            item.geometry->getDataRevision(),item.geometry->getSkin()?item.geometry->getSkin()->revision:0,
            item.state.getRevision(),texture?texture->getRevision():0,
            item.instance && item.instance->getSelectable()});
    }
    if(frameWorkOptimized() && m_valid && eye==m_eye && direction==m_direction && signatures==m_signatures){
        if(surfaceDistance) *surfaceDistance=m_distance;
        return m_hit;
    }
    ++m_queries;
    m_signatures=std::move(signatures);m_eye=eye;m_direction=direction;m_valid=true;
    std::stable_sort(candidates.begin(),candidates.end(),[](const Candidate& a,const Candidate& b){return a.entry<b.entry;});
    std::optional<ModelHit> hit;
    float nearest = std::numeric_limits<float>::infinity();
    for (const auto& candidate : candidates){
        if(candidate.entry>nearest) break;
        const auto& item=*candidate.item;
        const auto glass = item.state.getUniforms().find("uGlass");
        const bool refractive =
            glass != item.state.getUniforms().end() && std::get<bool>(glass->second);
        if (item.state.getBlend() && !refractive){
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
    if(surfaceDistance) *surfaceDistance=nearest;
    m_distance=nearest;m_hit=hit;
    return hit;
}
} // namespace vr
