#pragma once
#include "Camera.h"
#include "Texture.h"
#include <functional>

namespace vr{
// A scene attachment: one depth image per specified light camera.
class RenderToTexture{
public:
    // Creates a complete depth framebuffer backed by one image or six cubemap faces.
    RenderToTexture(int resolution, bool cube);

    // Releases the owned framebuffer before its OpenGL context is destroyed.
    ~RenderToTexture();

    // Prevents copying ownership of an OpenGL framebuffer.
    RenderToTexture(const RenderToTexture&) = delete;

    // Prevents assigning ownership of an OpenGL framebuffer.
    RenderToTexture& operator=(const RenderToTexture&) = delete;

    // Assigns one independent camera to the requested depth image.
    void setCamera(unsigned layer, const std::shared_ptr<Camera>& camera);

    // Returns the light cameras assigned to this render target.
    const std::vector<std::shared_ptr<Camera>>& getCameras() const{
        return m_cameras;
    }

    // Returns the depth texture that a later color pass can sample.
    const std::shared_ptr<Texture>& getTexture() const{
        return m_texture;
    }

    // Renders a camera into its depth image and restores caller state on return or throw.
    void render(unsigned layer, const std::function<void(const Camera&)>& draw);

    // Reads an actual depth image for diagnostics without changing the caller target.
    std::vector<float> readDepth(unsigned layer) const;

private:
    friend class ShadowRenderer;
    // The shadow renderer owns one caller-state guard around all light faces.
    void renderFace(unsigned layer, const std::function<void(const Camera&)>& draw);
    // Binds the prevalidated framebuffer for one immutable depth face.
    void attach(unsigned layer) const;
    std::vector<GLuint> m_framebuffers;
    std::shared_ptr<Texture> m_texture;
    std::vector<std::shared_ptr<Camera>> m_cameras;
};
} // namespace vr
