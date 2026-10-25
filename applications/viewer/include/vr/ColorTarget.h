#pragma once
#include "Texture.h"

namespace vr{
class ColorTarget final{
public:
    // Creates a floating-point color target with optional shared opaque depth.
    ColorTarget(int width, int height, std::shared_ptr<Texture> depth ={});
    // Releases the owned framebuffer before its textures.
    ~ColorTarget();
    // Prevents copying framebuffer ownership.
    ColorTarget(const ColorTarget&) = delete;
    // Prevents assigning framebuffer ownership.
    ColorTarget& operator=(const ColorTarget&) = delete;
    // Binds the color target and sets its pixel viewport.
    void bind() const;

    // Returns the color texture for a later screen pass.
    const std::shared_ptr<Texture>& getTexture() const{
        return m_color;
    }

private:
    GLuint m_framebuffer = 0;
    std::shared_ptr<Texture> m_color, m_depth;
};
} // namespace vr
