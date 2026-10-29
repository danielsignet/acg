#pragma once
#include <vr/Reader.h>
#include <vr/Camera.h>
#include <sstream>
#include <cmath>
#include <set>
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
        bool contains(const glm::vec3& p) const{
            if (glm::any(glm::lessThan(p,minimum-1e-4f)) ||
                glm::any(glm::greaterThan(p,maximum+1e-4f))) return false;
            const glm::vec2 d(p.x-circle.x,p.z-circle.y);
            return circle.z==0 || glm::dot(d,d)<=circle.z*circle.z+1e-4f;
        }
    };

    RoomIsolation(const std::shared_ptr<vr::Transform>& target,const vr::Reader::AnimationAttributes& attrs)
        : m_target(target){
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
            m_zones.push_back(zone);
        }
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
        m_updatedRooms.clear();
        for(const auto& [weak,enabled]:m_work) if(const auto wing=weak.lock()){
            const bool run=enabled && (active.empty() || wing->getRoomId()==active);
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
        if (!m_portals) return;
        if (m_active.empty()) {m_portals->render(context,"",{},{});return;}
        for(const auto& zone:m_zones) if(zone.room==m_active){
            m_portals->render(context,m_active,zone.openingMin,zone.openingMax);break;
        }
    }

private:
    void prepareDestination(vr::Node& wing){
        if(!m_updatedRooms.insert(wing.getRoomId()).second) return;
        struct Restore{
            vr::Node& node;bool enabled;
            ~Restore(){ node.setUpdateEnabled(enabled); }
        } restore{wing,wing.getUpdateEnabled()};
        wing.setUpdateEnabled(true);
        vr::UpdateVisitor update;
        update.update(wing,m_context);
        if(m_context.gpuSkinning) m_context.gpuSkinning->prepare();
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
