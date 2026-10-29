#include <algorithm>
#include <cmath>
#include <glm/gtc/constants.hpp>
#include <stb_image.h>
#include <stdexcept>
#include <vr/Antialiasing.h>
#include <vr/EnvironmentMap.h>
#include <vr/FramebufferState.h>
#include <vr/GLState.h>

namespace vr{
namespace{
constexpr unsigned skySize=1024,skyLevels=11;
// Copies a complete cube layer on the GPU, retaining the individual mip levels.
void copyCube(const std::shared_ptr<Texture>& source,const std::shared_ptr<Texture>& target,
              unsigned sourceLayer,unsigned targetLayer,unsigned levels){
    FramebufferState saved;
    source->apply(1);target->apply(0);
    GLuint framebuffer=0;
    glGenFramebuffers(1,&framebuffer);
    GLState::bindFramebuffer(GL_READ_FRAMEBUFFER,framebuffer);
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    for (unsigned level=0;level<levels;++level){
        const int size=std::max(1,source->getWidth()>>level);
        for (unsigned face=0;face<6;++face){
            if (source->getTarget()==GL_TEXTURE_CUBE_MAP_ARRAY)
                glFramebufferTextureLayer(GL_READ_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,source->getId(),level,sourceLayer*6+face);
            else glFramebufferTexture2D(GL_READ_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,
                    GL_TEXTURE_CUBE_MAP_POSITIVE_X+face,source->getId(),level);
            glCopyTexSubImage3D(GL_TEXTURE_CUBE_MAP_ARRAY,level,0,0,targetLayer*6+face,0,0,size,size);
        }
    }
    GLState::deleteFramebuffers(1,&framebuffer);
}
}
// Projects diffuse lighting and normalizes exposure before the temporary HDR upload.
void EnvironmentMap::projectLighting(float* pixels, int width, int height, float requestedExposure){
    m_irradiance.fill(glm::vec3(0));
    double weightedLuminance = 0, totalWeight = 0;
    float brightest = 0;
    for (int y = 0; y < height; ++y){
        const float latitude = ((y + 0.5f) / height - 0.5f) * glm::pi<float>();
        const double weight = std::cos(latitude);
        for (int x = 0; x < width; ++x){
            const auto offset = (static_cast<std::size_t>(y) * width + x) * 3;
            const glm::vec3 color(pixels[offset], pixels[offset + 1], pixels[offset + 2]);
            if (!std::isfinite(color.x) || !std::isfinite(color.y) || !std::isfinite(color.z) ||
                glm::any(glm::lessThan(color, glm::vec3(0)))){
                throw std::runtime_error("HDRI contains invalid radiance");
            }
            const float luminance = glm::dot(color, glm::vec3(0.2126f, 0.7152f, 0.0722f));
            weightedLuminance += luminance * weight;
            totalWeight += weight;
            if (latitude > 0 && luminance > brightest){
                const float longitude = ((x + 0.5f) / width - 0.5f) * glm::two_pi<float>();
                m_sunDirection ={std::cos(latitude) * std::cos(longitude), std::sin(latitude),
                                  std::cos(latitude) * std::sin(longitude)};
                brightest = luminance;
            }
        }
    }
    const float exposure = requestedExposure>0 ? requestedExposure :
        static_cast<float>(0.35 * totalWeight / std::max(weightedLuminance, 1e-6));
    for (std::size_t i = 0; i < static_cast<std::size_t>(width) * height * 3; ++i){
        pixels[i] *= exposure;
    }
    constexpr int stride = 4;
    const float pixelArea =
        glm::two_pi<float>() * glm::pi<float>() * stride * stride / (width * height);
    for (int y = stride / 2; y < height; y += stride){
        const float latitude = ((y + 0.5f) / height - 0.5f) * glm::pi<float>();
        for (int x = stride / 2; x < width; x += stride){
            const float longitude = ((x + 0.5f) / width - 0.5f) * glm::two_pi<float>();
            const glm::vec3 n(std::cos(latitude) * std::cos(longitude), std::sin(latitude),
                              std::cos(latitude) * std::sin(longitude));
            const std::array<float, 9> basis{
                {0.282095f, 0.488603f * n.y, 0.488603f * n.z, 0.488603f * n.x,
                 1.092548f * n.x * n.y, 1.092548f * n.y * n.z, 0.315392f * (3 * n.z * n.z - 1),
                 1.092548f * n.x * n.z, 0.546274f * (n.x * n.x - n.y * n.y)}};
            const auto offset = (static_cast<std::size_t>(y) * width + x) * 3;
            const glm::vec3 color(pixels[offset], pixels[offset + 1], pixels[offset + 2]);
            for (unsigned i = 0; i < basis.size(); ++i){
                const float convolution = i == 0 ? 1.0f : i < 4 ? 2.0f / 3.0f : 0.25f;
                m_irradiance[i] +=
                    color * (basis[i] * std::cos(latitude) * pixelArea * convolution);
            }
        }
    }
}

// Loads one HDRI and bakes reusable GGX reflections, diffuse SH and the BRDF lookup.
EnvironmentMap::EnvironmentMap(const std::filesystem::path& root,
                               const std::filesystem::path& image, float exposure){
    if (!std::isfinite(exposure) || exposure<0) throw std::invalid_argument("Invalid sky exposure");
    const auto shaders = root / "shaders";
    m_skyProgram =
        std::make_shared<Program>(shaders / "screen-quad.vs", shaders / "environment-sky.fs");
    m_deferredProgram =
        std::make_shared<Program>(shaders / "screen-quad.vs", shaders / "environment-deferred.fs");
    m_multisampleProgram = std::make_shared<Program>(shaders / "screen-quad.vs",
                                                     shaders / "environment-deferred-msaa.fs");
    m_bakeProgram =
        std::make_shared<Program>(shaders / "screen-quad.vs", shaders / "environment-bake.fs");
    auto& bake = *m_bakeProgram;
    int width = 0, height = 0, channels = 0;
    stbi_set_flip_vertically_on_load(1);
    std::unique_ptr<float, decltype(&stbi_image_free)> pixels(
        stbi_loadf((root / image).string().c_str(), &width, &height, &channels, 3),
        stbi_image_free);
    if (!pixels){
        throw std::runtime_error("Cannot load sky HDRI: " + image.string());
    }
    projectLighting(pixels.get(), width, height, exposure);
    m_irradiances.push_back(m_irradiance);
    FramebufferState saved;
    Antialiasing::screenPass();
    GLState::enable(GL_TEXTURE_CUBE_MAP_SEAMLESS);
    GLuint source = 0, framebuffer = 0;
    glGenTextures(1, &source);
    glGenFramebuffers(1, &framebuffer);
    try{
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, source);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, width, height, 0, GL_RGB, GL_FLOAT, pixels.get());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        m_radiance = Texture::colorCube(skySize, skyLevels);
        m_prefiltered = Texture::colorCubeArray(256, 9, 1);
        m_brdf = std::make_unique<ColorTarget>(128, 128);
        m_radiance->apply(1);
        m_prefiltered->apply(1);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, source);
        bake.use();
        bake.set("uEquirectangular", 0);
        bake.set("uSourceCube", 1);
        bake.set("uSampleCount", 256);
        GLState::bindFramebuffer(GL_FRAMEBUFFER, framebuffer);
        glDrawBuffer(GL_COLOR_ATTACHMENT0);
        const auto faces = [&](const std::shared_ptr<Texture>& target, unsigned level, int size){
            GLState::viewport(0, 0, size, size);
            for (unsigned face = 0; face < 6; ++face){
                if (target->getTarget() == GL_TEXTURE_CUBE_MAP_ARRAY){
                    glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, target->getId(),
                                              level, face);
                } else{
                    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                           GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, target->getId(),
                                           level);
                }
                if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE){
                    throw std::runtime_error("Incomplete environment bake framebuffer");
                }
                bake.set("uFace", static_cast<int>(face));
                m_quad.draw();
            }
        };
        bake.set("uPass", 0);
        faces(m_radiance, 0, skySize);
        m_radiance->apply(1);
        glGenerateMipmap(GL_TEXTURE_CUBE_MAP);
        bake.set("uPass", 1);
        for (unsigned level = 0; level < 9; ++level){
            bake.set("uRoughness", static_cast<float>(level) / 8);
            faces(m_prefiltered, level, std::max(1, 256 >> level));
        }
        m_brdf->bind();
        bake.set("uPass", 2);
        m_quad.draw();
    } catch (...){
        GLState::deleteFramebuffers(1, &framebuffer);
        glDeleteTextures(1, &source);
        throw;
    }
    GLState::deleteFramebuffers(1, &framebuffer);
    glDeleteTextures(1, &source);
    m_skies=Texture::colorCubeArray(skySize,skyLevels,1);
    copyCube(m_radiance,m_skies,0,0,skyLevels);
    // The packed array owns the baked pixels; release the temporary cube.
    m_radiance.reset();
}

void EnvironmentMap::configureRooms(const std::filesystem::path& root,const SceneEnvironments& layout){
    if (layout.getSkies().empty()) return;
    const auto count=static_cast<unsigned>(layout.getSkies().size())+1;
    auto skies=Texture::colorCubeArray(skySize,skyLevels,count);
    auto filtered=Texture::colorCubeArray(256,9,count);
    copyCube(m_skies,skies,0,0,skyLevels);
    copyCube(m_prefiltered,filtered,0,0,9);
    auto irradiances=std::vector<std::array<glm::vec3,9>>{m_irradiance};
    for (unsigned i=0;i<layout.getSkies().size();++i){
        const auto& sky=layout.getSkies()[i];
        EnvironmentMap room(root,sky.image,sky.exposure);
        copyCube(room.m_skies,skies,0,i+1,skyLevels);
        copyCube(room.m_prefiltered,filtered,0,i+1,9);
        irradiances.push_back(room.m_irradiance);
    }
    m_skies=std::move(skies);m_prefiltered=std::move(filtered);
    m_irradiances=std::move(irradiances);m_layout=layout;
    m_localMaps.reset();m_readyMask=1;
}

void EnvironmentMap::applyRegions(const Program& program) const{
    program.set("uEnvironmentCount",static_cast<int>(getSkyCount()));
    const auto& regions=m_layout.getRegions();
    program.set("uSkyRegionCount",static_cast<int>(regions.size()));
    for (std::size_t i=0;i<regions.size();++i){
        const auto suffix="["+std::to_string(i)+"]";
        const auto& r=regions[i];
        program.set("uSkyRegionMin"+suffix,r.bounds.getMinimum());
        program.set("uSkyRegionMax"+suffix,r.bounds.getMaximum());
        program.set("uSkyRegionCylinder"+suffix,r.cylinder);
        program.set("uSkyOpeningMin"+suffix,r.opening.getMinimum());
        program.set("uSkyOpeningMax"+suffix,r.opening.getMaximum());
        program.set("uSkyOpeningNormal"+suffix,r.outward);
        program.set("uSkyRegionLayer"+suffix,static_cast<int>(r.sky));
    }
}

// Publishes the environment to a forward program using the two remaining sampler units.
void EnvironmentMap::apply(const Program& program, unsigned firstUnit) const{
    const auto maps =
        m_mode == ReflectionMode::Cubemaps && m_localMaps ? m_localMaps : m_prefiltered;
    maps->apply(firstUnit);
    m_brdf->getTexture()->apply(firstUnit + 1);
    program.set("uEnvironment", static_cast<int>(firstUnit));
    program.set("uEnvironmentBrdf", static_cast<int>(firstUnit + 1));
    program.set("uEnvironmentMaxLod", 8.0f);
    program.set("uEnvironmentEnabled", true);
    program.set("uReflectionMode", static_cast<int>(m_mode));
    program.set("uReflectionReadyMask", m_localMaps ? static_cast<int>(m_readyMask) : 1);
    applyRegions(program);
    for (unsigned layer=0;layer<m_irradiances.size();++layer){
      for (unsigned i = 0; i < 9; ++i){
        program.set("uIrradiance[" + std::to_string(layer*9+i) + "]", m_irradiances[layer][i]);
      }
    }
}

// Copies the static environment into layer zero of a newly allocated local array.
void EnvironmentMap::initializeLocalReflections(const std::shared_ptr<Texture>& maps){
    for (unsigned layer=0;layer<getSkyCount();++layer) copyCube(m_prefiltered,maps,layer,layer,9);
}

// Prefilters a complete scene capture into one local reflection layer.
void EnvironmentMap::prefilterLocal(const std::shared_ptr<Texture>& source,
                                    const std::shared_ptr<Texture>& maps, unsigned layer){
    FramebufferState saved;
    Antialiasing::screenPass();
    maps->apply(0);
    source->apply(1);
    glGenerateMipmap(GL_TEXTURE_CUBE_MAP);
    auto& bake = *m_bakeProgram;
    bake.use();
    bake.set("uPass", 1);
    bake.set("uSourceCube", 1);
    bake.set("uSampleCount", 32);
    GLuint framebuffer = 0;
    glGenFramebuffers(1, &framebuffer);
    GLState::bindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    glDrawBuffer(GL_COLOR_ATTACHMENT0);
    for (unsigned level = 0; level < 9; ++level){
        const int size = std::max(1, 256 >> level);
        GLState::viewport(0, 0, size, size);
        bake.set("uRoughness", static_cast<float>(level) / 8);
        for (unsigned face = 0; face < 6; ++face){
            glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, maps->getId(), level,
                                      layer * 6 + face);
            bake.set("uFace", static_cast<int>(face));
            m_quad.draw();
        }
    }
    GLState::deleteFramebuffers(1, &framebuffer);
}

// Draws an HDR or display-encoded sky before forward scene geometry.
void EnvironmentMap::drawSky(const Camera& camera, bool linearOutput){
    FramebufferState saved;
    Antialiasing::screenPass();
    m_skyProgram->use();
    m_skies->apply(0);
    applyRegions(*m_skyProgram);
    m_skyProgram->set("uSky", 0);
    m_skyProgram->set("uInverseProjection", glm::inverse(camera.getProjection()));
    m_skyProgram->set("uInverseView", glm::inverse(camera.getView()));
    m_skyProgram->set("uEye",camera.getPosition());
    m_skyProgram->set("uLinearOutput", linearOutput);
    m_quad.draw();
}

// Adds the sky and surface IBL to the currently bound deferred lighting target.
void EnvironmentMap::drawDeferred(const Camera& camera, const GBuffer& buffer,
                                  const std::shared_ptr<Texture>& ambient, int brdfMode){
    FramebufferState saved;
    Antialiasing::screenPass();
    const auto& program =
        buffer.getDepth()->getSamples() > 1 ? m_multisampleProgram : m_deferredProgram;
    program->use();
    program->set("uBrdfMode", brdfMode);
    const std::array<unsigned, 6> attachments{{GBuffer::Position, GBuffer::Normal, GBuffer::Albedo,
                                               GBuffer::Specular, GBuffer::Emissive,
                                               GBuffer::Ambient}};
    const std::array<const char*, 6> names{
        {"uPosition", "uNormal", "uAlbedo", "uSpecular", "uEmissive", "uAmbient"}};
    for (unsigned i = 0; i < attachments.size(); ++i){
        const auto& texture =
            attachments[i] == GBuffer::Ambient ? ambient : buffer.get(attachments[i]);
        texture->apply(i);
        program->set(names[i], static_cast<int>(i));
    }
    apply(*program, 6);
    m_skies->apply(8);
    program->set("uSky", 8);
    program->set("uEye", camera.getPosition());
    program->set("uInverseProjection", glm::inverse(camera.getProjection()));
    program->set("uInverseView", glm::inverse(camera.getView()));
    GLState::enable(GL_BLEND);
    GLState::blendFunc(GL_ONE, GL_ONE);
    GLState::blendEquation(GL_FUNC_ADD);
    GLState::colorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_FALSE);
    m_quad.draw();
}
} // namespace vr
