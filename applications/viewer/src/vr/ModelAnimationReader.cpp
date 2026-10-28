#include "ModelReader.h"
#include <functional>
#include <stdexcept>

namespace vr{
// Copies source clips and bone bindings before the importer releases its scene.
std::shared_ptr<SkeletalAnimation> ModelReader::readAnimation(const aiScene& scene){
    std::vector<SkeletalAnimation::Joint> joints;
    std::map<std::string, unsigned> names;
    std::vector<unsigned> meshes(scene.mNumMeshes);
    m_jointIndices.clear();
    const std::function<void(const aiNode&, int)> visit = [&](const aiNode& node, int parent){
        const auto index = static_cast<unsigned>(joints.size());
        m_jointIndices[&node] = index;
        if (!names.emplace(node.mName.C_Str(), index).second){
            throw std::runtime_error("Animated nodes require unique names");
        }
        aiVector3D scale, position;
        aiQuaternion rotation;
        node.mTransformation.Decompose(scale, rotation, position);
        joints.push_back({parent,
                          {position.x, position.y, position.z},
                          {scale.x, scale.y, scale.z},
                          {rotation.w, rotation.x, rotation.y, rotation.z}});
        for (unsigned i = 0; i < node.mNumMeshes; ++i){
            meshes[node.mMeshes[i]] = index;
        }
        for (unsigned i = 0; i < node.mNumChildren; ++i){
            visit(*node.mChildren[i], static_cast<int>(index));
        }
    };
    visit(*scene.mRootNode, -1);
    std::vector<SkeletalAnimation::Clip> clips;
    for (unsigned i = 0; i < scene.mNumAnimations; ++i){
        const auto& source = *scene.mAnimations[i];
        const double rate = source.mTicksPerSecond > 0 ? source.mTicksPerSecond : 25;
        SkeletalAnimation::Clip clip;
        clip.name = source.mName.C_Str();
        if (const auto separator = clip.name.find_last_of('|'); separator != std::string::npos){
            clip.name.erase(0, separator + 1);
        }
        clip.duration = source.mDuration / rate;
        for (unsigned j = 0; j < source.mNumChannels; ++j){
            const auto& channel = *source.mChannels[j];
            SkeletalAnimation::Track track;
            track.joint = names.at(channel.mNodeName.C_Str());
            for (unsigned k = 0; k < channel.mNumPositionKeys; ++k){
                const auto& key = channel.mPositionKeys[k];
                track.positions.push_back(
                    {key.mTime / rate,{key.mValue.x, key.mValue.y, key.mValue.z}});
            }
            for (unsigned k = 0; k < channel.mNumRotationKeys; ++k){
                const auto& key = channel.mRotationKeys[k];
                track.rotations.push_back(
                    {key.mTime / rate,{key.mValue.w, key.mValue.x, key.mValue.y, key.mValue.z}});
            }
            for (unsigned k = 0; k < channel.mNumScalingKeys; ++k){
                const auto& key = channel.mScalingKeys[k];
                track.scales.push_back(
                    {key.mTime / rate,{key.mValue.x, key.mValue.y, key.mValue.z}});
            }
            clip.tracks.push_back(std::move(track));
        }
        clips.push_back(std::move(clip));
    }
    std::vector<SkeletalAnimation::Binding> bindings;
    for (unsigned i = 0; i < scene.mNumMeshes; ++i){
        const auto& mesh = *scene.mMeshes[i];
        if (!mesh.mNumBones || !m_meshes[i]){
            continue;
        }
        SkeletalAnimation::Binding binding;
        binding.mesh = meshes[i];
        binding.pose = std::make_shared<SkinningPose>();
        for (unsigned j = 0; j < mesh.mNumBones; ++j){
            const auto& bone = *mesh.mBones[j];
            binding.joints.push_back(names.at(bone.mName.C_Str()));
            binding.offsets.push_back(convertMatrix(bone.mOffsetMatrix));
        }
        m_meshes[i]->setSkin(binding.pose);
        bindings.push_back(std::move(binding));
    }
    auto animation = std::make_shared<SkeletalAnimation>(std::move(joints), std::move(clips),
                                                         std::move(bindings));
    for (const auto& mesh : m_meshes){
        if (mesh){
            mesh->updateSkin();
        }
    }
    return animation;
}
} // namespace vr
