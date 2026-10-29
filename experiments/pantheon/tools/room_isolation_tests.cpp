#include "RoomIsolation.h"
#include <vr/GeometryFactory.h>
#include <vr/CullVisitor.h>
#include <vr/ShadowCollector.h>
#include <vr/LightCollector.h>
#include <vr/UpdateVisitor.h>
#include <iostream>

int main(){
    try{
        unsigned checks=0;
        const auto check=[&](bool pass,const char* message){
            ++checks;if(!pass) throw std::runtime_error(message);
        };
        vr::Scene scene;
        auto root=std::make_shared<vr::Group>("experiment");scene.getRoot()->addChild(root);
        auto state=std::make_shared<vr::State>();state->setCastShadows(true);root->setState(state);
        auto controller=std::make_shared<vr::Transform>("controller");root->addChild(controller);
        vr::Reader::AnimationAttributes attrs{
            {"zone-lapis","-24 0 -82 24 26 -34.66 0 -58 24"},
            {"zone-aes","34 0 -32 112 106 32 0 0 0"},
            {"zone-silva","-21 0 34 21 14 78 0 0 0"},
            {"zone-astra","-82 0 -24 -34.66 30 24 -58 0 24"},
            {"opening-lapis","-5.6 0 -34.66 5.6 10 -34.66"},
            {"opening-aes","34 0 -5.6 34 10 5.6"},
            {"opening-silva","-5.6 0 34 5.6 10 34"},
            {"opening-astra","-34.66 0 -5.6 -34.66 10 5.6"}};
        auto isolation=std::make_shared<pantheon::RoomIsolation>(controller,attrs);
        controller->setUpdateCallback(isolation);
        const auto geometry=vr::GeometryFactory::createGround();
        auto hall=std::make_shared<vr::Group>("hall");hall->setRoomId("rotunda");
        hall->addChild(geometry);root->addChild(hall);
        auto dome=std::make_shared<vr::Group>("dome");dome->setRoomId("rotunda");
        dome->addChild(geometry);root->addChild(dome);
        std::vector<std::shared_ptr<vr::Group>> wings;
        std::array<unsigned,4> ticks{};
        for (const std::string name:{"lapis","aes","silva","astra"}){
            auto wing=std::make_shared<vr::Group>(name);wing->setRoomId(name);
            wing->addChild(geometry);
            auto passage=std::make_shared<vr::Group>(name+" passage");
            passage->setRoomId(name+"-passage");passage->addChild(geometry);wing->addChild(passage);
            wing->addChild(std::make_shared<vr::Light>(name+" light"));
            const auto index=wings.size();
            auto work=std::make_shared<vr::Transform>("room work");
            work->setUpdateCallback(std::make_shared<vr::FunctionCallback>(
                [&,index](vr::Node&,const vr::UpdateContext&){ ++ticks[index]; }));
            wing->addChild(work);
            root->addChild(wing);wings.push_back(wing);
        }
        // A frustum-culling exemption must never bypass semantic isolation.
        wings[0]->setCullingEnabled(false);
        vr::UpdateVisitor update;
        vr::CullVisitor cull;
        vr::ShadowCollector shadows;
        const auto move=[&](const glm::vec3& eye,const std::string& room){
            const auto previous=ticks;
            scene.getCamera()->setLookAt(eye,eye+glm::vec3(0,0,-1));
            update.update(*scene.getRoot(),{0,0,false}); // visibility still follows a paused camera
            cull.collect(*scene.getRoot(),glm::mat4(1),eye,false);
            check(isolation->activeRoom()==room,"Incorrect active room");
            check(cull.getRenderList().size()==(room.empty()?10u:4u),"Wrong number of room/passage/Pantheon draws");
            for(const auto& draw:cull.getRenderList())
                check(room.empty() || draw.room==room || draw.room==room+"-passage" ||
                      (draw.room=="rotunda" && std::get<bool>(draw.state.getUniforms().at("uRoomWindowEnabled"))),
                      "An exterior subtree leaked into the room's render list");
            const auto casters=shadows.collect(*scene.getRoot(),eye);
            check(casters.size()==(room.empty()?10u:4u),"Unexpected shadow casters");
            for(const auto& caster:casters){
                check(room.empty() || caster.room==room || caster.room==room+"-passage" || caster.room=="rotunda",
                      "A hidden room leaked into shadow traversal");
                check(!caster.dynamic,"Controller must not mark static architecture as animated");
            }
            const auto lights=vr::collectLights(*scene.getRoot(),eye);
            check(lights.size()==4,
                  "Isolation must preserve light slot numbering");
            for(unsigned i=0;i<wings.size();++i){
                const bool active=room.empty() || wings[i]->getRoomId()==room;
                check(wings[i]->getUpdateEnabled()==active && ticks[i]==previous[i]+unsigned(active),
                      "Inactive rooms must not run update callbacks");
                check(lights[i].active==active,"Hidden room lights must be inactive without changing slots");
            }
        };
        move({0,3,0},"");
        move({0,3,-58},"lapis");
        move({58,3,0},"aes");
        move({0,3,58},"silva");
        move({-58,3,0},"astra");
        move({-35,3,23},""); // outside the round room, but inside its box
        move({-34.63f,3,0},""); // in the corridor
        move({-34.69f,3,0},"astra");
        move({0,3,0},"");
        dome->setVisible(false);
        scene.getCamera()->setLookAt({-58,3,0},{-58,3,-1});
        update.update(*scene.getRoot(),{0,0,false});
        scene.getCamera()->setLookAt({0,3,0},{0,3,-1});
        update.update(*scene.getRoot(),{0,0,true});
        check(!dome->getVisible() && hall->getVisible(),"Exit must restore prior visibility, including user-hidden nodes");
        std::cout << "Passed " << checks << " room-isolation checks: all four rooms, doorway transitions, shadows, light slots and culling exemptions\n";
        return 0;
    }catch(const std::exception& error){std::cerr << error.what() << '\n';return 1;}
}
