#pragma once
#include "Program.h"
#include <cstdint>
#include <glm/glm.hpp>
#include <memory>
#include <vector>

namespace vr{
class Material{
public:
    Material() = default;
    Material(const Material& other);
    Material& operator=(const Material& other);

    // Overrides the ambient contribution while preserving other material properties.
    void setAmbient(const glm::vec3& v){
        if (m_hasAmbient && m_ambient == v){
            return;
        }
        m_ambient = v;
        m_hasAmbient = true;
        ++m_revision;
    }

    // Overrides the diffuse contribution while preserving other material properties.
    void setDiffuse(const glm::vec3& v){
        if (m_hasDiffuse && m_diffuse == v){
            return;
        }
        m_diffuse = v;
        m_hasDiffuse = true;
        ++m_revision;
    }

    // Overrides the specular contribution while preserving other material properties.
    void setSpecular(const glm::vec3& v){
        if (m_hasSpecular && m_specular == v){
            return;
        }
        m_specular = v;
        m_hasSpecular = true;
        ++m_revision;
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
    // Reuses inherited material resolution, invalidating on edits to either input or output.
    std::shared_ptr<Material> resolve(const std::shared_ptr<Material>& parent) const;
    bool sameValues(const Material& other) const;

    std::uint64_t getRevision() const{
        return m_revision;
    }

    // Uploads the material colors and shininess to the shader.
    void apply(const Program& program) const;

private:
    struct Resolution{
        std::weak_ptr<Material> parent;
        std::size_t parentRevision, localRevision, outputRevision;
        std::shared_ptr<Material> output;
    };

    std::size_t m_revision = 0;
    mutable std::vector<Resolution> m_resolutions;
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
