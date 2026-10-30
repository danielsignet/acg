#pragma once
#include <vr/Reader.h>
#include <vr/Camera.h>
#include <sstream>
#include <cmath>
#include <set>
#include <cstdlib>
#include <vr/UpdateVisitor.h>
#include <vr/GpuSkinning.h>
#include "RoomPortalViews.h"

namespace pantheon{
// An empty controller node changes visibility on its sibling subtrees. Keeping
// the callback off the architecture avoids marking every shadow caster dynamic.
class RoomIsolation final : public vr::UpdateCallback, public vr::SceneRenderExtension{
public:
    struct Zone{
        std::string room;
        glm::vec3 minimum,maximum;
        glm::vec3 circle; // horizontal centre x/z and optional radius
        glm::vec3 openingMin{0},openingMax{0};
        glm::vec3 entryMin{0},entryMax{0};
        bool contains(const glm::vec3& p) const{
            if (glm::any(glm::lessThan(p,minimum-1e-4f)) ||
                glm::any(glm::greaterThan(p,maximum+1e-4f))) return false;
            const glm::vec2 d(p.x-circle.x,p.z-circle.y);
            return circle.z==0 || glm::dot(d,d)<=circle.z*circle.z+1e-4f;
        }
    };

    RoomIsolation(const std::shared_ptr<vr::Transform>& target,const vr::Reader::AnimationAttributes& attrs)
        : m_target(target){
        if (const auto hub = attrs.find("hub-radius"); hub != attrs.end()) m_hubRadius = std::stof(hub->second);
        if (!std::isfinite(m_hubRadius) || m_hubRadius < 0) throw std::invalid_argument("Invalid hub radius");
        const auto* mode = std::getenv("PANTHEON_HUB_WORK");
        m_fullHub = mode && std::string(mode) == "all";
        for (const auto& [key,value]:attrs){
            if (key.compare(0,5,"zone-")!=0) continue;
            std::istringstream input(value);
            float v[9]{};
            for (float& x:v) if (!(input>>x) || !std::isfinite(x))
                throw std::invalid_argument("Invalid room isolation volume");
            std::string extra;
            if (input>>extra) throw std::invalid_argument("Extra room isolation component");
            Zone zone{key.substr(5),{v[0],v[1],v[2]},{v[3],v[4],v[5]},{v[6],v[7],v[8]}};
            if (zone.room.empty() || glm::any(glm::lessThanEqual(zone.maximum,zone.minimum)) || zone.circle.z<0)
                throw std::invalid_argument("Invalid room isolation bounds");
            std::istringstream opening(attrs.at("opening-"+zone.room));
            float box[6]{};
            for (float& x:box) if (!(opening>>x) || !std::isfinite(x))
                throw std::invalid_argument("Invalid room opening");
            zone.openingMin={box[0],box[1],box[2]};
            zone.openingMax={box[3],box[4],box[5]};
            zone.entryMin = zone.openingMin; zone.entryMax = zone.openingMax;
            if (const auto entry = attrs.find("entry-"+zone.room); entry != attrs.end()){
                std::istringstream input(entry->second);
                for (float& x : box) if (!(input>>x) || !std::isfinite(x))
                    throw std::invalid_argument("Invalid hall doorway");
                zone.entryMin={box[0],box[1],box[2]}; zone.entryMax={box[3],box[4],box[5]};
            }
            m_zones.push_back(zone);
        }
        for (const auto& zone : m_zones) m_hubCeiling=std::min(m_hubCeiling,zone.entryMax.y);
        if (!target || m_zones.empty()) throw std::invalid_argument("Missing room isolation target/zones");
    }

    bool changesRenderedContent() const override{ return false; }

    void update(vr::Node& node,const vr::UpdateContext& context) override{
        const auto target=m_target.lock();
        if (!target || target.get()!=&node) return;
        if (m_camera.expired()) initialize(*target);
        const auto camera=m_camera.lock();
        if (!camera) return;
        std::string active;
        const Zone* selected=nullptr;
        for (const auto& zone:m_zones) if (zone.contains(camera->getPosition())){
            active=zone.room;selected=&zone;break;
        }
        m_context=context;
        const auto eye = camera->getPosition();
        m_hubActive = !m_fullHub && active.empty() && m_hubRadius > 0 && eye.y >= 0 && eye.y < m_hubCeiling &&
                      glm::dot(glm::vec2(eye.x,eye.z),glm::vec2(eye.x,eye.z)) < m_hubRadius*m_hubRadius;
        m_primaryRooms = neededRooms(*camera, "");
        for (const auto& mirror : m_mirrors) if (const auto node = mirror.node.lock())
            node->setVisible(mirror.visible && (!m_hubActive || m_primaryRooms.count(mirror.room)));
        m_updatedRooms.clear();
        for(const auto& [weak,enabled]:m_work) if(const auto wing=weak.lock()){
            const bool run=enabled && (active.empty() ? (!m_hubActive || !m_initialized || m_primaryRooms.count(wing->getRoomId())) : wing->getRoomId()==active);
            wing->setUpdateEnabled(run);
            if(run) m_updatedRooms.insert(wing->getRoomId());
        }
        if (m_initialized && active==m_active) return;
        for (const auto& [weak,visible]:m_hidden) if (const auto child=weak.lock()) child->setVisible(visible);
        m_hidden.clear();
        const auto owner=m_owner.lock();
        if (!owner) return;
        for (const auto& child:owner->getChildren()){
            if (child->getRoomId()!="rotunda" && child->getName().compare(0,11,"roomPortal_")!=0) continue;
            if (!child->getState()) child->setState(std::make_shared<vr::State>());
            child->getState()->setUniform("uRoomWindowEnabled",selected!=nullptr);
            if (selected){
                child->getState()->setUniform("uRoomWindowMin",selected->openingMin);
                child->getState()->setUniform("uRoomWindowMax",selected->openingMax);
            }
        }
        if (!active.empty()){
            for (const auto& child:owner->getChildren()){
                if (child->getRoomId().empty() || child->getRoomId()==active || child->getRoomId()=="rotunda") continue;
                m_hidden.emplace_back(child,child->getVisible());
                child->setVisible(false);
            }
        }
        m_active=active;m_initialized=true;
    }

    const std::string& activeRoom() const{ return m_active; }

    void beforeSceneRender(const vr::SceneRenderContext& context) override{
        m_viewScopes.emplace_back();
        // Probe visibility belongs to the probe's physical origin, independently
        // of the primary camera's current room. Nested portal captures retain
        // their explicitly isolated destination instead.
        if (context.environmentCapture && (!m_portals || !m_portals->isCapturing())){
            const auto owner = m_owner.lock();
            const Zone* source = nullptr;
            for (const auto& zone : m_zones)
                if (zone.contains(context.camera.getPosition())) { source = &zone; break; }
            if (owner) for (const auto& child : owner->getChildren()){
                m_viewScopes.back().push_back({child,child->getVisible(),child->getState()});
                bool visible = child->getVisible();
                for (const auto& saved : m_hidden)
                    if (saved.first.lock() == child) { visible = saved.second; break; }
                if (source && !child->getRoomId().empty() && child->getRoomId() != source->room &&
                    child->getRoomId() != "rotunda") visible = false;
                child->setVisible(visible);
                if (child->getRoomId() == "rotunda" || child->getName().compare(0,11,"roomPortal_") == 0){
                    auto state = child->getState() ? std::make_shared<vr::State>(*child->getState())
                                                   : std::make_shared<vr::State>();
                    state->setUniform("uRoomWindowEnabled", source != nullptr);
                    if (source){
                        state->setUniform("uRoomWindowMin",source->openingMin);
                        state->setUniform("uRoomWindowMax",source->openingMax);
                    }
                    child->setState(std::move(state));
                }
            }
        }
        if (!m_portals) return;
        auto measured=context;
        measured.profiler=m_context.profiler;
        if (m_active.empty()) {m_portals->render(measured,"",{},{});return;}
        for(const auto& zone:m_zones) if(zone.room==m_active){
            m_portals->render(measured,m_active,zone.openingMin,zone.openingMax);break;
        }
    }

    void afterSceneRender(const vr::SceneRenderContext&) noexcept override{
        if (m_viewScopes.empty()) return;
        for (const auto& saved : m_viewScopes.back()){
            saved.node->setState(saved.state);
            saved.node->setVisible(saved.visible);
        }
        m_viewScopes.pop_back();
    }

    bool roomVisible(const std::string& room, const vr::Camera&) const override{
        return !m_hubActive || room.empty() || room=="rotunda" || m_primaryRooms.count(room);
    }

    bool roomVisibleInView(const std::string& room,const vr::SceneRenderContext& context) const override{
        if (!m_hubActive || context.environmentCapture || context.reflectionPass || room.empty() ||
            room=="rotunda" || room.find("-passage")!=std::string::npos) return true;
        bool known=false;
        std::string source;
        for (const auto& zone:m_zones){
            known=known || zone.room==room;
            if (zone.contains(context.camera.getPosition())) source=zone.room;
        }
        if (!known) return true;
        if (context.reflectionPass && !m_mirrors.empty()) source=m_mirrors.front().room;
        const auto eye=context.camera.getPosition();
        if (source.empty() && glm::length(glm::vec2(eye.x,eye.z))>=m_hubRadius) return true;
        return neededRooms(context.camera,source).count(room)!=0;
    }

    bool dynamicEnvironment() const override{ return m_hubActive && m_context.animate; }

    void prepareFrameView(const vr::SceneRenderContext& context) override{
        if (!m_hubActive) return;
        std::string source;
        if (context.reflectionPass && !m_mirrors.empty()) source=m_mirrors.front().room;
        else for (const auto& zone : m_zones) if (zone.contains(context.camera.getPosition())) {source=zone.room;break;}
        const vr::Frustum frustum(context.camera.getProjection()*context.camera.getView());
        // Auxiliary views retain the full physical scene, including roofs and
        // objects visible above another room. Wake every intersecting subtree.
        for (const auto& [weak,enabled] : m_work) if (enabled) if (const auto wing=weak.lock())
            if (wing->getRoomId()==source || wing->hasCullingExemption() ||
                frustum.classify(wing->getWorldBound(glm::mat4(1)))!=vr::Visibility::Outside)
                prepareDestination(*wing,false);
    }

private:
    struct ViewState{
        std::shared_ptr<vr::Node> node;
        bool visible;
        std::shared_ptr<vr::State> state;
    };
    std::vector<std::vector<ViewState>> m_viewScopes;
    struct Rect{glm::vec2 low{-1},high{1};bool valid=true;};
    static Rect projected(const glm::mat4& vp, const glm::vec3& low, const glm::vec3& high){
        // Include the curved floor lip and a conservative guard at doorway edges.
        const auto minimum=low-glm::vec3(.75f),maximum=high+glm::vec3(.75f);
        if (vr::Frustum(vp).classify(vr::BoundingBox(minimum,maximum))==vr::Visibility::Outside) return {{},{},false};
        Rect result{glm::vec2(1e20f),glm::vec2(-1e20f),true};
        for(int corner=0;corner<8;++corner){
            const auto p=vp*glm::vec4(corner&1?maximum.x:minimum.x,corner&2?maximum.y:minimum.y,corner&4?maximum.z:minimum.z,1);
            if (p.w<=1e-4f) return {};
            const auto ndc=glm::vec2(p)/p.w;
            result.low=glm::min(result.low,ndc);result.high=glm::max(result.high,ndc);
        }
        return result;
    }
    static void intersect(Rect& a,const Rect& b){
        a.low=glm::max(a.low,b.low);a.high=glm::min(a.high,b.high);
        a.valid=a.valid && b.valid && glm::all(glm::greaterThanEqual(a.high-a.low,glm::vec2(-1e-4f)));
    }
    std::set<std::string> neededRooms(const vr::Camera& camera,const std::string& source) const{
        std::set<std::string> result;
        const auto vp=camera.getProjection()*camera.getView();
        Rect sourceWindow;
        for (const auto& zone:m_zones) if (zone.room==source){
            result.insert(source);
            intersect(sourceWindow,projected(vp,zone.openingMin,zone.openingMax));
            intersect(sourceWindow,projected(vp,zone.entryMin,zone.entryMax));
        }
        for (const auto& zone:m_zones){
            if (zone.room==source) continue;
            auto window=sourceWindow;
            intersect(window,projected(vp,zone.entryMin,zone.entryMax));
            intersect(window,projected(vp,zone.openingMin,zone.openingMax));
            if (window.valid) result.insert(zone.room);
        }
        return result;
    }
    void prepareDestination(vr::Node& wing,bool prepareSkin=true){
        if(!m_updatedRooms.insert(wing.getRoomId()).second) return;
        struct Restore{
            vr::Node& node;bool enabled;
            ~Restore(){ node.setUpdateEnabled(enabled); }
        } restore{wing,wing.getUpdateEnabled()};
        wing.setUpdateEnabled(true);
        vr::UpdateVisitor update;
        update.update(wing,m_context);
        if(prepareSkin && m_context.gpuSkinning) m_context.gpuSkinning->prepare();
    }

    void initialize(vr::Transform& target){
        if (target.getParents().size()!=1) throw std::runtime_error("Isolation controller needs one parent");
        auto owner=std::dynamic_pointer_cast<vr::Group>(target.getParents()[0].lock());
        if (!owner) throw std::runtime_error("Missing isolation scenegraph parent");
        for (const auto& zone:m_zones){
            bool found=false;
            for (const auto& child:owner->getChildren()) found=found || child->getRoomId()==zone.room;
            if (!found) throw std::runtime_error("Missing room subtree: "+zone.room);
        }
        m_owner=owner;
        const std::function<void(vr::Node&,std::string)> mirrors=[&](vr::Node& node,std::string room){
            if (!node.getRoomId().empty()) room=node.getRoomId();
            if (node.getName()=="materialMirror") m_mirrors.push_back({node.shared_from_this(),room,node.getVisible()});
            if (auto* group=dynamic_cast<vr::Group*>(&node)) for (const auto& child:group->getChildren()) mirrors(*child,room);
        };
        mirrors(*owner,"");
        std::vector<RoomPortalViews::Portal> portals;
        for(const auto& zone:m_zones){
            std::shared_ptr<vr::Node> wing;
            std::shared_ptr<vr::Transform> surface;
            for(const auto& child:owner->getChildren()){
                if(child->getRoomId()==zone.room) wing=child;
                if(child->getName()=="roomPortal_"+zone.room) surface=std::dynamic_pointer_cast<vr::Transform>(child);
            }
            if(wing) m_work.emplace_back(wing,wing->getUpdateEnabled());
            if(wing && surface){
                surface->setVisible(false);
                portals.push_back({zone.room,wing,surface,zone.openingMin,zone.openingMax});
            }
        }
        if(!portals.empty()){
            m_portals=std::make_unique<RoomPortalViews>(owner,std::move(portals));
            m_portals->setPrepareDestination([this](vr::Node& wing){ prepareDestination(wing); });
        }
        std::shared_ptr<vr::Node> root=owner;
        while (!root->getParents().empty()) root=root->getParents().front().lock();
        const auto sceneRoot=std::dynamic_pointer_cast<vr::Group>(root);
        if (sceneRoot) for (const auto& child:sceneRoot->getChildren())
            if (const auto camera=std::dynamic_pointer_cast<vr::Camera>(child)) m_camera=camera;
        if (m_camera.expired()) throw std::runtime_error("Missing scene camera for room isolation");
    }
    struct Mirror{std::weak_ptr<vr::Node> node;std::string room;bool visible;};
    std::vector<Mirror> m_mirrors;
    std::set<std::string> m_primaryRooms;
    float m_hubRadius=0,m_hubCeiling=44;
    bool m_fullHub=false,m_hubActive=false;
    std::weak_ptr<vr::Transform> m_target;
    std::weak_ptr<vr::Group> m_owner;
    std::weak_ptr<vr::Camera> m_camera;
    std::vector<Zone> m_zones;
    std::vector<std::pair<std::weak_ptr<vr::Node>,bool>> m_hidden;
    std::vector<std::pair<std::weak_ptr<vr::Node>,bool>> m_work;
    vr::UpdateContext m_context{0,0,false};
    std::set<std::string> m_updatedRooms;
    std::string m_active;
    bool m_initialized=false;
    std::unique_ptr<RoomPortalViews> m_portals;
};

inline void registerRoomIsolation(){
    vr::Reader::registerTransformAnimation("room-isolation",[](const auto& target,const auto& attrs){
        return std::make_shared<RoomIsolation>(target,attrs);
    });
}
} // namespace pantheon
