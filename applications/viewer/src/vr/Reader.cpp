#include "ModelReader.h"
#include "SceneReader.h"
#include <stdexcept>
#include <vr/Reader.h>

namespace vr{
namespace{
std::map<std::string, Reader::AnimationFactory>& animationFactories(){
    static std::map<std::string, Reader::AnimationFactory> factories;
    return factories;
}
}

void Reader::registerTransformAnimation(const std::string& name, AnimationFactory factory){
    if (name.empty() || name == "spin" || !factory ||
        !animationFactories().emplace(name, std::move(factory)).second){
        throw std::invalid_argument("Invalid or duplicate animation: " + name);
    }
}

std::shared_ptr<UpdateCallback> Reader::createTransformAnimation(
    const std::string& name, const std::shared_ptr<Transform>& target,
    const AnimationAttributes& attributes){
    const auto found = animationFactories().find(name);
    if (found == animationFactories().end()){
        throw std::runtime_error("Unregistered transform animation: " + name);
    }
    auto callback = found->second(target, attributes);
    if (!callback){
        throw std::runtime_error("Animation factory returned an empty callback: " + name);
    }
    return callback;
}

// Finds a resource relative to the configured root and validates its existence.
std::filesystem::path Reader::resolve(const std::filesystem::path& file) const{
    auto path = file.is_absolute() ? file : m_root / file;
    if (!std::filesystem::is_regular_file(path)){
        throw std::runtime_error("File not found: " + path.string());
    }
    return std::filesystem::weakly_canonical(path);
}

// Resolves and shares a decoded texture while it remains in use.
std::shared_ptr<Texture> Reader::readTexture(const std::filesystem::path& file){
    const auto path = resolve(file);
    const auto key = path.generic_string();
    if (auto p = m_textures[key].lock()){
        return p;
    }
    auto texture = Texture::fromFile(path);
    m_textures[key] = texture;
    return texture;
}

// Registers a reusable asset and rejects empty or duplicate definitions.
void Reader::registerNode(const std::string& id, std::shared_ptr<Node> node){
    if (!node || !m_assets.emplace(id, std::move(node)).second){
        throw std::runtime_error("Invalid or duplicate asset: " + id);
    }
}

// Finds a reusable graph asset and reports unknown identifiers.
std::shared_ptr<Node> Reader::getAsset(const std::string& id) const{
    auto i = m_assets.find(id);
    if (i == m_assets.end()){
        throw std::runtime_error("Unknown asset: " + id);
    }
    return i->second;
}

// Caches each import variant independently while retaining hierarchy by default.
std::shared_ptr<Node> Reader::readModel(const std::filesystem::path& file, bool flatten,
                                        const MaterialOverrides& materials){
    const auto path = resolve(file);
    if (!materials.empty()){
        return ModelReader(*this).read(path, flatten, materials);
    }
    const auto key = std::make_pair(path.generic_string(), flatten);
    if (auto cached = m_models[key].lock()){
        return cached;
    }
    auto model = ModelReader(*this).read(path, flatten);
    m_models[key] = model;
    return model;
}

// Builds an XML scene using this reader's shared resource caches.
std::shared_ptr<Scene> Reader::readScene(const std::filesystem::path& file){
    return SceneReader(*this).read(resolve(file));
}

} // namespace vr
