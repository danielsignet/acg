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
    explicit HideInstance(Node& instance)
        : m_instance(instance), m_visible(instance.getVisible()){
        instance.setVisible(false);
    }

    ~HideInstance(){
        m_instance.setVisible(m_visible);
    }

private:
    Node& m_instance;
    bool m_visible;
};
} // namespace

// Assigns local reflection layers to selectable environment-mapped model instances.
ReflectionProbes::ReflectionProbes(Scene& scene, EnvironmentMap& environment,
                                   const std::shared_ptr<Program>& program)
    : m_renderer(program){
    CullVisitor collector;
    collector.collect(*scene.getRoot(), glm::mat4(1), scene.getCamera()->getPosition(), false);
    const auto probeLimit = std::min(maxProbes,
        Texture::MaxCubeArrayLayers - environment.getSkyCount());
    std::set<ModelInstance*> assigned;
    // A room switch replaces the probe array. Clear previous layer IDs before
    // assigning the visible instances, including candidates beyond the limit.
    for(const auto& item:collector.getRenderList()){
        if(item.instance && item.instance->getSelectable() && assigned.insert(item.instance).second){
            if(!item.instance->getState()) item.instance->setState(std::make_shared<State>());
            item.instance->getState()->setUniform("uReflectionProbe",0);
        }
    }
    assigned.clear();
    std::map<int,unsigned> groups;
    for (const auto& item : collector.getRenderList()){
        const auto found = item.state.getUniforms().find("uEnvironmentMaterial");
        if (!item.instance || !item.instance->getSelectable() ||
            found == item.state.getUniforms().end() || !std::get<bool>(found->second) ||
            !assigned.insert(item.instance).second){
            continue;
        }
        if (!item.instance->getState()){
            item.instance->setState(std::make_shared<State>());
        }
        const auto tagged=item.state.getUniforms().find("uReflectionGroup");
        const int group=tagged==item.state.getUniforms().end()?0:std::get<int>(tagged->second);
        if(group && groups.count(group)){
            item.instance->getState()->setUniform("uReflectionProbe",static_cast<int>(groups[group]));
            continue;
        }
        if(m_probes.size()==probeLimit) continue;
        Node* excluded=item.instance;
        if(group){
            std::shared_ptr<Node> ancestor=item.instance->shared_from_this();
            while(ancestor){
                if(ancestor->getState()){
                    const auto local=ancestor->getState()->getUniforms().find("uReflectionGroup");
                    if(local!=ancestor->getState()->getUniforms().end() && std::get<int>(local->second)==group)
                        excluded=ancestor.get();
                }
                if(ancestor->getParents().empty()) break;
                ancestor=ancestor->getParents().front().lock();
            }
        }
        m_probes.push_back({item.instance, item.instanceModel, item.room,0,excluded});
        if(group) groups[group]=static_cast<unsigned>(m_probes.size());
        item.instance->getState()->setUniform("uReflectionProbe",
                                              static_cast<int>(m_probes.size()));
    }
    m_maps = Texture::colorCubeArray(256, 9, getProbeCount() + environment.getSkyCount());
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

const Camera* ReflectionProbes::plan(const Camera& camera, const PortalResult& visibility,
                                                bool dynamic,
                                                const std::function<bool(const std::string&)>& roomVisible){
    m_hasPlan = true;
    m_planValid = false;
    if (m_probes.empty()){
        return nullptr;
    }
    const Frustum frustum(camera.getProjection() * camera.getView());
    const auto eligible = [&](const Probe& probe){
        return probe.instance->getVisible() &&
               (!roomVisible || roomVisible(probe.room)) &&
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
            return nullptr;
        }
    }
    const auto& probe = m_probes[m_cursor];
    const auto center = probe.instance->getBound().transformed(probe.model).getCenter();
    const std::array<glm::vec3, 6> directions{
        {{1, 0, 0},{-1, 0, 0},{0, 1, 0},{0, -1, 0},{0, 0, 1},{0, 0, -1}}};
    const std::array<glm::vec3, 6> up{
        {{0, -1, 0},{0, -1, 0},{0, 0, 1},{0, 0, -1},{0, -1, 0},{0, -1, 0}}};
    if (!m_planned) m_planned = std::make_unique<Camera>();
    m_planned->setPerspective(90, 1, 0.1f, 150);
    m_planned->setLookAt(center, center + directions[m_face], up[m_face]);
    m_planValid = true;
    return m_planned.get();
}

// Captures at most one visible object's cube face and publishes complete filtered maps.
void ReflectionProbes::update(Scene& scene, const Camera& camera, EnvironmentMap& environment,
                              ShadingMode shading, const PortalResult& visibility, bool dynamic){
    idle();
    if (!m_hasPlan) plan(camera, visibility, dynamic);
    m_hasPlan = false;
    if (!m_planValid) return;
    const auto& capture = *m_planned;
    m_planValid = false;
    auto& probe = m_probes[m_cursor];
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
        HideInstance hidden(*(probe.excluded?probe.excluded:probe.instance));
        m_renderer.setShadingMode(shading);
        // The probe originates at the excluded object's center, rather than the observer.
        auto portals = scene.getPortalGraph().collect(capture.getPosition(), nullptr);
        m_renderer.render(*scene.getRoot(), capture, nullptr, &portals);
    }
    m_stats = m_renderer.getStats();
    m_faces = 1;
    if (++m_face == 6){
        environment.prefilterLocal(m_capture, m_maps, m_cursor + environment.getSkyCount());
        m_filterDraws = 6 * 9;
        m_readyMask |= 1u << (m_cursor + 1);
        environment.setLocalReflections(m_maps, m_readyMask);
        probe.revision = m_revision;
        m_face = 0;
        m_cursor = (m_cursor + 1) % getProbeCount();
    }
}
} // namespace vr
