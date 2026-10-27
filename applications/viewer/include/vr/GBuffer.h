#pragma once
#include "Texture.h"
#include <array>

namespace vr{
class GBuffer final{
public:
    enum Attachment : unsigned{
        Position,
        Normal,
        Albedo,
        Specular,
        Ambient,
        Emissive,
        Plane,
        Count
    };

    // Allocates floating-point surface data and a shared camera depth texture.
    GBuffer(int width, int height, int samples = 1);
    // Releases the framebuffer before its attachment textures.
    ~GBuffer();
    // Prevents copying framebuffer ownership.
    GBuffer(const GBuffer&) = delete;
    // Prevents assigning framebuffer ownership.
    GBuffer& operator=(const GBuffer&) = delete;
    // Binds and clears every surface attachment for a new geometry pass.
    void begin() const;
    // Binds all surface outputs, or one attachment for an alpha-only AO update.
    void bind(unsigned attachment = Count) const;
    // Returns a surface attachment for lighting, previews or diagnostics.
    const std::shared_ptr<Texture>& get(unsigned attachment) const;

    // Returns the opaque depth shared with the transparent color pass.
    const std::shared_ptr<Texture>& getDepth() const{
        return m_depth;
    }

    // Reads unencoded floating-point surface values from one attachment.
    std::vector<float> read(unsigned attachment) const;

private:
    GLuint m_framebuffer = 0;
    std::array<std::shared_ptr<Texture>, Count> m_textures;
    std::shared_ptr<Texture> m_depth;
};
} // namespace vr
