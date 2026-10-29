#include "GyroscopeAnimation.h"
#include "RoomIsolation.h"
#include <vr/GeometryFactory.h>
#include <vr/ModelPicker.h>
#include <vr/ModelInstance.h>
#include <vr/ReflectionProbes.h>
#include <vr/OrbitCallback.h>
#include <vr/LightCollector.h>
#include <vr/Light.h>
#include <vr/Flashlight.h>
#include <vr/UpdateVisitor.h>
#include <GLFW/glfw3.h>
#include <iostream>

int main(int argc,char** argv){
    GLFWwindow* window=nullptr;
    try{
        if(argc!=2) throw std::runtime_error("picking_audit resource-root");
        pantheon::registerGyroscopes();pantheon::registerRoomIsolation();
        if(!glfwInit()) throw std::runtime_error("GLFW failed");
        glfwWindowHint(GLFW_VISIBLE,GLFW_FALSE);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,4);glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,1);
        glfwWindowHint(GLFW_OPENGL_PROFILE,GLFW_OPENGL_CORE_PROFILE);
        window=glfwCreateWindow(64,64,"Picking audit",nullptr,nullptr);
        glfwMakeContextCurrent(window);
        if(!window || !gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress)))
            throw std::runtime_error("OpenGL failed");
        {
            vr::Reader reader(argv[1]);
            reader.registerNode("ground",vr::GeometryFactory::createGround());
            reader.registerNode("box",vr::GeometryFactory::createBox());
            reader.registerNode("sphere-low",vr::GeometryFactory::createSphere(8,6));
            reader.registerNode("material-sphere",vr::GeometryFactory::createSphere(64,48));
            reader.registerNode("torus",vr::GeometryFactory::createTorus(64,24));
            reader.registerNode("alphatest-panel",vr::GeometryFactory::createAlphaTestPanel());
            auto scene=reader.readScene("experiments/pantheon/scene.xml");
            vr::UpdateVisitor update;vr::CullVisitor cull;
            const auto collect=[&](const glm::vec3& eye){
                scene->getCamera()->setLookAt(eye,eye+glm::vec3(1,0,0));
                update.update(*scene->getRoot(),{.5,0,false});
                cull.collect(*scene->getRoot(),glm::mat4(1),eye,false);
            };
            const auto ring=scene->findTransform("lightOrbit");
            if(!ring) throw std::runtime_error("Legacy light orbit missing");
            vr::OrbitCallback orbit(ring,38);
            collect({0,5,56});
            const auto before=vr::collectLights(*scene->getRoot(),{0,5,56});
            const std::array<std::string,5> names{{"red","green","blue","white","Oculus sun"}};
            if(before.size()!=9) throw std::runtime_error("Unexpected scene light count");
            for(unsigned i=0;i<names.size();++i)
                if(before[i].node->getName()!=names[i]) throw std::runtime_error("Legacy light controls target the wrong slots");
            orbit.update(*ring,{1,1,false});
            const auto after=vr::collectLights(*scene->getRoot(),{0,5,56});
            for(unsigned i=0;i<4;++i)
                if(glm::distance(glm::vec3(before[i].model*before[i].node->getPosition()),
                                 glm::vec3(after[i].model*after[i].node->getPosition()))<1)
                    throw std::runtime_error("A colored light did not orbit inside its room");
            orbit.setEnabled(false);const auto frozen=ring->getMatrix();orbit.update(*ring,{2,1,false});
            if(ring->getMatrix()!=frozen) throw std::runtime_error("Light orbit pause failed");
            vr::Flashlight flashlight(*scene,true);
            if(vr::collectLights(*scene->getRoot(),{0,5,56}).size()!=10)
                throw std::runtime_error("The flashlight exceeds the scene light budget");
            std::cout << "Legacy lights: four orbiting markers, correct control slots, pause and ten-light capacity\n";
            const glm::vec3 eye(35,55,17);
            collect(eye);
            auto hit=vr::pickModel(cull.getRenderList(),eye,{1,0,0});
            if(!hit || hit->instance->getName().find("Tower 007 windows")==std::string::npos)
                throw std::runtime_error("Upper-floor facade is not selectable");
            std::cout << "Upper tower: " << hit->instance->getName() << '\n';
            float surface=0;
            const auto floorHit=vr::pickModel(cull.getRenderList(),{44,2,0},{0,-1,0},&surface);
            if(floorHit || std::abs(surface-2)>0.01f) throw std::runtime_error("DoF cannot focus an unselectable floor");
            std::cout << "DoF: unselectable floor provides geometric focus distance\n";
            bool leaf=false;
            for(const auto& item:cull.getRenderList()){
                if(!item.instance || item.instance->getName()!="Leaf") continue;
                const auto& v=item.geometry->getVertices();
                const auto& indices=item.geometry->getIndices();
                for(std::size_t i=0;i+2<indices.size() && !leaf;i+=3){
                    for(int u=1;u<10 && !leaf;++u) for(int t=1;t<10-u && !leaf;++t){
                        const auto local=(v[indices[i]].position*static_cast<float>(10-u-t)+v[indices[i+1]].position*static_cast<float>(u)+
                                          v[indices[i+2]].position*static_cast<float>(t))*.1f;
                        const auto point=glm::vec3(item.model*glm::vec4(local,1));
                        const auto normal=glm::normalize(glm::mat3(item.model)*v[indices[i]].normal);
                        const auto picked=vr::pickModel(cull.getRenderList(),point+normal*.05f,-normal);
                        leaf=picked && picked->instance->getName()=="Leaf";
                    }
                }
                if(leaf) break;
            }
            if(!leaf) throw std::runtime_error("Alpha-tested leaf is not selectable");
            std::cout << "Leaf: alpha-tested pixels selectable\n";
            collect({0,5,65});
            std::set<std::string> gyros;
            for(const auto& item:cull.getRenderList()){
                if(!item.instance || item.instance->getName().find("Gyroscope ")!=0) continue;
                const auto& v=item.geometry->getVertices();const auto& indices=item.geometry->getIndices();
                if(indices.size()<3) continue;
                const auto local=(v[indices[0]].position+v[indices[1]].position+v[indices[2]].position)/3.0f;
                const auto point=glm::vec3(item.model*glm::vec4(local,1));
                const auto normal=glm::normalize(glm::transpose(glm::inverse(glm::mat3(item.model)))*v[indices[0]].normal);
                const auto picked=vr::pickModel(cull.getRenderList(),point+normal*.02f,-normal);
                if(picked && picked->instance==item.instance) gyros.insert(item.instance->getName());
            }
            if(gyros.size()!=3) throw std::runtime_error("Not all three gyroscopes are selectable");
            std::cout << "Gyroscopes: all three selectable\n";
            auto program=std::make_shared<vr::Program>(std::filesystem::path(argv[1])/"shaders/phong-shading.vs",
                                                       std::filesystem::path(argv[1])/"shaders/phong-shading.fs");
            vr::EnvironmentMap environment(argv[1],"textures/sky/kloofendal_48d_partly_cloudy_puresky_2k.hdr");
            environment.configureRooms(argv[1],scene->getEnvironments());
            collect({6,2,-62});
            {vr::ReflectionProbes probes(*scene,environment,program);
             if(probes.getProbeCount()!=9) throw std::runtime_error("Sphere room did not receive its nine cubemaps");
             std::cout << "Sphere room: nine reflection probes\n";}
            collect({35,55,17});
            {vr::ReflectionProbes probes(*scene,environment,program);
             cull.collect(*scene->getRoot(),glm::mat4(1),{35,55,17},false);
             std::map<int,std::set<int>> groups;
             for(const auto& item:cull.getRenderList()){
                 const auto group=item.state.getUniforms().find("uReflectionGroup");
                 if(group==item.state.getUniforms().end() || !item.instance || !item.instance->getSelectable()) continue;
                 const int id=std::get<int>(group->second);
                 const int probe=std::get<int>(item.state.getUniforms().at("uReflectionProbe"));
                 if(probe<=0) throw std::runtime_error("A high building facade has no cubemap");
                 groups[id].insert(probe);
             }
             if(groups.size()!=4) throw std::runtime_error("Missing building reflection groups");
             for(const auto& [id,ids]:groups) if(ids.size()!=1) throw std::runtime_error("Building parts do not share one cubemap");
             std::cout << "City: all facade floors share four building cubemaps\n";}
            collect({0,3,0});
            {vr::ReflectionProbes probes(*scene,environment,program);
             if(probes.getProbeCount()!=vr::Texture::MaxCubeArrayLayers-environment.getSkyCount())
                 throw std::runtime_error("Rotunda reflection probes exceed the cubemap array budget");
             std::cout << "Rotunda: " << probes.getProbeCount() << " local probes + "
                       << environment.getSkyCount() << " skies fit the cubemap array\n";}
        }
        glfwDestroyWindow(window);glfwTerminate();return 0;
    }catch(const std::exception& error){
        std::cerr << error.what() << '\n';if(window) glfwDestroyWindow(window);glfwTerminate();return 1;
    }
}
