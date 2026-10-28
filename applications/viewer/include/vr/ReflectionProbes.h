#pragma once
#include "EnvironmentMap.h"
#include "RenderVisitor.h"
#include "Scene.h"

namespace vr{
class ReflectionProbes final{
public:
    // Assigns local reflection layers to selectable environment-mapped model instances.
    ReflectionProbes(Scene& scene, EnvironmentMap& environment,
                     const std::shared_ptr<Program>& program);
    // Releases the capture framebuffer while the OpenGL context is still current.
    ~ReflectionProbes();
    // Keeps ownership of the capture framebuffer in one instance.
    ReflectionProbes(const ReflectionProbes&) = delete;
    // Prevents assigning framebuffer ownership or scene pointers.
    ReflectionProbes& operator=(const ReflectionProbes&) = delete;

    // Captures at most one visible object's cube face and publishes complete filtered maps.
    void update(Scene& scene, const Camera& camera, EnvironmentMap& environment,
                ShadingMode shading, const PortalResult& visibility, bool dynamic);

    // Restarts partial captures after lighting, material or geometry changes.
    void invalidate(){
        ++m_revision;
        m_face = 0;
    }

    // Clears per-frame counters while a different reflection source is active.
    void idle(){
        m_stats ={};
        m_faces = 0;
        m_filterDraws = 0;
    }

    // Returns auxiliary geometry submitted during this frame's face capture.
    const RenderStats& getStats() const{
        return m_stats;
    }

    // Returns the number of cube faces captured during the latest scene frame.
    unsigned getFaces() const{
        return m_faces;
    }

    // Returns roughness-prefilter fullscreen submissions during the latest frame.
    unsigned getFilterDraws() const{
        return m_filterDraws;
    }

    // Counts complete local maps that are available for material sampling.
    unsigned getReadyCount() const;

    // Returns the number of eligible reflective model instances in this scene.
    unsigned getProbeCount() const{
        return static_cast<unsigned>(m_probes.size());
    }

    void setStateCaching(bool enabled){
        m_renderer.setStateCaching(enabled);
    }

private:
    struct Probe{
        ModelInstance* instance;
        glm::mat4 model;
        std::string room;
        unsigned revision = 0;
    };

    std::vector<Probe> m_probes;
    RenderVisitor m_renderer;
    RenderStats m_stats;
    std::shared_ptr<Texture> m_capture, m_depth, m_maps;
    GLuint m_framebuffer = 0;
    unsigned m_cursor = 0, m_face = 0, m_revision = 1, m_readyMask = 1;
    unsigned m_faces = 0, m_filterDraws = 0;
};
} // namespace vr
