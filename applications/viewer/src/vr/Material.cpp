#include <stdexcept>
#include <vr/Material.h>

namespace vr{
// Validates and overrides the specular highlight exponent.
void Material::setShininess(float value){
    if (!(value >= 1 && value <= 1024)){
        throw std::invalid_argument("Shininess must be in [1,1024]");
    }
    m_shininess = value;
    m_hasShininess = true;
}

// Sets perceptual microfacet roughness in [0,1].
void Material::setRoughness(float value){
    if (!(value >= 0 && value <= 1)){
        throw std::invalid_argument("Roughness must be in [0,1]");
    }
    m_roughness = value;
    m_hasRoughness = true;
}

// Sets the blend between dielectric and conductor behavior in [0,1].
void Material::setMetallic(float value){
    if (!(value >= 0 && value <= 1)){
        throw std::invalid_argument("Metallic must be in [0,1]");
    }
    m_metallic = value;
    m_hasMetallic = true;
}

// Combines inherited values with explicitly supplied local overrides.
Material Material::merged(const Material& local) const{
    Material result = *this;
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
