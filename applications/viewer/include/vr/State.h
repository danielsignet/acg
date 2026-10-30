#pragma once
#include "Material.h"
#include "Program.h"
#include "Revision.h"
#include "Texture.h"
#include <array>
#include <map>
#include <memory>
#include <optional>
#include <string>

namespace vr{
class State{
public:
    using Uniforms = std::map<std::string, Uniform>;

    std::uint64_t getRevision() const{
        return m_revision;
    }

    std::uint64_t getMaterialRevision() const{
        return m_material ? m_material->getRevision() : 0;
    }
    // Depth collection depends only on caster eligibility, albedo and alpha/LOD overrides.
    std::uint64_t getShadowRevision() const{ return m_shadowRevision; }

    static constexpr unsigned MaxTextures = 4;
    static constexpr unsigned MaterialTextureUnits = MaxTextures + 1;
    // The full light mask must remain exactly representable in the RGBA16F G-buffer.
    static constexpr unsigned MaxLights = 10;
    // Keep shadow samplers within units 5-13; environment maps use 14-15.
    static constexpr unsigned MaxPointShadows = 8;
    static constexpr unsigned MaxShadowMaps = MaxPointShadows + 1;

    // Creates the complete initial rendering state for the supplied shader program.
    static State defaults(const std::shared_ptr<Program>& program);

    // Combines inherited values with explicitly supplied local overrides.
    State merged(const State& local) const;
    // Depth passes inherit only caster eligibility, alpha cutouts and LOD coverage.
    State mergedShadow(const State& local) const;
    // Receiver collection needs lighting/masks, without resolving shared color materials.
    State mergedReceiver(const State& local) const;
    // Resolved-state compatibility for opaque instancing; uniforms include LOD and light masks.
    bool sameRenderState(const State& other) const;
    std::size_t renderKey() const;

    // Applies resolved shader, material, texture and rasterization state.
    void apply() const;

    // Binds shadow maps after any caller-reserved G-buffer texture units.
    void applyShadowMaps(const Program& program, unsigned firstUnit = MaterialTextureUnits) const;

    // Overrides global shader lighting for this rendering scope.
    void setLighting(bool enabled){
        if (m_lightingEnabled == enabled){
            return;
        }
        m_lightingEnabled = enabled;
        changed();
    }

    // Overrides back-face culling for this rendering scope.
    void setCullFace(bool enabled){
        if (m_cullFace == enabled){
            return;
        }
        m_cullFace = enabled;
        changed();
    }

    // Overrides alpha blending and depth-write behavior for this rendering scope.
    void setBlend(bool enabled){
        if (m_blend == enabled){
            return;
        }
        m_blend = enabled;
        changed();
    }

    // Returns the resolved alpha-blending setting.
    bool getBlend() const{
        return m_blend.value_or(false);
    }

    // Overrides whether this subtree contributes geometry to light depth passes.
    void setCastShadows(bool enabled){
        if (m_castShadows == enabled){
            return;
        }
        m_castShadows = enabled;
        m_shadowRevision=nextRevision();
        changed();
    }

    // Overrides whether the lighting shader samples shadow maps on this subtree.
    void setReceiveShadows(bool enabled){
        if (m_receiveShadows == enabled){
            return;
        }
        m_receiveShadows = enabled;
        changed();
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
        if (m_material == value){
            return;
        }
        m_material = std::move(value);
        changed();
    }

    // Overrides the shader program inherited by this scope.
    void setProgram(std::shared_ptr<Program> value){
        if (m_program == value){
            return;
        }
        m_program = std::move(value);
        changed();
    }

    // Overrides a texture layer; an empty pointer explicitly disables that layer.
    void setTexture(unsigned layer, std::shared_ptr<Texture> texture);

    // Overrides the tangent-space normal texture; null explicitly removes an inherited map.
    void setNormalMap(std::shared_ptr<Texture> texture);

    // Returns the normal texture independently of the color texture layers.
    std::shared_ptr<Texture> getNormalMap() const{
        return m_normalMap.value_or(nullptr);
    }

    // Overrides a scalar height texture; null removes an inherited bump map.
    void setHeightMap(std::shared_ptr<Texture> texture);

    // Returns the height texture independently of RGB normal and color textures.
    std::shared_ptr<Texture> getHeightMap() const{
        return m_heightMap.value_or(nullptr);
    }

    // Stores one typed shader-uniform override by name.
    void setUniform(const std::string& name, Uniform value){
        const auto found = m_uniforms->find(name);
        if (found != m_uniforms->end() && found->second == value){
            return;
        }
        if (m_uniforms.use_count() != 1){
            m_uniforms = std::make_shared<Uniforms>(*m_uniforms);
        }
        (*m_uniforms)[name] = std::move(value);
        if(name=="uAlphaCutoff" || name=="uLodRange") m_shadowRevision=nextRevision();
        changed();
    }

    // Live per-pass data is shared by cached states; material inheritance stays immutable.
    void setPassUniform(const std::string& name, Uniform value);

    const Uniforms& getPassUniforms() const{
        return m_passUniforms ? *m_passUniforms : *emptyUniforms();
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
        return *m_uniforms;
    }

private:
    static std::shared_ptr<Uniforms> emptyUniforms();

    void changed(){
        m_revision = nextRevision();
    }

    std::uint64_t m_revision = nextRevision();
    std::uint64_t m_shadowRevision = nextRevision();
    std::optional<bool> m_lightingEnabled, m_cullFace, m_blend;
    std::optional<bool> m_castShadows, m_receiveShadows;
    std::array<std::shared_ptr<Texture>, MaxShadowMaps> m_shadowMaps;
    std::array<std::optional<bool>, MaxLights> m_lightEnabled;
    std::shared_ptr<Material> m_material;
    std::shared_ptr<Program> m_program;
    std::map<unsigned, std::shared_ptr<Texture>> m_textures;
    std::optional<std::shared_ptr<Texture>> m_normalMap;
    std::optional<std::shared_ptr<Texture>> m_heightMap;
    std::shared_ptr<Uniforms> m_uniforms = emptyUniforms();
    std::shared_ptr<Uniforms> m_passUniforms;
};
} // namespace vr
