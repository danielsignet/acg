#include <stdexcept>
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
    s.setUniform("uOpacity", 1.0f);
    s.setUniform("uFog", true);
    s.setUniform("uTint", glm::vec3(1));
    s.setUniform("uEmissive", glm::vec3(0));
    s.setUniform("uToon", false);
    s.setUniform("uAlphaCutoff", 0.0f);
    s.setUniform("uTextureMix", 0.3f);
    s.setUniform("uMultiTexture", true);
    return s;
}

// Combines inherited values with explicitly supplied local overrides.
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
    // Unspecified material properties retain their inherited values.
    if (local.m_material){
        result.m_material = m_material
                                ? std::make_shared<Material>(m_material->merged(*local.m_material))
                                : local.m_material;
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
    for (const auto& entry : local.m_uniforms){
        result.m_uniforms[entry.first] = entry.second;
    }
    return result;
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
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask(GL_FALSE);
    } else{
        glDisable(GL_BLEND);
        glDepthMask(GL_TRUE);
    }
    if (getCullFace()){
        glEnable(GL_CULL_FACE);
    } else{
        glDisable(GL_CULL_FACE);
    }
    m_program->set("uLighting", getLighting());
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
    for (const auto& value : m_uniforms){
        m_program->set(value.first, value.second);
    }
}
} // namespace vr
