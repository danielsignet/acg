#include <algorithm>
#include <set>
#include <stdexcept>
#include <vr/FramebufferState.h>
#include <vr/GLState.h>
#include <vr/ModelInstance.h>
#include <vr/ReflectionProbes.h>

namespace vr{
namespace{
constexpr int captureSize = 128;
constexpr unsigned maxProbes = 15;

// Restores picking and scene visibility even when an auxiliary draw fails.
class HideInstance final{
public:
    explicit HideInstance(ModelInstance& instance)
        : m_instance(instance), m_visible(instance.getVisible()){
        instance.setVisible(false);
    }

    ~HideInstance(){
        m_instance.setVisible(m_visible);
    }

private:
    ModelInstance& m_instance;
    bool m_visible;
};
} // namespace

// Assigns local reflection layers to selectable environment-mapped model instances.
ReflectionProbes::ReflectionProbes(Scene& scene, EnvironmentMap& environment,
                                   const std::shared_ptr<Program>& program)
    : m_renderer(program){
    CullVisitor collector;
    collector.collect(*scene.getRoot(), glm::mat4(1), scene.getCamera()->getPosition(), false);
    std::set<ModelInstance*> assigned;
    for (const auto& item : collector.getRenderList()){
        const auto found = item.state.getUniforms().find("uEnvironmentMaterial");
        if (m_probes.size() == maxProbes){
            break;
        }
        if (!item.instance || !item.instance->getSelectable() ||
            found == item.state.getUniforms().end() || !std::get<bool>(found->second) ||
            !assigned.insert(item.instance).second){
            continue;
        }
        if (!item.instance->getState()){
            item.instance->setState(std::make_shared<State>());
        }
        m_probes.push_back({item.instance, item.instanceModel, item.room});
        item.instance->getState()->setUniform("uReflectionProbe",
                                              static_cast<int>(m_probes.size()));
    }
    m_maps = Texture::colorCubeArray(256, 9, getProbeCount() + 1);
    environment.initializeLocalReflections(m_maps);
    environment.setLocalReflections(m_maps, m_readyMask);
    m_capture = Texture::colorCube(captureSize, 8);
    m_depth = Texture::depth(captureSize, captureSize);
    m_renderer.setEnvironment(&environment);
    m_renderer.setEnvironmentCapture();
    m_renderer.setCulling(true);
}

// Releases the capture framebuffer while the OpenGL context is still current.
ReflectionProbes::~ReflectionProbes(){
    if (m_framebuffer){
        GLState::deleteFramebuffers(1, &m_framebuffer);
    }
}

// Counts fully published probes independently of their current update revision.
unsigned ReflectionProbes::getReadyCount() const{
    unsigned count = 0;
    for (unsigned i = 1; i <= m_probes.size(); ++i){
        count += (m_readyMask >> i) & 1u;
    }
    return count;
}

// Captures at most one visible object's cube face and publishes complete filtered maps.
void ReflectionProbes::update(Scene& scene, const Camera& camera, EnvironmentMap& environment,
                              ShadingMode shading, const PortalResult& visibility, bool dynamic){
    idle();
    if (m_probes.empty()){
        return;
    }
    const Frustum frustum(camera.getProjection() * camera.getView());
    const auto eligible = [&](const Probe& probe){
        return probe.instance->getVisible() &&
               (probe.room.empty() || visibility.visible(probe.room)) &&
               frustum.classify(probe.instance->getBound().transformed(probe.model)) !=
                   Visibility::Outside &&
               (dynamic || probe.revision != m_revision);
    };
    if (!eligible(m_probes[m_cursor])){
        m_face = 0;
        unsigned checked = 0;
        while (checked++ < m_probes.size() && !eligible(m_probes[m_cursor])){
            m_cursor = (m_cursor + 1) % getProbeCount();
        }
        if (!eligible(m_probes[m_cursor])){
            return;
        }
    }
    auto& probe = m_probes[m_cursor];
    const auto center = probe.instance->getBound().transformed(probe.model).getCenter();
    const std::array<glm::vec3, 6> directions{
        {{1, 0, 0},{-1, 0, 0},{0, 1, 0},{0, -1, 0},{0, 0, 1},{0, 0, -1}}};
    const std::array<glm::vec3, 6> up{
        {{0, -1, 0},{0, -1, 0},{0, 0, 1},{0, 0, -1},{0, -1, 0},{0, -1, 0}}};
    Camera capture;
    capture.setPerspective(90, 1, 0.1f, 150);
    capture.setLookAt(center, center + directions[m_face], up[m_face]);
    FramebufferState saved;
    m_capture->apply(0);
    m_depth->apply(1);
    if (!m_framebuffer){
        glGenFramebuffers(1, &m_framebuffer);
    }
    GLState::bindFramebuffer(GL_FRAMEBUFFER, m_framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_CUBE_MAP_POSITIVE_X + m_face, m_capture->getId(), 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, m_depth->getId(), 0);
    glDrawBuffer(GL_COLOR_ATTACHMENT0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE){
        throw std::runtime_error("Incomplete local reflection framebuffer");
    }
    GLState::viewport(0, 0, captureSize, captureSize);
    GLState::disable(GL_SCISSOR_TEST);
    GLState::disable(GL_BLEND);
    GLState::disable(GL_MULTISAMPLE);
    GLState::disable(GL_SAMPLE_SHADING);
    GLState::enable(GL_DEPTH_TEST);
    GLState::depthFunc(GL_LESS);
    GLState::depthMask(GL_TRUE);
    GLState::colorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    GLState::frontFace(GL_CCW);
    GLState::clearDepth(1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    {
        HideInstance hidden(*probe.instance);
        m_renderer.setShadingMode(shading);
        // The probe originates at the excluded object's center, rather than the observer.
        auto portals = scene.getPortalGraph().collect(center, nullptr);
        m_renderer.render(*scene.getRoot(), capture, nullptr, &portals);
    }
    m_stats = m_renderer.getStats();
    m_faces = 1;
    if (++m_face == 6){
        environment.prefilterLocal(m_capture, m_maps, m_cursor + 1);
        m_filterDraws = 6 * 9;
        m_readyMask |= 1u << (m_cursor + 1);
        environment.setLocalReflections(m_maps, m_readyMask);
        probe.revision = m_revision;
        m_face = 0;
        m_cursor = (m_cursor + 1) % getProbeCount();
    }
}
} // namespace vr
