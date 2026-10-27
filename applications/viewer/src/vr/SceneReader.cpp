#include "SceneReader.h"
#include <vr/ModelInstance.h>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <glm/gtc/matrix_transform.hpp>
#include <sstream>
#include <stdexcept>
#include <vr/Animation.h>

namespace vr{
// Returns an XML attribute or the caller-supplied default.
std::string SceneReader::attribute(Xml* node, const char* key, const std::string& fallback){
    auto* xmlAttribute = node->first_attribute(key);
    return xmlAttribute ? xmlAttribute->value() : fallback;
}

// Rejects a missing or empty required XML attribute.
std::string SceneReader::required(Xml* node, const char* key){
    auto value = SceneReader::attribute(node, key);
    if (value.empty()){
        throw std::runtime_error(std::string("Missing XML attribute: ") + key);
    }
    return value;
}

// Parses exactly the requested number of finite numeric components.
std::vector<float> SceneReader::numbers(const std::string& value, std::size_t count){
    std::istringstream stream(value);
    std::vector<float> result(count);
    for (auto& component : result){
        if (!(stream >> component) || !std::isfinite(component)){
            throw std::runtime_error("Invalid numeric XML value: " + value);
        }
    }
    std::string extra;
    if (stream >> extra){
        throw std::runtime_error("Too many XML components: " + value);
    }
    return result;
}

// Parses three finite numeric components into a vector.
glm::vec3 SceneReader::vec3(const std::string& value){
    auto components = numbers(value, 3);
    return{components[0], components[1], components[2]};
}

// Parses one finite floating-point value.
float SceneReader::number(const std::string& value){
    return numbers(value, 1)[0];
}

// Parses the documented boolean spellings and rejects other values.
bool SceneReader::boolean(const std::string& value){
    if (value == "true" || value == "1"){
        return true;
    }
    if (value == "false" || value == "0"){
        return false;
    }
    throw std::runtime_error("Invalid XML boolean: " + value);
}

// Normalizes XML resource-path separators across platforms.
std::filesystem::path SceneReader::portablePath(std::string path){
    std::replace(path.begin(), path.end(), '\\', '/');
    return std::filesystem::path(path);
}

// Parses one XML document into a complete scene.
std::shared_ptr<Scene> SceneReader::read(const std::filesystem::path& path){

    std::ifstream input(path);
    std::string content{std::istreambuf_iterator<char>(input),{}};
    content.push_back('\0');
    rapidxml::xml_document<> doc;
    try{
        doc.parse<0>(content.data());
    } catch (const rapidxml::parse_error& e){
        throw std::runtime_error(std::string("XML: ") + e.what());
    }
    Xml* root = doc.first_node("scene");
    if (!root){
        throw std::runtime_error("Missing scene root");
    }
    m_scene = std::make_shared<Scene>();
    m_anonymousNodeCount = 0;
    readAssets(root);
    readCamera(root);
    readVisibility(root);

    for (auto* child = root->first_node(); child; child = child->next_sibling()){
        const std::string tag = child->name();
        if (tag == "assets" || tag == "camera" || tag == "visibility"){
            continue;
        }
        if (tag == "state"){
            m_scene->getRoot()->setState(readState(child));
        } else{
            m_scene->getRoot()->addChild(readNode(child));
        }
    }
    m_scene->getCamera()->setLookAt(m_scene->getInitialEye(), m_scene->getInitialTarget());
    m_scene->getCamera()->setPerspective(48, 1.6f, 0.1f, 150);
    return m_scene;
}

// Reads finite cells, the exterior region and explicit rectangular openings.
void SceneReader::readVisibility(Xml* root){
    auto* layout = root->first_node("visibility");
    if (!layout){
        return;
    }
    std::vector<PortalCell> cells;
    std::vector<Doorway> doors;
    for (auto* item = layout->first_node(); item; item = item->next_sibling()){
        const BoundingBox bounds(vec3(required(item, "min")), vec3(required(item, "max")));
        const std::string tag = item->name();
        if (tag == "cell"){
            cells.push_back({required(item, "id"), bounds});
        } else if (tag == "portal"){
            doors.push_back({required(item, "from"), required(item, "to"), bounds});
        } else{
            throw std::runtime_error("Unknown visibility element: " + tag);
        }
    }
    m_scene->getPortalGraph().build(std::move(cells), doors, attribute(layout, "exterior"));
}

// Loads and registers model assets before reading their scene instances.
void SceneReader::readAssets(Xml* root){
    if (auto* assets = root->first_node("assets")){
        for (auto* asset = assets->first_node(); asset; asset = asset->next_sibling()){
            if (std::string(asset->name()) != "model"){
                throw std::runtime_error("Unknown asset element");
            }
            m_reader.registerNode(
                required(asset, "id"),
                m_reader.readModel(portablePath(required(asset, "file")),
                                   boolean(attribute(asset, "flatten", "false"))));
            m_modelAssets.insert(required(asset, "id"));
        }
    }
}

// Reads the initial camera position and target from XML.
void SceneReader::readCamera(Xml* root){
    if (auto* camera = root->first_node("camera")){
        m_scene->setInitialEye(vec3(attribute(camera, "eye", "16 11 22")));
        m_scene->setInitialTarget(vec3(attribute(camera, "target", "0 1 0")));
    }
}

// Builds the concrete graph node described by one XML element.
std::shared_ptr<Node> SceneReader::readNode(Xml* xml){

    const std::string tag = xml->name(),
                      name = attribute(xml, "name", tag + std::to_string(m_anonymousNodeCount++));
    if (tag == "instance"){
        if (xml->first_node()){
            throw std::runtime_error("Put instance overrides on a surrounding transform/group");
        }
        const auto asset = required(xml, "ref");
        auto instance = ModelInstance::create(
            attribute(xml, "name", asset), m_reader.getAsset(asset),
            boolean(attribute(xml, "selectable", m_modelAssets.count(asset) ? "true" : "false")));
        instance->setCullingEnabled(boolean(attribute(xml, "culling", "true")));
        return instance;
    }
    if (tag == "light"){
        return readLight(xml, name);
    }
    if (tag == "lod"){
        return readLod(xml, name);
    }
    std::shared_ptr<Group> group;
    if (tag == "transform"){
        group = readTransform(xml, name);
    } else if (tag == "group"){
        group = std::make_shared<Group>(name);
    } else{
        throw std::runtime_error("Unknown scene element: " + tag);
    }
    group->setRoomId(attribute(xml, "room"));
    group->setCullingEnabled(boolean(attribute(xml, "culling", "true")));
    readChildren(xml, *group);
    return group;
}

// Reads light colors, position, attenuation and optional rendering state.
std::shared_ptr<Light> SceneReader::readLight(Xml* xml, const std::string& name){

    auto light = std::make_shared<Light>(name);
    auto position = numbers(attribute(xml, "position", "0 4 0 1"), 4);
    if (position[3] != 0 && position[3] != 1){
        throw std::runtime_error("Light position w must be zero or one");
    }
    light->setPosition({position[0], position[1], position[2], position[3]});
    light->setAmbient(vec3(attribute(xml, "ambient", "0.035 0.035 0.035")));
    light->setDiffuse(vec3(attribute(xml, "diffuse", "1 1 1")));
    light->setSpecular(vec3(attribute(xml, "specular", "1 1 1")));
    light->setAttenuation(vec3(attribute(xml, "attenuation", "1 0.025 0.006")));
    if (auto* state = xml->first_node("state")){
        light->setState(readState(state));
    }
    m_scene->registerLight(light);
    return light;
}

// Builds all XML detail levels and validates their distance ordering.
std::shared_ptr<LOD> SceneReader::readLod(Xml* xml, const std::string& name){

    auto lod = std::make_shared<LOD>(name);
    lod->setFadeWidth(number(attribute(xml, "fadeWidth", "0")));
    if (xml->first_attribute("center")){
        lod->setCenter(vec3(attribute(xml, "center")));
    }
    if (auto* state = xml->first_node("state")){
        lod->setState(readState(state));
    }
    for (auto* level = xml->first_node(); level; level = level->next_sibling()){
        if (std::string(level->name()) == "state"){
            continue;
        }
        if (std::string(level->name()) != "level"){
            throw std::runtime_error("LOD expects level elements");
        }
        auto levelGroup = std::make_shared<Group>("LOD level");
        for (auto* child = level->first_node(); child; child = child->next_sibling()){
            levelGroup->addChild(readNode(child));
        }
        if (levelGroup->getChildren().empty()){
            throw std::runtime_error("Empty LOD level");
        }
        const auto maximum = required(level, "max");
        lod->addLevel(maximum == "inf" ? std::numeric_limits<float>::infinity() : number(maximum),
                      levelGroup);
    }
    if (lod->getRanges().empty()){
        throw std::runtime_error("LOD has no levels");
    }
    return lod;
}

// Combines local translation, rotation and scale before attaching animation.
std::shared_ptr<Transform> SceneReader::readTransform(Xml* xml, const std::string& name){

    auto transform = std::make_shared<Transform>(name);
    glm::mat4 matrix = glm::translate(glm::mat4(1), vec3(attribute(xml, "translate", "0 0 0")));
    const auto rotation = vec3(attribute(xml, "rotate", "0 0 0"));
    const auto scale = vec3(attribute(xml, "scale", "1 1 1"));
    if (glm::any(glm::lessThanEqual(glm::abs(scale), glm::vec3(1e-8f)))){
        throw std::runtime_error("Singular scale in XML");
    }
    matrix = glm::rotate(matrix, glm::radians(rotation.z), glm::vec3(0, 0, 1));
    matrix = glm::rotate(matrix, glm::radians(rotation.y), glm::vec3(0, 1, 0));
    matrix = glm::rotate(matrix, glm::radians(rotation.x), glm::vec3(1, 0, 0));
    matrix = glm::scale(matrix, scale);
    transform->setMatrix(matrix);
    if (attribute(xml, "animation") == "spin"){
        transform->setUpdateCallback(
            std::make_shared<SpinCallback>(transform, number(attribute(xml, "speed", "35")),
                                           vec3(attribute(xml, "axis", "0.3 1 0.2"))));
    }
    m_scene->registerTransform(transform);
    return transform;
}

// Attaches local state and recursively builds the ordered child nodes.
void SceneReader::readChildren(Xml* xml, Group& group){
    if (auto* state = xml->first_node("state")){
        group.setState(readState(state));
    }
    for (auto* child = xml->first_node(); child; child = child->next_sibling()){
        if (std::string(child->name()) != "state"){
            group.addChild(readNode(child));
        }
    }
}

// Builds local state from its explicitly supplied XML properties.
std::shared_ptr<State> SceneReader::readState(Xml* xml){

    auto state = std::make_shared<State>();
    if (xml->first_attribute("lighting")){
        state->setLighting(boolean(attribute(xml, "lighting")));
    }
    if (xml->first_attribute("cull")){
        state->setCullFace(boolean(attribute(xml, "cull")));
    }
    if (xml->first_attribute("blend")){
        state->setBlend(boolean(attribute(xml, "blend")));
    }
    if (xml->first_attribute("castShadows")){
        state->setCastShadows(boolean(attribute(xml, "castShadows")));
    }
    if (xml->first_attribute("receiveShadows")){
        state->setReceiveShadows(boolean(attribute(xml, "receiveShadows")));
    }
    readMaterial(xml, *state);
    readTextures(xml, *state);
    readUniforms(xml, *state);
    readProgram(xml, *state);
    return state;
}

// Reads only the material properties provided by the source.
void SceneReader::readMaterial(Xml* xml, State& state){
    if (xml->first_attribute("diffuse") || xml->first_attribute("ambient") ||
        xml->first_attribute("specular") || xml->first_attribute("shininess")){
        auto material = std::make_shared<Material>();
        if (xml->first_attribute("diffuse")){
            material->setDiffuse(vec3(attribute(xml, "diffuse")));
        }
        if (xml->first_attribute("ambient")){
            material->setAmbient(vec3(attribute(xml, "ambient")));
        }
        if (xml->first_attribute("specular")){
            material->setSpecular(vec3(attribute(xml, "specular")));
        }
        if (xml->first_attribute("shininess")){
            material->setShininess(number(attribute(xml, "shininess")));
        }
        state.setMaterial(material);
    }
}

// Resolves texture layers and explicit removal of inherited textures.
void SceneReader::readTextures(Xml* xml, State& state){
    if (xml->first_attribute("normalMap")){
        const auto path = attribute(xml, "normalMap");
        state.setNormalMap(path == "none" ? nullptr : m_reader.readTexture(portablePath(path)));
    }
    for (unsigned layer = 0; layer < State::MaxTextures; ++layer){
        const auto key = "texture" + std::to_string(layer);
        if (xml->first_attribute(key.c_str())){
            const auto path = attribute(xml, key.c_str());
            state.setTexture(layer,
                             path == "none" ? nullptr : m_reader.readTexture(portablePath(path)));
        }
    }
}

// Reads built-in and arbitrary typed shader-uniform overrides.
void SceneReader::readUniforms(Xml* xml, State& state){
    if (xml->first_attribute("normalScale")){
        state.setUniform("uNormalScale", number(attribute(xml, "normalScale")));
    }
    if (xml->first_attribute("opacity")){
        const auto opacity = number(attribute(xml, "opacity"));
        if (opacity < 0 || opacity > 1){
            throw std::runtime_error("Opacity must be 0..1");
        }
        state.setUniform("uOpacity", opacity);
    }
    if (xml->first_attribute("tint")){
        state.setUniform("uTint", vec3(attribute(xml, "tint")));
    }
    if (xml->first_attribute("emissive")){
        state.setUniform("uEmissive", vec3(attribute(xml, "emissive")));
    }
    if (xml->first_attribute("toon")){
        state.setUniform("uToon", boolean(attribute(xml, "toon")));
    }
    if (xml->first_attribute("alphaCutoff")){
        state.setUniform("uAlphaCutoff", number(attribute(xml, "alphaCutoff")));
    }
    if (xml->first_attribute("textureMix")){
        state.setUniform("uTextureMix", number(attribute(xml, "textureMix")));
    }

    for (auto* child = xml->first_node(); child; child = child->next_sibling()){
        if (std::string(child->name()) != "uniform"){
            throw std::runtime_error("Only uniform elements are allowed inside state");
        }
        const auto name = required(child, "name");
        const auto type = required(child, "type");
        const auto value = required(child, "value");
        if (type == "float"){
            state.setUniform(name, number(value));
        } else if (type == "int"){
            state.setUniform(name, std::stoi(value));
        } else if (type == "bool"){
            state.setUniform(name, boolean(value));
        } else if (type == "vec2"){
            const auto components = numbers(value, 2);
            state.setUniform(name, glm::vec2(components[0], components[1]));
        } else if (type == "vec3"){
            state.setUniform(name, vec3(value));
        } else if (type == "vec4"){
            const auto components = numbers(value, 4);
            state.setUniform(name,
                             glm::vec4(components[0], components[1], components[2], components[3]));
        } else{
            throw std::runtime_error("Unsupported XML uniform type: " + type);
        }
    }
}

// Resolves the optional vertex, fragment and geometry shader paths.
void SceneReader::readProgram(Xml* xml, State& state){
    if (xml->first_attribute("vertex")){
        state.setProgram(std::make_shared<Program>(
            m_reader.getRoot() / required(xml, "vertex"),
            m_reader.getRoot() / required(xml, "fragment"),
            attribute(xml, "geometry").empty() ? std::filesystem::path()
                                               : m_reader.getRoot() / attribute(xml, "geometry")));
    }
}

} // namespace vr
