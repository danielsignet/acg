#pragma once
#include "Material.h"
#include "Program.h"
#include "Texture.h"
#include <array>
#include <map>
#include <memory>
#include <optional>
#include <string>

namespace vr{
class State{
public:
    static constexpr unsigned MaxTextures = 4;
    static constexpr unsigned MaxLights = 8;
    static constexpr unsigned MaxShadowMaps = MaxLights + 1;

    // Creates the complete initial rendering state for the supplied shader program.
    static State defaults(const std::shared_ptr<Program>& program);

    // Combines inherited values with explicitly supplied local overrides.
    State merged(const State& local) const;

    // Applies resolved shader, material, texture and rasterization state.
    void apply() const;

    // Overrides global shader lighting for this rendering scope.
    void setLighting(bool enabled){
        m_lightingEnabled = enabled;
    }

    // Overrides back-face culling for this rendering scope.
    void setCullFace(bool enabled){
        m_cullFace = enabled;
    }

    // Overrides alpha blending and depth-write behavior for this rendering scope.
    void setBlend(bool enabled){
        m_blend = enabled;
    }

    // Returns the resolved alpha-blending setting.
    bool getBlend() const{
        return m_blend.value_or(false);
    }

    // Overrides whether this subtree contributes geometry to light depth passes.
    void setCastShadows(bool enabled){
        m_castShadows = enabled;
    }

    // Overrides whether the lighting shader samples shadow maps on this subtree.
    void setReceiveShadows(bool enabled){
        m_receiveShadows = enabled;
    }

    // Returns the resolved shadow-caster flag, enabled by default.
    bool getCastShadows() const{
        return m_castShadows.value_or(true);
    }

    // Returns the resolved shadow-receiver flag, enabled by default.
    bool getReceiveShadows() const{
        return m_receiveShadows.value_or(true);
    }

    // Assigns a validated 2D or cubemap depth texture outside the material texture slots.
    void setShadowMap(unsigned index, std::shared_ptr<Texture> texture);

    // Overrides one validated light slot independently of global lighting.
    void setLightEnabled(unsigned index, bool enabled);

    // Attaches local material property overrides.
    void setMaterial(std::shared_ptr<Material> value){
        m_material = std::move(value);
    }

    // Overrides the shader program inherited by this scope.
    void setProgram(std::shared_ptr<Program> value){
        m_program = std::move(value);
    }

    // Overrides a texture layer; an empty pointer explicitly disables that layer.
    void setTexture(unsigned layer, std::shared_ptr<Texture> texture);

    // Stores one typed shader-uniform override by name.
    void setUniform(const std::string& name, Uniform value){
        m_uniforms[name] = std::move(value);
    }

    // Returns the global lighting setting resolved for this scope.
    bool getLighting() const{
        return m_lightingEnabled.value_or(true);
    }

    // Returns the back-face culling setting resolved for this scope.
    bool getCullFace() const{
        return m_cullFace.value_or(true);
    }

    // Returns the enabled state of one validated light slot.
    bool getLightEnabled(unsigned index) const;

    // Returns the shader program associated with this rendering state.
    const std::shared_ptr<Program>& getProgram() const{
        return m_program;
    }

    // Returns the material associated with this rendering state.
    const std::shared_ptr<Material>& getMaterial() const{
        return m_material;
    }

    // Returns the texture assigned to one layer, if any.
    std::shared_ptr<Texture> getTexture(unsigned layer) const;

    // Returns the named uniform overrides associated with this state.
    const std::map<std::string, Uniform>& getUniforms() const{
        return m_uniforms;
    }

private:
    std::optional<bool> m_lightingEnabled, m_cullFace, m_blend;
    std::optional<bool> m_castShadows, m_receiveShadows;
    std::array<std::shared_ptr<Texture>, MaxShadowMaps> m_shadowMaps;
    std::array<std::optional<bool>, MaxLights> m_lightEnabled;
    std::shared_ptr<Material> m_material;
    std::shared_ptr<Program> m_program;
    std::map<unsigned, std::shared_ptr<Texture>> m_textures;
    std::map<std::string, Uniform> m_uniforms;
};
} // namespace vr
