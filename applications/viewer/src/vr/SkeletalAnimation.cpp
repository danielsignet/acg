#include <algorithm>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>
#include <stdexcept>
#include <vr/SkeletalAnimation.h>
#include <vr/Transform.h>

namespace vr{
namespace{
// Interpolates keys within their source range and holds the endpoints.
template <class T, class Interpolate>
T sample(const std::vector<SkeletalAnimation::Key<T>>& keys, double time, const T& fallback,
         Interpolate interpolate){
    if (keys.empty()){
        return fallback;
    }
    const auto next = std::upper_bound(keys.begin(), keys.end(), time,
                                       [](double t, const auto& key){ return t < key.time; });
    if (next == keys.begin()){
        return next->value;
    }
    if (next == keys.end()){
        return keys.back().value;
    }
    const auto& previous = *(next - 1);
    const auto factor = static_cast<float>((time - previous.time) / (next->time - previous.time));
    return interpolate(previous.value, next->value, factor);
}
} // namespace

// Owns copied animation data independently of Assimp's importer lifetime.
SkeletalAnimation::SkeletalAnimation(std::vector<Joint> joints, std::vector<Clip> clips,
                                     std::vector<Binding> bindings)
    : SkeletalAnimation(std::make_shared<const Source>(Source{std::move(joints), std::move(clips)}),
                        std::move(bindings)){
}

SkeletalAnimation::SkeletalAnimation(std::shared_ptr<const Source> source,
                                     std::vector<Binding> bindings)
    : m_source(std::move(source)), m_sampled(m_source->joints), m_bindings(std::move(bindings)),
      m_local(m_sampled.size()), m_global(m_sampled.size()){
    if (m_source->clips.empty()){
        throw std::invalid_argument("Skeletal animation requires source clips");
    }
    for (std::size_t i = 0; i < m_sampled.size(); ++i){
        if (m_sampled[i].parent >= static_cast<int>(i)){
            throw std::invalid_argument("Skeleton parents must precede their children");
        }
    }
    for (std::size_t i = 0; i < m_source->clips.size(); ++i){
        if (m_source->clips[i].name == "Idle"){
            m_clip = i;
        }
    }
    evaluate();
}

std::shared_ptr<SkeletalAnimation> SkeletalAnimation::instantiate() const{
    auto bindings = m_bindings;
    for (auto& binding : bindings){
        binding.pose = std::make_shared<SkinningPose>();
    }
    auto result =
        std::shared_ptr<SkeletalAnimation>(new SkeletalAnimation(m_source, std::move(bindings)));
    result->m_clip = m_clip;
    result->m_time = m_time;
    result->evaluate();
    return result;
}

void JointAnimationCallback::update(Node& node, const UpdateContext&){
    if (auto animation = m_animation.lock()){
        static_cast<Transform&>(node).setMatrix(animation->getLocal(m_joint));
    }
}

// Advances clip time only while scene animation is enabled.
void SkeletalAnimation::update(Node& node, const UpdateContext& context){
    if (context.animate && context.delta > 0){
        const auto duration = m_source->clips[m_clip].duration;
        m_time = duration > 0 ? std::fmod(m_time + context.delta, duration) : 0;
        evaluate();
    }
    if (auto transform = dynamic_cast<Transform*>(&node); transform && !m_local.empty()){
        transform->setMatrix(m_local[0]);
    }
}

// Selects a looping source clip and evaluates its first pose immediately.
void SkeletalAnimation::setClip(std::size_t index){
    if (index >= m_source->clips.size()){
        throw std::out_of_range("Animation clip");
    }
    m_clip = index;
    m_time = 0;
    evaluate();
}

// Samples TRS tracks and updates each mesh-local skin palette.
void SkeletalAnimation::evaluate(){
    m_sampled = m_source->joints;
    auto& pose = m_sampled;
    const auto linear = [](const auto& a, const auto& b, float t){ return glm::mix(a, b, t); };
    const auto spherical = [](const glm::quat& a, const glm::quat& b, float t){
        return glm::normalize(glm::slerp(a, b, t));
    };
    for (const auto& track : m_source->clips[m_clip].tracks){
        auto& joint = pose.at(track.joint);
        joint.position = sample(track.positions, m_time, joint.position, linear);
        joint.rotation = sample(track.rotations, m_time, joint.rotation, spherical);
        joint.scale = sample(track.scales, m_time, joint.scale, linear);
    }
    for (std::size_t i = 0; i < pose.size(); ++i){
        const auto& joint = pose[i];
        m_local[i] = glm::translate(glm::mat4(1), joint.position) * glm::mat4_cast(joint.rotation) *
                     glm::scale(glm::mat4(1), joint.scale);
        m_global[i] = joint.parent < 0 ? m_local[i] : m_global[joint.parent] * m_local[i];
    }
    for (const auto& binding : m_bindings){
        auto& skin = *binding.pose;
        const auto inverseMesh = glm::inverse(m_global.at(binding.mesh));
        skin.matrices.resize(binding.joints.size());
        skin.normals.resize(binding.joints.size());
        for (std::size_t i = 0; i < binding.joints.size(); ++i){
            skin.matrices[i] = inverseMesh * m_global.at(binding.joints[i]) * binding.offsets[i];
            skin.normals[i] = glm::transpose(glm::inverse(glm::mat3(skin.matrices[i])));
        }
        ++skin.revision;
    }
}
} // namespace vr
