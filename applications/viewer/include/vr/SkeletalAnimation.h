#pragma once
#include "UpdateCallback.h"
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <cstdint>
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
    // Advances clip time only while scene animation is enabled.
    void update(Node& node, const UpdateContext& context) override;
    // Selects a looping source clip and evaluates its first pose immediately.
    void setClip(std::size_t index);

    // Returns the source clips for contextual controls.
    const std::vector<Clip>& getClips() const{
        return m_clips;
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
    // Samples TRS tracks and updates each mesh-local skin palette.
    void evaluate();
    std::vector<Joint> m_joints;
    std::vector<Clip> m_clips;
    std::vector<Binding> m_bindings;
    std::vector<glm::mat4> m_local, m_global;
    std::size_t m_clip = 0;
    double m_time = 0;
};
} // namespace vr
