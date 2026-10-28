#include "ModelReader.h"
#include <algorithm>
#include <cctype>
#include <assimp/Importer.hpp>
#include <assimp/GltfMaterial.h>
#include <assimp/postprocess.h>
#include <iostream>
#include <stdexcept>
#include <set>

namespace vr{
// Preserves imported transforms, or merges an explicitly selected static variant.
std::shared_ptr<Node> ModelReader::read(const std::filesystem::path& path, bool flatten,
                                        const MaterialOverrides& materials){
    Assimp::Importer importer;
    unsigned flags = aiProcess_Triangulate | aiProcess_GenSmoothNormals |
                     aiProcess_JoinIdenticalVertices | aiProcess_SortByPType |
                     aiProcess_ValidateDataStructure;
    if (flatten){
        flags |= aiProcess_PreTransformVertices | aiProcess_OptimizeMeshes;
        if (materials.empty()){
            flags |= aiProcess_RemoveRedundantMaterials;
        }
    }
    const auto* scene = importer.ReadFile(path.string(), flags);
    if (!scene || !scene->mRootNode){
        throw std::runtime_error("Assimp: " + path.string() + ": " + importer.GetErrorString());
    }
    m_modelDirectory = path.parent_path();
    auto extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
    // Assimp flips glTF's V coordinates. Keep imported and regenerated tangent frames consistent.
    m_normalY = extension == ".gltf" || extension == ".glb" ? -1.0f : 1.0f;
    m_materials.clear();
    std::set<std::string> matched;
    for (unsigned index = 0; index < scene->mNumMaterials; ++index){
        const auto& imported = *scene->mMaterials[index];
        auto state = readMaterial(imported, *scene);
        aiString name;
        imported.Get(AI_MATKEY_NAME, name);
        for (const auto& selector :{std::string("*"), std::string(name.C_Str())}){
            if (const auto found = materials.find(selector); found != materials.end()){
                *state = state->merged(*found->second);
                matched.insert(selector);
            }
        }
        m_materials.push_back(std::move(state));
    }
    for (const auto& entry : materials){
        if (!matched.count(entry.first)){
            throw std::runtime_error("Unknown material override: " + entry.first);
        }
    }
    m_meshes.clear();
    for (unsigned index = 0; index < scene->mNumMeshes; ++index){
        m_meshes.push_back(readMesh(*scene->mMeshes[index]));
    }
    auto root = readNode(*scene->mRootNode);
    std::cout << "Loaded " << path.filename().generic_string() << ": " << m_meshes.size()
              << (flatten ? " meshes, static import\n" : " meshes, hierarchy retained\n");
    return root;
}

// Reads only the material properties provided by the source.
std::shared_ptr<State> ModelReader::readMaterial(const aiMaterial& imported, const aiScene& scene){
    auto material = std::make_shared<Material>();
    aiColor3D color;
    if (imported.Get(AI_MATKEY_COLOR_AMBIENT, color) == AI_SUCCESS){
        material->setAmbient({color.r, color.g, color.b});
    }
    if (imported.Get(AI_MATKEY_COLOR_DIFFUSE, color) == AI_SUCCESS){
        material->setDiffuse({color.r, color.g, color.b});
    }
    if (imported.Get(AI_MATKEY_COLOR_SPECULAR, color) == AI_SUCCESS){
        material->setSpecular({color.r, color.g, color.b});
    }
    float shininess = 48.0f;
    if (imported.Get(AI_MATKEY_SHININESS, shininess) == AI_SUCCESS){
        material->setShininess(std::clamp(shininess, 1.0f, 1024.0f));
    }
    auto state = std::make_shared<State>();
    state->setMaterial(material);
    float metallic = 0, roughness = 0.35f;
    if (imported.Get(AI_MATKEY_METALLIC_FACTOR, metallic) == AI_SUCCESS){
        material->setMetallic(std::clamp(metallic, 0.0f, 1.0f));
    }
    if (imported.Get(AI_MATKEY_ROUGHNESS_FACTOR, roughness) == AI_SUCCESS){
        material->setRoughness(std::clamp(roughness, 0.0f, 1.0f));
    }
    if (imported.Get(AI_MATKEY_COLOR_EMISSIVE, color) == AI_SUCCESS){
        state->setUniform("uEmissive", glm::vec3(color.r, color.g, color.b));
    }
    aiString image;
    if (imported.GetTexture(aiTextureType_DIFFUSE, 0, &image) == AI_SUCCESS){
        state->setTexture(0, readTexture(image, scene));
    }
    unsigned uvSet = 0;
    if (imported.GetTexture(aiTextureType_NORMALS, 0, &image, nullptr, &uvSet) == AI_SUCCESS){
        if (uvSet == 0){
            state->setNormalMap(readTexture(image, scene));
            float scale = 1;
            imported.Get(AI_MATKEY_GLTF_TEXTURE_SCALE(aiTextureType_NORMALS, 0), scale);
            state->setUniform("uNormalScale", scale);
            state->setUniform("uNormalY", m_normalY);
        } else{
            std::cout << "Normal texture requires unsupported UV set " << uvSet << '\n';
        }
    }
    return state;
}

// Resolves and shares a decoded texture while it remains in use.
std::shared_ptr<Texture> ModelReader::readTexture(const aiString& name, const aiScene& scene){
    if (const auto* embedded = scene.GetEmbeddedTexture(name.C_Str())){
        return readEmbeddedTexture(*embedded);
    }
    std::string portableName = name.C_Str();
    std::replace(portableName.begin(), portableName.end(), '\\', '/');
    std::filesystem::path path(portableName);
    if (std::filesystem::is_regular_file(m_modelDirectory / path)){
        path = m_modelDirectory / path;
    } else if (std::filesystem::is_regular_file(m_modelDirectory / "textures" / path.filename())){
        path = m_modelDirectory / "textures" / path.filename();
    } else if (std::filesystem::is_regular_file(m_modelDirectory / path.filename())){
        path = m_modelDirectory / path.filename();
    }
    return m_reader.readTexture(path);
}

// Converts compressed images or raw BGRA texels into RGBA pixels.
std::shared_ptr<Texture> ModelReader::readEmbeddedTexture(const aiTexture& texture) const{
    if (texture.mHeight == 0){
        return Texture::fromMemory(reinterpret_cast<const unsigned char*>(texture.pcData),
                                   static_cast<int>(texture.mWidth));
    }
    std::vector<unsigned char> pixels(static_cast<std::size_t>(texture.mWidth) * texture.mHeight *
                                      4);
    for (unsigned row = 0; row < texture.mHeight; ++row){
        for (unsigned column = 0; column < texture.mWidth; ++column){
            const auto pixel = texture.pcData[row * texture.mWidth + column];
            const auto offset = ((texture.mHeight - 1 - row) * texture.mWidth + column) * 4;
            pixels[offset] = pixel.r;
            pixels[offset + 1] = pixel.g;
            pixels[offset + 2] = pixel.b;
            pixels[offset + 3] = pixel.a;
        }
    }
    return std::make_shared<Texture>(static_cast<int>(texture.mWidth),
                                     static_cast<int>(texture.mHeight), std::move(pixels));
}

// Copies local vertex attributes and triangle indices into a mesh.
std::shared_ptr<Geometry> ModelReader::readMesh(const aiMesh& mesh) const{
    std::vector<glm::vec3> positions;
    std::vector<glm::vec3> normals;
    std::vector<glm::vec2> textureCoordinates;
    std::vector<glm::vec4> tangents;
    std::vector<unsigned> indices;
    for (unsigned index = 0; index < mesh.mNumVertices; ++index){
        const auto position = mesh.mVertices[index];
        positions.push_back({position.x, position.y, position.z});
        const auto normal = mesh.HasNormals() ? mesh.mNormals[index] : aiVector3D(0, 1, 0);
        normals.push_back({normal.x, normal.y, normal.z});
        if (mesh.HasTangentsAndBitangents()){
            const auto t = mesh.mTangents[index], b = mesh.mBitangents[index];
            const glm::vec3 tangent(t.x, t.y, t.z), bitangent(b.x, b.y, b.z);
            const float sign =
                glm::dot(glm::cross(normals.back(), tangent), bitangent) < 0 ? -1.0f : 1.0f;
            tangents.emplace_back(tangent, sign * m_normalY);
        }
        textureCoordinates.push_back(
            mesh.HasTextureCoords(0)
                ? glm::vec2(mesh.mTextureCoords[0][index].x, mesh.mTextureCoords[0][index].y)
                : glm::vec2(0));
    }
    for (unsigned index = 0; index < mesh.mNumFaces; ++index){
        const auto& face = mesh.mFaces[index];
        if (face.mNumIndices == 3){
            indices.insert(indices.end(), face.mIndices, face.mIndices + 3);
        }
    }
    if (indices.empty()){
        return nullptr;
    }
    auto geometry = std::make_shared<Geometry>(mesh.mName.C_Str());
    geometry->setData(std::move(positions), std::move(indices), std::move(normals),
                      std::move(textureCoordinates), std::move(tangents));
    if (mesh.mMaterialIndex < m_materials.size()){
        geometry->setState(m_materials[mesh.mMaterialIndex]);
    }
    return geometry;
}

// Maps Assimp row indexing to GLM column indexing.
glm::mat4 ModelReader::convertMatrix(const aiMatrix4x4& matrix){
    glm::mat4 result(1);
    for (int row = 0; row < 4; ++row){
        for (int column = 0; column < 4; ++column){
            result[column][row] = matrix[row][column];
        }
    }
    return result;
}

// Builds the concrete graph node described by one XML element.
std::shared_ptr<Node> ModelReader::readNode(const aiNode& imported) const{
    auto node = std::make_shared<Transform>(imported.mName.C_Str());
    node->setMatrix(convertMatrix(imported.mTransformation));
    for (unsigned index = 0; index < imported.mNumMeshes; ++index){
        if (const auto& mesh = m_meshes.at(imported.mMeshes[index])){
            node->addChild(mesh);
        }
    }
    for (unsigned index = 0; index < imported.mNumChildren; ++index){
        node->addChild(readNode(*imported.mChildren[index]));
    }
    return node;
}
} // namespace vr
