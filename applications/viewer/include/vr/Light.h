#pragma once
#include "Node.h"
#include "NodeVisitor.h"

namespace vr{
class Light : public Node{
public:
    using Node::Node;
    bool containsLights() const override{ return true; }

    // Dispatches this concrete node type to the visitor.
    void accept(NodeVisitor& visitor) override{
        visitor.apply(*this);
    }

    // Sets the local light position or directional vector using its w component.
    void setPosition(const glm::vec4& v){
        m_position = v;
    }

    // Overrides the ambient contribution while preserving other material properties.
    void setAmbient(const glm::vec3& v){
        m_ambient = v;
    }

    // Overrides the diffuse contribution while preserving other material properties.
    void setDiffuse(const glm::vec3& v){
        m_diffuse = v;
    }

    // Overrides the specular contribution while preserving other material properties.
    void setSpecular(const glm::vec3& v){
        m_specular = v;
    }

    // Sets the constant, linear and quadratic distance attenuation factors.
    void setAttenuation(const glm::vec3& v){
        m_attenuation = v;
    }

    // Configures a normalized spotlight direction and its inner/outer cone angles.
    void setSpot(const glm::vec3& direction, float innerAngle, float outerAngle);

    // Sets a finite lighting range; zero leaves ordinary lights unbounded.
    void setRange(float distance);

    // Reports whether this positional light has a restricted beam.
    bool getSpot() const{
        return m_spot;
    }

    // Returns the local direction of the spotlight beam.
    const glm::vec3& getSpotDirection() const{
        return m_spotDirection;
    }

    // Returns the outer half-angle in degrees for the shadow volume.
    float getOuterAngle() const{
        return m_outerAngle;
    }

    // Returns the finite lighting range, or zero for an unbounded light.
    float getRange() const{
        return m_range;
    }

    // Enables or pauses this effect without discarding its current state.
    void setEnabled(bool enabled){
        m_enabled = enabled;
    }

    // Returns the current position in the coordinate system used by this object.
    const glm::vec4& getPosition() const{
        return m_position;
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

    // Returns the constant, linear and quadratic attenuation factors.
    const glm::vec3& getAttenuation() const{
        return m_attenuation;
    }

    // Reports whether this effect is currently enabled.
    bool getEnabled() const{
        return m_enabled;
    }

    // Uploads the transformed light and its enabled state to the shader.
    void apply(const Program& program, unsigned index, const glm::mat4& model, bool enabled) const;

private:
    glm::vec4 m_position{0, 5, 0, 1};
    glm::vec3 m_ambient{0.08f};
    glm::vec3 m_diffuse{1};
    glm::vec3 m_specular{1};
    glm::vec3 m_attenuation{1, 0.025f, 0.006f};
    glm::vec3 m_spotDirection{0, 0, -1};
    float m_innerAngle = 14, m_outerAngle = 20, m_range = 0;
    bool m_spot = false;
    bool m_enabled = true;
};
} // namespace vr
