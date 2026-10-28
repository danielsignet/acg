#pragma once
#include "Camera.h"
#include "ColorTarget.h"
#include "GBuffer.h"
#include "Program.h"
#include "ScreenQuad.h"
#include "RenderMode.h"
#include <array>

namespace vr{
class EnvironmentMap final{
public:
    // Loads one HDRI and bakes reusable GGX reflections, diffuse SH and the BRDF lookup.
    EnvironmentMap(const std::filesystem::path& root, const std::filesystem::path& image);
    // Publishes the environment to a forward program using the two remaining sampler units.
    void apply(const Program& program, unsigned firstUnit = 14) const;
    // Draws an HDR or display-encoded sky before forward scene geometry.
    void drawSky(const Camera& camera, bool linearOutput);
    // Adds the sky and surface IBL to the currently bound deferred lighting target.
    void drawDeferred(const Camera& camera, const GBuffer& buffer,
                      const std::shared_ptr<Texture>& ambient, int brdfMode);

    // Selects reflected radiance while preserving sky rendering and diffuse irradiance.
    void setReflectionMode(ReflectionMode mode){
        m_mode = mode;
    }

    // Publishes only complete local cubemaps; layer zero always retains the HDRI.
    void setLocalReflections(std::shared_ptr<Texture> maps, unsigned readyMask){
        m_localMaps = std::move(maps);
        m_readyMask = readyMask;
    }

    // Copies the static environment into layer zero of a newly allocated local array.
    void initializeLocalReflections(const std::shared_ptr<Texture>& maps);
    // Prefilters a complete scene capture into one local reflection layer.
    void prefilterLocal(const std::shared_ptr<Texture>& source,
                        const std::shared_ptr<Texture>& maps, unsigned layer);

    // Returns the HDRI's brightest upper-hemisphere direction for the directional light.
    const glm::vec3& getSunDirection() const{
        return m_sunDirection;
    }

private:
    // Projects diffuse lighting and normalizes exposure before the temporary HDR upload.
    void projectLighting(float* pixels, int width, int height);
    std::shared_ptr<Texture> m_radiance, m_prefiltered;
    std::shared_ptr<Texture> m_localMaps;
    std::unique_ptr<ColorTarget> m_brdf;
    std::shared_ptr<Program> m_skyProgram, m_deferredProgram, m_multisampleProgram;
    std::shared_ptr<Program> m_bakeProgram;
    ReflectionMode m_mode = ReflectionMode::Environment;
    unsigned m_readyMask = 1;
    std::array<glm::vec3, 9> m_irradiance{};
    glm::vec3 m_sunDirection{0, 1, 0};
    ScreenQuad m_quad;
};
} // namespace vr
