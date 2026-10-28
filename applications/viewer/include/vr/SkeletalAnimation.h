#pragma once
#include "UpdateCallback.h"
#include <cstdint>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <memory>
#include <string>
#include <vector>

namespace vr{
// Shares an evaluated mesh palette between original and subdivided geometry.
struct SkinningPose{
    std::vector<glm::mat4> matrices;
    std::vector<glm::mat3> normals;
    std::uint64_t revision = 0;
};

class SkeletalAnimation final : public UpdateCallback{
public:
    struct Joint{
        int parent = -1;
        glm::vec3 position{0}, scale{1};
        glm::quat rotation{1, 0, 0, 0};
    };

    template <class T> struct Key{
        double time;
        T value;
    };

    struct Track{
        unsigned joint;
        std::vector<Key<glm::vec3>> positions, scales;
        std::vector<Key<glm::quat>> rotations;
    };

    struct Clip{
        std::string name;
        double duration = 0;
        std::vector<Track> tracks;
    };

    struct Binding{
        unsigned mesh;
        std::vector<unsigned> joints;
        std::vector<glm::mat4> offsets;
        std::shared_ptr<SkinningPose> pose;
    };

    // Owns copied animation data independently of Assimp's importer lifetime.
    SkeletalAnimation(std::vector<Joint> joints, std::vector<Clip> clips,
                      std::vector<Binding> bindings);
    // Shares immutable source data while allocating independent playback and palettes.
    std::shared_ptr<SkeletalAnimation> instantiate() const;

    const std::vector<Binding>& getBindings() const{
        return m_bindings;
    }

    // Advances clip time only while scene animation is enabled.
    void update(Node& node, const UpdateContext& context) override;
    // Selects a looping source clip and evaluates its first pose immediately.
    void setClip(std::size_t index);

    // Returns the source clips for contextual controls.
    const std::vector<Clip>& getClips() const{
        return m_source->clips;
    }

    // Reports the selected source clip.
    std::size_t getClip() const{
        return m_clip;
    }

    // Returns the evaluated local transform for an imported node.
    const glm::mat4& getLocal(unsigned joint) const{
        return m_local.at(joint);
    }

private:
    struct Source{
        std::vector<Joint> joints;
        std::vector<Clip> clips;
    };

    SkeletalAnimation(std::shared_ptr<const Source> source, std::vector<Binding> bindings);
    // Samples TRS tracks and updates each mesh-local skin palette.
    void evaluate();
    std::shared_ptr<const Source> m_source;
    std::vector<Joint> m_sampled;
    std::vector<Binding> m_bindings;
    std::vector<glm::mat4> m_local, m_global;
    std::size_t m_clip = 0;
    double m_time = 0;
};

// Synchronizes an imported transform with one joint of its instance's current pose.
class JointAnimationCallback final : public UpdateCallback{
public:
    JointAnimationCallback(std::weak_ptr<SkeletalAnimation> animation, unsigned joint)
        : m_animation(std::move(animation)), m_joint(joint){
    }

    void update(Node& node, const UpdateContext& context) override;

    unsigned getJoint() const{
        return m_joint;
    }

private:
    std::weak_ptr<SkeletalAnimation> m_animation;
    unsigned m_joint;
};
} // namespace vr
