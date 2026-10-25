#pragma once
#include <rapidxml-1.13/rapidxml.hpp>
#include <vr/Reader.h>

namespace vr{
class SceneReader final{
public:
    // Uses the resource reader to construct one XML scene.
    explicit SceneReader(Reader& reader) : m_reader(reader){
    }

    // Parses one XML document into a complete scene.
    std::shared_ptr<Scene> read(const std::filesystem::path& path);

private:
    using Xml = rapidxml::xml_node<char>;

    // Builds the concrete graph node described by one XML element.
    std::shared_ptr<Node> readNode(Xml* xml);

    // Reads light colors, position, attenuation and optional rendering state.
    std::shared_ptr<Light> readLight(Xml* xml, const std::string& name);

    // Builds all XML detail levels and validates their distance ordering.
    std::shared_ptr<LOD> readLod(Xml* xml, const std::string& name);

    // Combines local translation, rotation and scale before attaching animation.
    std::shared_ptr<Transform> readTransform(Xml* xml, const std::string& name);

    // Loads and registers model assets before reading their scene instances.
    void readAssets(Xml* root);

    // Reads the initial camera position and target from XML.
    void readCamera(Xml* root);
    void readVisibility(Xml* root);

    // Attaches local state and recursively builds the ordered child nodes.
    void readChildren(Xml* xml, Group& group);

    // Builds local state from its explicitly supplied XML properties.
    std::shared_ptr<State> readState(Xml* xml);

    // Reads only the material properties provided by the source.
    void readMaterial(Xml* xml, State& state);

    // Resolves texture layers and explicit removal of inherited textures.
    void readTextures(Xml* xml, State& state);

    // Reads built-in and arbitrary typed shader-uniform overrides.
    void readUniforms(Xml* xml, State& state);

    // Resolves the optional vertex, fragment and geometry shader paths.
    void readProgram(Xml* xml, State& state);

    // Returns an XML attribute or the caller-supplied default.
    static std::string attribute(Xml* xml, const char* key, const std::string& fallback = {});

    // Rejects a missing or empty required XML attribute.
    static std::string required(Xml* xml, const char* key);

    // Parses exactly the requested number of finite numeric components.
    static std::vector<float> numbers(const std::string& value, std::size_t count);

    // Parses three finite numeric components into a vector.
    static glm::vec3 vec3(const std::string& value);

    // Parses one finite floating-point value.
    static float number(const std::string& value);

    // Parses the documented boolean spellings and rejects other values.
    static bool boolean(const std::string& value);

    // Normalizes XML resource-path separators across platforms.
    static std::filesystem::path portablePath(std::string path);
    Reader& m_reader;
    std::shared_ptr<Scene> m_scene;
    unsigned m_anonymousNodeCount = 0;
};
} // namespace vr
