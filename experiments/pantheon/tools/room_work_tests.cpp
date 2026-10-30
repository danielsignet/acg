#include "RoomIsolation.h"
#include "GyroscopeAnimation.h"
#include <vr/GeometryFactory.h>
#include <vr/LightCollector.h>
#include <vr/OrbitCallback.h>
#include <vr/LodDemoCallback.h>
#include <vr/UpdateVisitor.h>
#include <GLFW/glfw3.h>
#include <iostream>

namespace{
void check(bool ok,const char* message){ if(!ok) throw std::runtime_error(message); }

void run(const std::filesystem::path& root){
    pantheon::registerGyroscopes();pantheon::registerRoomIsolation();
    vr::Reader reader(root);
    reader.registerNode("ground",vr::GeometryFactory::createGround());
    reader.registerNode("box",vr::GeometryFactory::createBox());
    reader.registerNode("sphere-low",vr::GeometryFactory::createSphere(8,6));
    reader.registerNode("material-sphere",vr::GeometryFactory::createSphere(64,48));
    reader.registerNode("torus",vr::GeometryFactory::createTorus(64,24));
    reader.registerNode("alphatest-panel",vr::GeometryFactory::createAlphaTestPanel());
    auto scene=reader.readScene("experiments/pantheon/scene.xml");
    const auto ring=scene->findTransform("lightOrbit");
    check(bool(ring),"Missing light orbit");
    ring->setUpdateCallback(std::make_shared<vr::OrbitCallback>(ring,38));
    unsigned ticks=0;
    auto counter=std::make_shared<vr::Transform>("portal-work-counter");
    counter->setUpdateCallback(std::make_shared<vr::FunctionCallback>(
        [&](vr::Node&,const vr::UpdateContext&){ ++ticks; }));
    ring->addChild(counter);
    vr::UpdateVisitor update;
    const auto camera=scene->getCamera();
    camera->setUpdateCallback(std::make_shared<vr::LodDemoCallback>(camera,glm::vec3(0)));
    camera->setPerspective(60,1,0.1f,150);
    camera->setLookAt({0,4,-50},{0,4,60});
    update.update(*scene->getRoot(),{.5,.5,true});
    check(ticks==0 && update.getSkinnedVertices()==0 && !update.hasDynamicUpdates(),
          "Sphere room must not update the hidden light ring, gyroscopes or dragon");
    const auto lights=vr::collectLights(*scene->getRoot(),camera->getPosition());
    for(unsigned i=0;i<lights.size();++i)
        check(lights[i].active==(i==4),"Only global sunlight is active in the sphere room");
    auto program=std::make_shared<vr::Program>(root/"shaders/phong-shading.vs",root/"shaders/phong-shading.fs");
    vr::RenderVisitor renderer(program);
    glViewport(0,0,256,256);
    renderer.render(*scene->getRoot(),*camera);
    check(ticks==1,"A visible legacy doorway must update its destination before capture");
    bool captured=false;
    for(const auto& capture:pantheon::RoomPortalViews::lastCaptures)
        captured=captured || capture.room=="silva";
    check(captured,"The legacy room must remain visible through its real portal texture");
    renderer.render(*scene->getRoot(),*camera);
    check(ticks==1,"Repeated views in one frame must not advance portal animations twice");
    camera->setLookAt({0,4,-50},{0,4,-70});
    update.update(*scene->getRoot(),{1,.5,true});
    renderer.render(*scene->getRoot(),*camera);
    check(ticks==1 && update.getSkinnedVertices()==0,
          "Looking away from the portal must leave its hidden destination asleep");
    camera->setLookAt({0,4,55},{0,4,71});
    update.update(*scene->getRoot(),{1.5,.5,true});
    check(ticks==2 && update.hasDynamicUpdates() && update.getSkinnedVertices()==0,
          "Entering legacy resumes its animations without updating the hidden dragon");
    camera->setLookAt({-55,4,0},{-60,5,0});
    update.update(*scene->getRoot(),{2,.5,true});
    check(update.getSkinnedVertices()>0 && ticks==2,"The dragon resumes only in its own room");
    const auto controller=scene->findTransform("Room visibility controller 001");
    check(bool(controller),"Missing room controller");
    const auto owner=std::dynamic_pointer_cast<vr::Group>(controller->getParents().front().lock());
    std::map<std::string,unsigned> roomTicks;
    for (const auto& wing:owner->getChildren()){
        const auto name=wing->getRoomId();
        if(name!="lapis" && name!="aes" && name!="silva" && name!="astra") continue;
        const auto group=std::dynamic_pointer_cast<vr::Group>(wing);
        auto clock=std::make_shared<vr::Transform>("hub-work-test");
        clock->setUpdateCallback(std::make_shared<vr::FunctionCallback>(
            [&,name](vr::Node&,const vr::UpdateContext&){++roomTicks[name];}));
        group->addChild(clock);
    }
    camera->setLookAt({0,4,0},{0,4,-60});
    update.update(*scene->getRoot(),{2.5,.5,true});
    check(roomTicks["lapis"]==1 && roomTicks["aes"]==0 && roomTicks["silva"]==0 && roomTicks["astra"]==0,
          "Inside the hall only the room behind the visible doorway should update");
    check(renderer.roomVisible(*scene->getRoot(),"lapis",*camera) &&
              !renderer.roomVisible(*scene->getRoot(),"astra",*camera),
          "Only visible hallway destinations should receive probe updates");
    renderer.render(*scene->getRoot(),*camera);
    for(const auto& [room,draws]:renderer.getStats().roomDraws)
        check(room!="aes" && room!="silva" && room!="astra",
              "Side-room geometry behind the hall walls must not enter the primary draw list");
    vr::Camera probe;
    probe.setPerspective(90,1,.1f,150);
    probe.setLookAt({-58,4,0},{-70,4,0});
    renderer.prepareFrameView(*scene->getRoot(),probe);
    check(roomTicks["astra"]==1,"A planned auxiliary observer must wake its own room before shadows");
    renderer.prepareFrameView(*scene->getRoot(),probe);
    check(roomTicks["astra"]==1,"Repeated auxiliary preparation must update a room once per frame");
    for(const auto& wing:owner->getChildren()) if(wing->getRoomId()=="aes") wing->setCullingEnabled(false);
    renderer.prepareFrameView(*scene->getRoot(),probe);
    check(roomTicks["aes"]==1,"Auxiliary preparation must honor inherited culling exemptions");
    for(const auto& wing:owner->getChildren()) if(wing->getRoomId()=="aes") wing->setCullingEnabled(true);
    camera->setLookAt({0,4,0},{0,40,-.1f});
    update.update(*scene->getRoot(),{3,.5,true});
    check(roomTicks["lapis"]==1 && roomTicks["astra"]==1 && roomTicks["silva"]==0,
          "Looking into the dome must leave every side-room animation asleep");
    check(glGetError()==GL_NO_ERROR,"Room work gating and portal captures leave valid GL state");
    std::cout << "Room work checks passed: sleeping updates/lights, visible portal animation, once per frame, room reentry\n";
}
}

int main(int argc,char** argv){
    if(argc!=2 || !glfwInit()) return 1;
    glfwWindowHint(GLFW_VISIBLE,GLFW_FALSE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,4);glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,1);
    glfwWindowHint(GLFW_OPENGL_PROFILE,GLFW_OPENGL_CORE_PROFILE);
    auto window=glfwCreateWindow(256,256,"Room work tests",nullptr,nullptr);
    if(!window){glfwTerminate();return 1;}
    glfwMakeContextCurrent(window);
    int result=0;
    try{
        if(!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress)))
            throw std::runtime_error("OpenGL failed");
        run(argv[1]);
    }catch(const std::exception& e){std::cerr << e.what() << '\n';result=1;}
    glfwDestroyWindow(window);glfwTerminate();return result;
}
