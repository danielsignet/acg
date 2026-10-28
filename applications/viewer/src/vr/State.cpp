#include <functional>
#include <stdexcept>
#include <vr/GLState.h>
#include <vr/State.h>

namespace vr{
// Creates the complete initial rendering state for the supplied shader program.
State State::defaults(const std::shared_ptr<Program>& program){
    State s;
    s.setProgram(program);
    s.setMaterial(std::make_shared<Material>());
    s.setLighting(true);
    s.setCullFace(true);
    s.setBlend(false);
    s.setCastShadows(true);
    s.setReceiveShadows(true);
    s.setUniform("uShadows", false);
    s.setUniform("uShadowPCF", true);
    s.setUniform("uOpacity", 1.0f);
    s.setUniform("uLodRange", glm::vec2(0, 1));
    s.setUniform("uFog", true);
    s.setUniform("uTint", glm::vec3(1));
    s.setUniform("uEmissive", glm::vec3(0));
    s.setUniform("uEmissionSwitchable", false);
    s.setUniform("uEmissionEnabled", true);
    s.setUniform("uToon", false);
    s.setUniform("uProcedural", 0);
    s.setUniform("uLinearOutput", false);
    s.setUniform("uAlphaCutoff", 0.0f);
    s.setUniform("uTextureMix", 0.3f);
    s.setUniform("uMultiTexture", true);
    s.setUniform("uNormalMapping", true);
    s.setUniform("uNormalScale", 1.0f);
    s.setUniform("uNormalY", 1.0f);
    s.setUniform("uBumpMapping", true);
    s.setUniform("uHeightScale", 0.03f);
    s.setUniform("uEnvironmentMaterial", false);
    s.setUniform("uReflectionProbe", 0);
    s.setUniform("uMirror", false);
    s.setUniform("uGlass", false);
    s.setUniform("uIor", 1.5f);
    return s;
}

// Combines inherited values with explicitly supplied local overrides.
State State::mergedShadow(const State& local) const{
    State result = *this;
    if (local.m_castShadows){
        result.m_castShadows = local.m_castShadows;
    }
    if (auto texture = local.m_textures.find(0); texture != local.m_textures.end()){
        result.m_textures[0] = texture->second;
    }
    for (const auto* name :{"uAlphaCutoff", "uLodRange"}){
        if (auto uniform = local.m_uniforms.find(name); uniform != local.m_uniforms.end()){
            result.m_uniforms[name] = uniform->second;
        }
    }
    return result;
}

State State::merged(const State& local) const{
    State result = *this;
    if (local.m_lightingEnabled){
        result.m_lightingEnabled = local.m_lightingEnabled;
    }
    if (local.m_cullFace){
        result.m_cullFace = local.m_cullFace;
    }
    if (local.m_blend){
        result.m_blend = local.m_blend;
    }
    if (local.m_castShadows){
        result.m_castShadows = local.m_castShadows;
    }
    if (local.m_receiveShadows){
        result.m_receiveShadows = local.m_receiveShadows;
    }
    for (unsigned i = 0; i < m_shadowMaps.size(); ++i){
        if (local.m_shadowMaps[i]){
            result.m_shadowMaps[i] = local.m_shadowMaps[i];
        }
    }
    // Unspecified material properties retain their inherited values.
    if (local.m_material){
        result.m_material = m_material ? local.m_material->resolve(m_material) : local.m_material;
    }
    if (local.m_program){
        result.m_program = local.m_program;
    }
    for (unsigned i = 0; i < MaxLights; ++i){
        if (local.m_lightEnabled[i]){
            result.m_lightEnabled[i] = local.m_lightEnabled[i];
        }
    }
    for (const auto& entry : local.m_textures){
        result.m_textures[entry.first] = entry.second;
    }
    if (local.m_normalMap){
        result.m_normalMap = local.m_normalMap;
    }
    if (local.m_heightMap){
        result.m_heightMap = local.m_heightMap;
    }
    for (const auto& entry : local.m_uniforms){
        result.m_uniforms[entry.first] = entry.second;
    }
    return result;
}

bool State::sameRenderState(const State& other) const{
    const bool material =
        m_material == other.m_material ||
        (m_material && other.m_material && m_material->sameValues(*other.m_material));
    return material && m_program == other.m_program && m_textures == other.m_textures &&
           m_normalMap == other.m_normalMap && m_heightMap == other.m_heightMap &&
           m_uniforms == other.m_uniforms && m_shadowMaps == other.m_shadowMaps &&
           m_lightEnabled == other.m_lightEnabled && m_lightingEnabled == other.m_lightingEnabled &&
           m_cullFace == other.m_cullFace && m_blend == other.m_blend &&
           m_castShadows == other.m_castShadows && m_receiveShadows == other.m_receiveShadows;
}

std::size_t State::renderKey() const{
    std::size_t key = 0;
    const auto add = [&](std::size_t value){
        key ^= value + 0x9e3779b9 + (key << 6) + (key >> 2);
    };
    const auto scalar = [&](float value){ add(std::hash<float>{}(value)); };
    add(std::hash<const void*>{}(m_program.get()));
    if (m_material){
        for (const auto& color :
             {m_material->getAmbient(), m_material->getDiffuse(), m_material->getSpecular()}){
            for (int i = 0; i < 3; ++i){
                scalar(color[i]);
            }
        }
        scalar(m_material->getShininess());
        scalar(m_material->getRoughness());
        scalar(m_material->getMetallic());
    }
    for (const auto& entry : m_textures){
        add(entry.first);
        add(std::hash<const void*>{}(entry.second.get()));
    }
    add(std::hash<const void*>{}(getNormalMap().get()));
    add(std::hash<const void*>{}(getHeightMap().get()));
    // Keep ordering stable when lights allocate auxiliary targets or pass uniforms change.
    // Exact compatibility (including every uniform and shadow texture) is checked separately.
    return key;
}

// Overrides one validated light slot independently of global lighting.
void State::setLightEnabled(unsigned index, bool enabled){
    if (index >= MaxLights){
        throw std::out_of_range("Light index");
    }
    m_lightEnabled[index] = enabled;
}

// Returns the enabled state of one validated light slot.
bool State::getLightEnabled(unsigned index) const{
    if (index >= MaxLights){
        throw std::out_of_range("Light index");
    }
    return m_lightEnabled[index].value_or(true);
}

// Overrides a texture layer; an empty pointer explicitly disables that layer.
void State::setTexture(unsigned layer, std::shared_ptr<Texture> texture){
    if (layer >= MaxTextures){
        throw std::out_of_range("Texture unit");
    }
    m_textures[layer] = std::move(texture);
}

// Overrides the tangent-space normal texture; null explicitly removes an inherited map.
void State::setNormalMap(std::shared_ptr<Texture> texture){
    if (texture && (texture->isDepth() || texture->getTarget() != GL_TEXTURE_2D)){
        throw std::invalid_argument("Normal map must be a 2D color texture");
    }
    m_normalMap = std::move(texture);
}

// Overrides a scalar height texture; null removes an inherited bump map.
void State::setHeightMap(std::shared_ptr<Texture> texture){
    if (texture && (texture->isDepth() || texture->getTarget() != GL_TEXTURE_2D)){
        throw std::invalid_argument("Height map must be a 2D color texture");
    }
    m_heightMap = std::move(texture);
}

// Assigns a validated 2D or cubemap depth texture outside the material texture slots.
void State::setShadowMap(unsigned index, std::shared_ptr<Texture> texture){
    if (index >= MaxShadowMaps){
        throw std::out_of_range("Shadow map index");
    }
    if (texture && (!texture->isDepth() ||
                    texture->getTarget() !=
                        static_cast<GLenum>(index == 0 ? GL_TEXTURE_2D : GL_TEXTURE_CUBE_MAP))){
        throw std::invalid_argument("Shadow map target");
    }
    m_shadowMaps[index] = std::move(texture);
}

// Returns the texture assigned to one layer, if any.
std::shared_ptr<Texture> State::getTexture(unsigned layer) const{
    const auto i = m_textures.find(layer);
    return i == m_textures.end() ? nullptr : i->second;
}

// Applies resolved shader, material, texture and rasterization state.
void State::apply() const{
    if (!m_program || !m_material){
        throw std::logic_error("Incomplete resolved rendering state");
    }
    m_program->use();
    if (getBlend()){
        GLState::enable(GL_BLEND);
        GLState::blendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        GLState::depthMask(GL_FALSE);
    } else{
        GLState::disable(GL_BLEND);
        GLState::depthMask(GL_TRUE);
    }
    if (getCullFace()){
        GLState::enable(GL_CULL_FACE);
    } else{
        GLState::disable(GL_CULL_FACE);
    }
    m_program->set("uLighting", getLighting());
    m_program->set("uReceiveShadows", getReceiveShadows());
    m_material->apply(*m_program);
    for (unsigned i = 0; i < MaxTextures; ++i){
        const auto texture = getTexture(i);
        if (texture){
            texture->apply(i);
        } else{
            glActiveTexture(GL_TEXTURE0 + i);
            glBindTexture(GL_TEXTURE_2D, 0);
        }
        m_program->set("uTextures[" + std::to_string(i) + "]", static_cast<int>(i));
        m_program->set("uHasTexture[" + std::to_string(i) + "]", static_cast<bool>(texture));
    }
    const auto bump = m_uniforms.find("uBumpMapping");
    const bool useHeight =
        getHeightMap() && (bump == m_uniforms.end() || std::get<bool>(bump->second));
    // Height and RGB normal maps share a sampler to retain the 16-unit GL 4.1 budget.
    const auto surfaceMap = useHeight ? getHeightMap() : getNormalMap();
    if (surfaceMap){
        surfaceMap->apply(MaxTextures);
    } else{
        glActiveTexture(GL_TEXTURE0 + MaxTextures);
        glBindTexture(GL_TEXTURE_2D, 0);
    }
    m_program->set("uSurfaceMap", static_cast<int>(MaxTextures));
    m_program->set("uHasNormalMap", static_cast<bool>(surfaceMap) && !useHeight);
    m_program->set("uHasHeightMap", useHeight);
    for (const auto& value : m_uniforms){
        m_program->set(value.first, value.second);
    }
    applyShadowMaps(*m_program);
}

// Binds shadow maps after any caller-reserved G-buffer texture units.
void State::applyShadowMaps(const Program& program, unsigned firstUnit) const{
    for (unsigned i = 0; i < MaxShadowMaps; ++i){
        if (m_shadowMaps[i]){
            m_shadowMaps[i]->apply(firstUnit + i);
        } else{
            glActiveTexture(GL_TEXTURE0 + firstUnit + i);
            glBindTexture(i == 0 ? GL_TEXTURE_2D : GL_TEXTURE_CUBE_MAP, 0);
        }
    }
    program.set("uSunShadow", static_cast<int>(firstUnit));
    for (unsigned i = 0; i < MaxLights; ++i){
        program.set("uPointShadows[" + std::to_string(i) + "]",
                    static_cast<int>(firstUnit + 1 + i));
    }
}
} // namespace vr
