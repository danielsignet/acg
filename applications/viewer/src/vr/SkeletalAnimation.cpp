#include <vr/SkeletalAnimation.h>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>
#include <stdexcept>

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
    : m_joints(std::move(joints)), m_clips(std::move(clips)), m_bindings(std::move(bindings)),
      m_local(m_joints.size()), m_global(m_joints.size()){
    if (m_clips.empty()){
        throw std::invalid_argument("Skeletal animation requires source clips");
    }
    for (std::size_t i = 0; i < m_joints.size(); ++i){
        if (m_joints[i].parent >= static_cast<int>(i)){
            throw std::invalid_argument("Skeleton parents must precede their children");
        }
    }
    for (std::size_t i = 0; i < m_clips.size(); ++i){
        if (m_clips[i].name == "Idle"){
            m_clip = i;
        }
    }
    evaluate();
}

// Advances clip time only while scene animation is enabled.
void SkeletalAnimation::update(Node&, const UpdateContext& context){
    if (context.animate && context.delta > 0){
        const auto duration = m_clips[m_clip].duration;
        m_time = duration > 0 ? std::fmod(m_time + context.delta, duration) : 0;
        evaluate();
    }
}

// Selects a looping source clip and evaluates its first pose immediately.
void SkeletalAnimation::setClip(std::size_t index){
    if (index >= m_clips.size()){
        throw std::out_of_range("Animation clip");
    }
    m_clip = index;
    m_time = 0;
    evaluate();
}

// Samples TRS tracks and updates each mesh-local skin palette.
void SkeletalAnimation::evaluate(){
    auto pose = m_joints;
    const auto linear = [](const auto& a, const auto& b, float t){ return glm::mix(a, b, t); };
    const auto spherical = [](const glm::quat& a, const glm::quat& b, float t){
        return glm::normalize(glm::slerp(a, b, t));
    };
    for (const auto& track : m_clips[m_clip].tracks){
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
