#pragma once
#include "Program.h"
#include <glm/glm.hpp>

namespace vr{
class Material{
public:
    // Overrides the ambient contribution while preserving other material properties.
    void setAmbient(const glm::vec3& v){
        m_ambient = v;
        m_hasAmbient = true;
    }

    // Overrides the diffuse contribution while preserving other material properties.
    void setDiffuse(const glm::vec3& v){
        m_diffuse = v;
        m_hasDiffuse = true;
    }

    // Overrides the specular contribution while preserving other material properties.
    void setSpecular(const glm::vec3& v){
        m_specular = v;
        m_hasSpecular = true;
    }

    // Validates and overrides the specular highlight exponent.
    void setShininess(float value);
    // Sets perceptual microfacet roughness in [0,1].
    void setRoughness(float value);
    // Sets the blend between dielectric and conductor behavior in [0,1].
    void setMetallic(float value);

    // Returns perceptual roughness, retaining an explicit zero for polished materials.
    float getRoughness() const{
        return m_roughness;
    }

    // Returns the conductor fraction of this material.
    float getMetallic() const{
        return m_metallic;
    }

    // Returns the ambient color contribution.
    const glm::vec3& getAmbient() const{
        return m_ambient;
    }

    // Returns the diffuse color contribution.
    const glm::vec3& getDiffuse() const{
        return m_diffuse;
    }

    // Returns the specular highlight color.
    const glm::vec3& getSpecular() const{
        return m_specular;
    }

    // Returns the specular highlight exponent.
    float getShininess() const{
        return m_shininess;
    }

    // Combines inherited values with explicitly supplied local overrides.
    Material merged(const Material& local) const;

    // Uploads the material colors and shininess to the shader.
    void apply(const Program& program) const;

private:
    glm::vec3 m_ambient{0.13f};
    glm::vec3 m_diffuse{0.72f};
    glm::vec3 m_specular{0.45f};
    float m_shininess = 48.0f;
    float m_roughness = 0.35f, m_metallic = 0;
    bool m_hasAmbient = false;
    bool m_hasDiffuse = false;
    bool m_hasSpecular = false;
    bool m_hasShininess = false;
    bool m_hasRoughness = false, m_hasMetallic = false;
};
} // namespace vr
