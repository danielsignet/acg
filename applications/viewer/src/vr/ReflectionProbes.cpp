#include <algorithm>
#include <set>
#include <stdexcept>
#include <vr/FramebufferState.h>
#include <vr/FrameWork.h>
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
    : m_environment(environment), m_renderer(program){
    CullVisitor collector;
    collector.collect(*scene.getRoot(), glm::mat4(1), scene.getCamera()->getPosition(), false);
    m_capacity = std::min(maxProbes,
        Texture::MaxCubeArrayLayers - environment.getSkyCount());
    std::set<ModelInstance*> assigned;
    // Clear stale bindings once for this scene, rather than at every doorway.
    for(const auto& item:collector.getRenderList()){
        if(item.instance && item.instance->getSelectable() && assigned.insert(item.instance).second){
            if(!item.instance->getState()) item.instance->setState(std::make_shared<State>());
            item.instance->getState()->setUniform("uReflectionProbe",0);
        }
    }
    m_probes = discover(collector.getRenderList());
    if (m_probes.size() > m_capacity) m_probes.resize(m_capacity);
    for (unsigned i = 0; i < m_probes.size(); ++i) bind(m_probes[i], i + 1);
    // Reserve the bounded pool once. Newly visible rooms reuse free/cold slots
    // without reallocating the array or losing maps for models still in view.
    m_maps = Texture::colorCubeArray(256, 9, m_capacity + environment.getSkyCount());
    environment.initializeLocalReflections(m_maps);
    environment.setLocalReflections(m_maps, m_readyMask);
    m_capture = Texture::colorCube(captureSize, 8);
    m_filtered = Texture::colorCubeArray(256,9,1);
    m_depth = Texture::depth(captureSize, captureSize);
    m_renderer.setEnvironment(&environment);
    m_renderer.setEnvironmentCapture();
    m_renderer.setCulling(true);
}

std::vector<ReflectionProbes::Probe> ReflectionProbes::discover(const std::vector<RenderNode>& items){
    std::vector<Probe> probes;
    std::set<ModelInstance*> assigned;
    std::map<int,unsigned> groups;
    for (const auto& item : items){
        const auto found = item.state.getUniforms().find("uEnvironmentMaterial");
        if (!item.instance || !item.instance->getSelectable() ||
            found == item.state.getUniforms().end() || !std::get<bool>(found->second) ||
            !assigned.insert(item.instance).second){
            continue;
        }
        const auto tagged=item.state.getUniforms().find("uReflectionGroup");
        const int group=tagged==item.state.getUniforms().end()?0:std::get<int>(tagged->second);
        if(group && groups.count(group)){
            probes[groups.at(group)].members.push_back(item.instance);
            continue;
        }
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
        if(group) groups[group]=static_cast<unsigned>(probes.size());
        probes.push_back({item.instance, item.instanceModel, item.room, 0, excluded,
                          group, {item.instance}});
    }
    return probes;
}

void ReflectionProbes::bind(Probe& probe, unsigned layer){
    for (auto* instance : probe.members){
        if (!instance->getState()) instance->setState(std::make_shared<State>());
        auto& state = *instance->getState();
        const auto value = state.getUniforms().find("uReflectionProbe");
        if (value == state.getUniforms().end() || std::get<int>(value->second) != static_cast<int>(layer))
            state.setUniform("uReflectionProbe", static_cast<int>(layer));
    }
}

void ReflectionProbes::synchronize(Scene& scene, const Camera& camera, const PortalResult& visibility,
    const std::function<bool(const std::string&)>& roomVisible){
    ++m_frame;
    m_selector.collect(*scene.getRoot(), camera.getProjection()*camera.getView(),
                       camera.getPosition(), true, &visibility);
    if (roomVisible) m_selector.filterRooms(roomVisible);
    auto candidates = discover(m_selector.getRenderList());
    for (auto& probe : m_probes) probe.visible = false;
    // Mark retained slots first, so discovering a new model cannot evict a
    // different model that is also needed by this frame's observer.
    std::vector<Probe> missing;
    for (auto& candidate : candidates){
        const auto found = std::find_if(m_probes.begin(), m_probes.end(), [&](const Probe& probe){
            return candidate.group ? candidate.group == probe.group
                                   : !probe.group && candidate.instance == probe.instance;
        });
        if (found == m_probes.end()){
            missing.push_back(std::move(candidate));
            continue;
        }
        found->visible = true;
        found->lastVisible = m_frame;
        if (found->instance == candidate.instance && found->model != candidate.model){
            found->model = candidate.model;
            found->revision = 0;
        }
        for (auto* member : candidate.members)
            if (std::find(found->members.begin(), found->members.end(), member) == found->members.end())
                found->members.push_back(member);
        bind(*found, static_cast<unsigned>(found - m_probes.begin()) + 1);
    }
    for (auto& candidate : missing){
        unsigned slot = getProbeCount();
        if (slot == m_capacity){
            auto cold = m_probes.end();
            for (auto it = m_probes.begin(); it != m_probes.end(); ++it)
                if (!it->visible && (cold == m_probes.end() || it->lastVisible < cold->lastVisible)) cold = it;
            if (cold == m_probes.end()){
                bind(candidate, 0);
                continue;
            }
            slot = static_cast<unsigned>(cold - m_probes.begin());
            bind(*cold, 0);
            if (slot == m_cursor){ m_face = 0; m_filterCursor = 54; }
            *cold = std::move(candidate);
        } else{
            m_probes.push_back(std::move(candidate));
        }
        auto& probe = m_probes[slot];
        probe.visible = true;
        probe.lastVisible = m_frame;
        m_readyMask &= ~(1u << (slot + 1));
        bind(probe, slot + 1);
    }
    m_environment.setLocalReflections(m_maps, m_readyMask);
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

const Camera* ReflectionProbes::plan(Scene& scene, const Camera& camera, const PortalResult& visibility,
                                                bool dynamic,
                                                const std::function<bool(const std::string&)>& roomVisible){
    m_hasPlan = true;
    m_planValid = false;
    synchronize(scene, camera, visibility, roomVisible);
    if (m_probes.empty() || m_filterCursor<54){
        return nullptr;
    }
    const auto eligible = [&](const Probe& probe){
        return probe.visible &&
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
    if (m_face == 0){
        m_captureCenter = probe.instance->getBound().transformed(probe.model).getCenter();
        m_captureModel = probe.model;
    }
    const std::array<glm::vec3, 6> directions{
        {{1, 0, 0},{-1, 0, 0},{0, 1, 0},{0, -1, 0},{0, 0, 1},{0, 0, -1}}};
    const std::array<glm::vec3, 6> up{
        {{0, -1, 0},{0, -1, 0},{0, 0, 1},{0, 0, -1},{0, -1, 0},{0, -1, 0}}};
    if (!m_planned) m_planned = std::make_unique<Camera>();
    m_planned->setPerspective(90, 1, 0.1f, 150);
    m_planned->setLookAt(m_captureCenter, m_captureCenter + directions[m_face], up[m_face]);
    m_planValid = true;
    return m_planned.get();
}

// Captures at most one visible object's cube face and publishes complete filtered maps.
void ReflectionProbes::update(Scene& scene, const Camera& camera, EnvironmentMap& environment,
                              ShadingMode shading, const PortalResult& visibility, bool dynamic){
    idle();
    if(m_filterCursor<54){
        m_hasPlan=m_planValid=false;
        // Budget texture samples as well as submissions. Sharp level zero is
        // a single lookup; rougher levels use the full 32-sample GGX kernel.
        unsigned count=0,samples=0;
        while(m_filterCursor+count<54 && count<18){
            const unsigned level=(m_filterCursor+count)/6;
            const unsigned size=std::max(1u,256u>>level);
            const unsigned cost=size*size*(level?32u:1u);
            if(count && samples+cost>1024u*1024u) break;
            samples+=cost;++count;
        }
        environment.prefilterLocalRange(m_capture,m_filtered,0,m_filterCursor,count);
        m_filterDraws=count;m_filterCursor+=count;
        if(m_filterCursor==54){
            environment.publishLocal(m_filtered,m_maps,m_cursor+environment.getSkyCount());
            m_readyMask|=1u<<(m_cursor+1);
            environment.setLocalReflections(m_maps,m_readyMask);
            auto& probe = m_probes[m_cursor];
            probe.revision = probe.model == m_captureModel ? m_revision : 0;
            m_cursor=(m_cursor+1)%getProbeCount();
        }
        return;
    }
    if (!m_hasPlan) plan(scene, camera, visibility, dynamic);
    m_hasPlan = false;
    if (!m_planValid) return;
    const auto& capture = *m_planned;
    m_planValid = false;
    auto& probe = m_probes[m_cursor];
    environment.setLocalReflections(m_maps, m_readyMask);
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
        if(frameWorkOptimized()) m_filterCursor=0;
        else{
            environment.prefilterLocal(m_capture,m_maps,m_cursor+environment.getSkyCount());
            m_filterDraws=54;m_readyMask|=1u<<(m_cursor+1);
            environment.setLocalReflections(m_maps,m_readyMask);
            probe.revision = probe.model == m_captureModel ? m_revision : 0;
            m_cursor=(m_cursor+1)%getProbeCount();
        }
        m_face = 0;
    }
}
} // namespace vr
