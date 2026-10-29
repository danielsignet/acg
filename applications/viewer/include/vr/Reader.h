#pragma once
#include "SceneGraph.h"
#include "UpdateCallback.h"
#include <utility>

namespace vr{
using MaterialOverrides = std::map<std::string, std::shared_ptr<State>>;

class Reader{
public:
    using AnimationAttributes = std::map<std::string, std::string>;
    using AnimationFactory = std::function<std::shared_ptr<UpdateCallback>(
        const std::shared_ptr<Transform>&, const AnimationAttributes&)>;

    // Registers application-owned XML animations without adding scene-specific
    // callbacks to the library. Register before loading scenes; spin is reserved.
    static void registerTransformAnimation(const std::string& name, AnimationFactory factory);

    // Creates a registered animation, reporting unsupported names explicitly.
    static std::shared_ptr<UpdateCallback> createTransformAnimation(
        const std::string& name, const std::shared_ptr<Transform>& target,
        const AnimationAttributes& attributes);

    // Stores the resource root used for model and texture lookup.
    explicit Reader(std::filesystem::path root) : m_root(std::move(root)){
    }

    // Shares imported hierarchies, or explicitly flattened static model variants.
    std::shared_ptr<Node> readModel(const std::filesystem::path& file, bool flatten = false,
                                    const MaterialOverrides& materials ={});

    // Builds an XML scene using this reader's shared resource caches.
    std::shared_ptr<Scene> readScene(const std::filesystem::path& file);

    // Resolves and shares a decoded texture while it remains in use.
    std::shared_ptr<Texture> readTexture(const std::filesystem::path& file);

    // Registers a reusable asset and rejects empty or duplicate definitions.
    void registerNode(const std::string& id, std::shared_ptr<Node> node);

    // Finds a reusable graph asset and reports unknown identifiers.
    std::shared_ptr<Node> getAsset(const std::string& id) const;

    // Returns the root owned by this scene or resource reader.
    const std::filesystem::path& getRoot() const{
        return m_root;
    }

private:
    // Finds a resource relative to the configured root and validates its existence.
    std::filesystem::path resolve(const std::filesystem::path& file) const;
    std::filesystem::path m_root;
    std::map<std::string, std::shared_ptr<Node>> m_assets;
    std::map<std::pair<std::string, bool>, std::weak_ptr<Node>> m_models;
    std::map<std::string, std::weak_ptr<Texture>> m_textures;
};
} // namespace vr
