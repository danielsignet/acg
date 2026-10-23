#pragma once
#include <assimp/scene.h>
#include <vr/Reader.h>

namespace vr{
class ModelReader final{
public:
    // Uses the shared resource reader to resolve model textures.
    explicit ModelReader(Reader& reader) : m_reader(reader){
    }

    // Converts one imported scene into nodes without flattening its hierarchy.
    std::shared_ptr<Node> read(const std::filesystem::path& path);

private:
    // Reads material colors, shininess and an optional diffuse image.
    std::shared_ptr<State> readMaterial(const aiMaterial& material, const aiScene& scene);

    // Resolves an embedded texture or a file relative to the model.
    std::shared_ptr<Texture> readTexture(const aiString& name, const aiScene& scene);

    // Converts compressed images or raw BGRA texels into RGBA pixels.
    std::shared_ptr<Texture> readEmbeddedTexture(const aiTexture& texture) const;

    // Copies local vertex attributes and triangle indices into a mesh.
    std::shared_ptr<Geometry> readMesh(const aiMesh& mesh) const;

    // Restores local transforms while reusing shared mesh objects.
    std::shared_ptr<Node> readNode(const aiNode& node) const;

    // Maps Assimp row indexing to GLM column indexing.
    static glm::mat4 convertMatrix(const aiMatrix4x4& matrix);
    Reader& m_reader;
    std::filesystem::path m_modelDirectory;
    std::vector<std::shared_ptr<State>> m_materials;
    std::vector<std::shared_ptr<Geometry>> m_meshes;
};
} // namespace vr
