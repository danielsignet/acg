#include <stdexcept>
#include <vr/Material.h>

namespace vr{
Material::Material(const Material& other){
    *this = other;
}

Material& Material::operator=(const Material& other){
    if (this == &other){
        return *this;
    }
    m_ambient = other.m_ambient;
    m_diffuse = other.m_diffuse;
    m_specular = other.m_specular;
    m_shininess = other.m_shininess;
    m_roughness = other.m_roughness;
    m_metallic = other.m_metallic;
    m_hasAmbient = other.m_hasAmbient;
    m_hasDiffuse = other.m_hasDiffuse;
    m_hasSpecular = other.m_hasSpecular;
    m_hasShininess = other.m_hasShininess;
    m_hasRoughness = other.m_hasRoughness;
    m_hasMetallic = other.m_hasMetallic;
    ++m_revision;
    m_resolutions.clear();
    return *this;
}

// Validates and overrides the specular highlight exponent.
void Material::setShininess(float value){
    if (!(value >= 1 && value <= 1024)){
        throw std::invalid_argument("Shininess must be in [1,1024]");
    }
    if (m_hasShininess && m_shininess == value){
        return;
    }
    m_shininess = value;
    m_hasShininess = true;
    ++m_revision;
}

// Sets perceptual microfacet roughness in [0,1].
void Material::setRoughness(float value){
    if (!(value >= 0 && value <= 1)){
        throw std::invalid_argument("Roughness must be in [0,1]");
    }
    if (m_hasRoughness && m_roughness == value){
        return;
    }
    m_roughness = value;
    m_hasRoughness = true;
    ++m_revision;
}

// Sets the blend between dielectric and conductor behavior in [0,1].
void Material::setMetallic(float value){
    if (!(value >= 0 && value <= 1)){
        throw std::invalid_argument("Metallic must be in [0,1]");
    }
    if (m_hasMetallic && m_metallic == value){
        return;
    }
    m_metallic = value;
    m_hasMetallic = true;
    ++m_revision;
}

// Combines inherited values with explicitly supplied local overrides.
Material Material::merged(const Material& local) const{
    Material result = *this;
    result.m_resolutions.clear();
    if (local.m_hasAmbient){
        result.setAmbient(local.m_ambient);
    }
    if (local.m_hasDiffuse){
        result.setDiffuse(local.m_diffuse);
    }
    if (local.m_hasSpecular){
        result.setSpecular(local.m_specular);
    }
    if (local.m_hasShininess){
        result.setShininess(local.m_shininess);
    }
    if (local.m_hasRoughness){
        result.setRoughness(local.m_roughness);
    }
    if (local.m_hasMetallic){
        result.setMetallic(local.m_metallic);
    }
    return result;
}

std::shared_ptr<Material> Material::resolve(const std::shared_ptr<Material>& parent) const{
    for (const auto& cached : m_resolutions){
        if (cached.parent.lock() == parent && cached.parentRevision == parent->m_revision &&
            cached.localRevision == m_revision &&
            cached.outputRevision == cached.output->m_revision){
            return cached.output;
        }
    }
    auto output = std::make_shared<Material>(parent->merged(*this));
    if (m_resolutions.size() == 8){
        m_resolutions.clear();
    }
    m_resolutions.push_back({parent, parent->m_revision, m_revision, output->m_revision, output});
    return output;
}

bool Material::sameValues(const Material& other) const{
    return m_ambient == other.m_ambient && m_diffuse == other.m_diffuse &&
           m_specular == other.m_specular && m_shininess == other.m_shininess &&
           m_roughness == other.m_roughness && m_metallic == other.m_metallic;
}

// Uploads the material colors and shininess to the shader.
void Material::apply(const Program& program) const{
    program.set("uMaterial.ambient", m_ambient);
    program.set("uMaterial.diffuse", m_diffuse);
    program.set("uMaterial.specular", m_specular);
    program.set("uMaterial.shininess", m_shininess);
    program.set("uMaterial.roughness", m_roughness);
    program.set("uMaterial.metallic", m_metallic);
}
} // namespace vr
