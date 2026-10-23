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
    return result;
}

// Uploads the material colors and shininess to the shader.
void Material::apply(const Program& program) const{
    program.set("uMaterial.ambient", m_ambient);
    program.set("uMaterial.diffuse", m_diffuse);
    program.set("uMaterial.specular", m_specular);
    program.set("uMaterial.shininess", m_shininess);
}
} // namespace vr
